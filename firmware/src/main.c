#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

#include <zephyr/dfu/mcuboot.h>

#include "power.h"
#include "transport.h"

/* ADXL367 register addresses */
#define ADXL367_STATUS     0x0Bu
#define ADXL367_X_DATA_H   0x0Eu
#define ADXL367_DATA_RDY   BIT(0)

/* I2C bus + address from device tree (ADXL367 @ 0x1d on I2C2) */
static const struct i2c_dt_spec accel_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(accel));

/*
 * Read raw 14-bit accelerometer counts directly via I2C.
 * Bypasses the Zephyr ADXL367 sensor driver (which has a 10x scale
 * error in NCS v2.9) to get the rawest possible data.
 *
 * Output: signed 14-bit values in [-8192, +8191].
 * At ±2g range, 1 LSB ≈ 250 µg (per ADXL367 datasheet).
 */
static int read_accel_raw(int16_t *x, int16_t *y, int16_t *z)
{
	uint8_t status;
	uint8_t buf[6];
	int ret;

	/* Poll STATUS register until DATA_RDY */
	do {
		ret = i2c_reg_read_byte_dt(&accel_i2c, ADXL367_STATUS, &status);
		if (ret) {
			return ret;
		}
	} while (!(status & ADXL367_DATA_RDY));

	/* Burst read 6 bytes: X_H, X_L, Y_H, Y_L, Z_H, Z_L */
	ret = i2c_burst_read_dt(&accel_i2c, ADXL367_X_DATA_H, buf, 6);
	if (ret) {
		return ret;
	}

	/* Parse 14-bit signed values (high byte << 6 | low byte >> 2) */
	*x = ((int16_t)buf[0] << 6) | (buf[1] >> 2);
	if (*x & BIT(13)) {
		*x |= 0xC000; /* sign-extend bits 14-15 */
	}

	*y = ((int16_t)buf[2] << 6) | (buf[3] >> 2);
	if (*y & BIT(13)) {
		*y |= 0xC000;
	}

	*z = ((int16_t)buf[4] << 6) | (buf[5] >> 2);
	if (*z & BIT(13)) {
		*z |= 0xC000;
	}

	return 0;
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
	printk("\n=== Thingy:91 X — Raw Accel + LTE + Supabase ===\n\n");

	/* Confirm MCUboot image so bootloader doesn't revert */
	boot_write_img_confirmed();

	/* --- Step 1: Accelerometer I2C bus --- */
	if (!i2c_is_ready_dt(&accel_i2c)) {
		printk("ERROR: ADXL367 I2C bus not ready!\n");
		return 0;
	}
	printk("ADXL367 I2C bus ready (addr 0x%02x).\n", accel_i2c.addr);

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

	/* Show a few raw readings at startup */
	for (int i = 0; i < 5; i++) {
		int16_t x, y, z;

		if (read_accel_raw(&x, &y, &z) == 0) {
			printk("Accel raw: x=%d  y=%d  z=%d  (counts)\n", x, y, z);
		} else {
			printk("Accel raw read failed\n");
		}
		k_msleep(100);
	}

	/* --- Step 2: LTE-M modem + TLS cert --- */
	modem_connect();

	/* Wait for date_time to sync from modem after LTE attach */
	printk("Waiting for time sync...\n");
	k_msleep(3000);

	/* --- Step 3: POST raw readings to Supabase every 10 seconds --- */
	printk("\n--- Posting raw accel + battery to Supabase every 10s ---\n");

	while (1) {
		int16_t x, y, z;
		int ret;

		/* Read raw accelerometer via I2C */
		ret = read_accel_raw(&x, &y, &z);
		if (ret) {
			printk("read_accel_raw failed: %d\n", ret);
			k_msleep(10000);
			continue;
		}

		printk("Accel raw: x=%d  y=%d  z=%d  (counts)\n", x, y, z);

		/* Read battery */
		int32_t bat_mv = 0;
		uint8_t bat_pct = 0;

		if (power_read_battery(&bat_mv, &bat_pct) == 0) {
			printk("Battery: %d.%03d V  (%u %%)\n",
			       bat_mv / 1000, bat_mv % 1000, bat_pct);
		}

		/* POST to Supabase */
		transport_send_reading(x, y, z, bat_mv);

		k_msleep(10000);
	}

	return 0;
}
