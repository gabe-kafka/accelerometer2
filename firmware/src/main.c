#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

#include <zephyr/dfu/mcuboot.h>

#include "power.h"
#include "transport.h"

static const struct device *accel = DEVICE_DT_GET(DT_NODELABEL(accel));

/* Convert sensor_value (m/s²) to integer milliG */
static int sensor_val_to_mg(const struct sensor_value *val)
{
	/* micro_ms2 = value in micro-m/s² (millionths of m/s²) */
	int64_t micro_ms2 = (int64_t)val->val1 * 1000000 + val->val2;

	/* ADXL367 driver (NCS v2.9) has 10x scale error: divides by 10M
	 * instead of 1M in adxl367_accel_convert(). Compensate here. */
	return (int)(micro_ms2 * 10 / 9807);
}

static void read_accel(void)
{
	struct sensor_value x, y, z;
	int ret;

	ret = sensor_sample_fetch(accel);
	if (ret) {
		printk("sensor_sample_fetch failed: %d\n", ret);
		return;
	}

	sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &x);
	sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &y);
	sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &z);

	printk("Accel: X=%d.%06d  Y=%d.%06d  Z=%d.%06d  (m/s2)\n",
	       x.val1, x.val2, y.val1, y.val2, z.val1, z.val2);
}

static int modem_connect(void)
{
	int err;
	char buf[128];

	printk("\n--- Modem bringup ---\n");

	printk("Initializing modem...\n");
	err = nrf_modem_lib_init();
	if (err) {
		printk("nrf_modem_lib_init failed: %d\n", err);
		return err;
	}
	printk("Modem initialized.\n");

	/* Provision TLS cert before LTE connect */
	err = transport_init();
	if (err) {
		printk("WARNING: transport_init failed: %d (POST will fail)\n", err);
	}

	/* Read SIM ICCID */
	err = nrf_modem_at_cmd(buf, sizeof(buf), "AT+CCID");
	if (err) {
		printk("AT+CCID failed: %d\n", err);
	} else {
		printk("SIM ICCID: %s", buf);
	}

	/* Read IMSI */
	err = nrf_modem_at_cmd(buf, sizeof(buf), "AT+CIMI");
	if (err) {
		printk("AT+CIMI failed: %d\n", err);
	} else {
		printk("IMSI: %s", buf);
	}

	printk("Connecting to LTE network (this may take 10-60 seconds)...\n");
	err = lte_lc_connect();
	if (err) {
		printk("lte_lc_connect failed: %d\n", err);
		return err;
	}
	printk("Connected to LTE!\n");

	/* Print signal strength */
	err = modem_info_init();
	if (err) {
		printk("modem_info_init failed: %d\n", err);
	} else {
		int rsrp_raw;

		err = modem_info_get_rsrp(&rsrp_raw);
		if (err) {
			printk("modem_info_get_rsrp failed: %d\n", err);
		} else {
			printk("RSRP: %d dBm\n", RSRP_IDX_TO_DBM(rsrp_raw));
		}
	}

	return 0;
}

int main(void)
{
	printk("\n=== Thingy:91 X — Accel + LTE + Supabase ===\n\n");

	/* Confirm MCUboot image so bootloader doesn't revert */
	boot_write_img_confirmed();

	/* --- Step 1: Accelerometer --- */
	if (!device_is_ready(accel)) {
		printk("ERROR: ADXL367 not ready!\n");
		return 0;
	}
	printk("ADXL367 accelerometer ready.\n");

	/* --- Step 1b: Battery --- */
	if (power_init() != 0) {
		printk("WARNING: nPM1300 charger not ready\n");
	} else {
		int32_t bat_mv;
		uint8_t bat_pct;

		if (power_read_battery(&bat_mv, &bat_pct) == 0) {
			printk("Battery: %d.%03d V  (%u %%)\n",
			       bat_mv / 1000, bat_mv % 1000, bat_pct);
		} else {
			printk("WARNING: battery read failed\n");
		}
	}

	/* Show a few readings so the user can see it works */
	for (int i = 0; i < 5; i++) {
		read_accel();
		k_msleep(100);
	}

	/* --- Step 2: LTE-M modem + TLS cert --- */
	modem_connect();

	/* Wait for date_time to sync from modem after LTE attach */
	printk("Waiting for time sync...\n");
	k_msleep(3000);

	/* --- Step 3: POST readings to Supabase every 10 seconds --- */
	printk("\n--- Posting accel + battery to Supabase every 10s ---\n");

	while (1) {
		struct sensor_value x, y, z;
		int ret;

		/* Read accelerometer */
		ret = sensor_sample_fetch(accel);
		if (ret) {
			printk("sensor_sample_fetch failed: %d\n", ret);
			k_msleep(10000);
			continue;
		}
		sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &x);
		sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &y);
		sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &z);

		int x_mg = sensor_val_to_mg(&x);
		int y_mg = sensor_val_to_mg(&y);
		int z_mg = sensor_val_to_mg(&z);

		printk("Accel: x=%d  y=%d  z=%d  (mg)\n", x_mg, y_mg, z_mg);

		/* Read battery */
		int32_t bat_mv = 0;
		uint8_t bat_pct = 0;

		if (power_read_battery(&bat_mv, &bat_pct) == 0) {
			printk("Battery: %d.%03d V  (%u %%)\n",
			       bat_mv / 1000, bat_mv % 1000, bat_pct);
		}

		/* POST to Supabase */
		transport_send_reading(x_mg, y_mg, z_mg, bat_mv);

		k_msleep(10000);
	}

	return 0;
}
