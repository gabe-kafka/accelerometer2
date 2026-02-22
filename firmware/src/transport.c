#include <stdint.h>
#include <zephyr/kernel.h>
#include <modem/modem_key_mgmt.h>
#include <net/rest_client.h>
#include <zephyr/net/http/client.h>
#include <date_time.h>

#include "transport.h"

#define TLS_SEC_TAG	42
#define SUPABASE_HOST	"rcaglkgoyemcjaszaahu.supabase.co"
#define SUPABASE_URL	"/rest/v1/accel_readings"
#define SUPABASE_PORT	443

#define SUPABASE_ANON_KEY \
	"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9." \
	"eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJjYWdsa2dveWVtY2phc3phYWh1Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzE3MTcyNzcsImV4cCI6MjA4NzI5MzI3N30." \
	"m2lz6wQIlv9PfuT7_DU4HZoBcCnK_kzXtJOAI7b6UV4"

/* GlobalSign Root CA — trust anchor for Supabase (via Google Trust Services) */
static const char ca_cert[] = {
	#include "../certs/GlobalSignRootCA.pem"
};

/* Buffers */
static char body_buf[256];
static char resp_buf[2048];

int transport_init(void)
{
	int err;
	bool exists;

	printk("Provisioning TLS certificate (sec_tag %d)...\n", TLS_SEC_TAG);

	err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
	if (err) {
		printk("modem_key_mgmt_exists failed: %d\n", err);
		return err;
	}

	if (exists) {
		int mismatch = modem_key_mgmt_cmp(TLS_SEC_TAG,
						  MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
						  ca_cert, sizeof(ca_cert));
		if (!mismatch) {
			printk("TLS certificate already provisioned.\n");
			return 0;
		}
		printk("TLS certificate mismatch, updating...\n");
		modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
	}

	err = modem_key_mgmt_write(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   ca_cert, sizeof(ca_cert));
	if (err) {
		printk("modem_key_mgmt_write failed: %d\n", err);
		return err;
	}

	printk("TLS certificate provisioned.\n");
	return 0;
}

int transport_send_reading(int16_t x_raw, int16_t y_raw, int16_t z_raw, int battery_mv)
{
	int err;
	int64_t ts_ms;
	time_t ts_sec;
	struct tm tm_buf;

	/* Get current wall-clock time from the modem */
	err = date_time_now(&ts_ms);
	if (err) {
		printk("date_time_now failed: %d (using epoch)\n", err);
		ts_ms = 0;
	}
	ts_sec = ts_ms / 1000;
	gmtime_r(&ts_sec, &tm_buf);

	/* Build JSON body — raw 14-bit counts, no conversion */
	int len = snprintf(body_buf, sizeof(body_buf),
		"{\"ts\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\","
		"\"x_raw\":%d,\"y_raw\":%d,\"z_raw\":%d,"
		"\"battery_v\":%d.%03d}",
		tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
		tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
		x_raw, y_raw, z_raw,
		battery_mv / 1000, battery_mv % 1000);

	if (len < 0 || len >= (int)sizeof(body_buf)) {
		printk("JSON body too large\n");
		return -ENOMEM;
	}

	/* REST client request */
	static const char *headers[] = {
		"apikey: " SUPABASE_ANON_KEY "\r\n",
		"Content-Type: application/json\r\n",
		"Prefer: return=minimal\r\n",
		NULL
	};

	struct rest_client_req_context req;
	struct rest_client_resp_context resp;

	rest_client_request_defaults_set(&req);
	req.host		= SUPABASE_HOST;
	req.port		= SUPABASE_PORT;
	req.url			= SUPABASE_URL;
	req.sec_tag		= TLS_SEC_TAG;
	req.tls_peer_verify	= REST_CLIENT_TLS_DEFAULT_PEER_VERIFY;
	req.http_method		= HTTP_POST;
	req.header_fields	= headers;
	req.body		= body_buf;
	req.body_len		= len;
	req.resp_buff		= resp_buf;
	req.resp_buff_len	= sizeof(resp_buf);
	req.timeout_ms		= 30000;

	printk("POST %s (%d bytes)...\n", SUPABASE_URL, len);

	err = rest_client_request(&req, &resp);
	if (err) {
		printk("rest_client_request failed: %d\n", err);
		return err;
	}

	printk("HTTP %d\n", resp.http_status_code);

	if (resp.http_status_code != 201 && resp.http_status_code != 200) {
		printk("Unexpected status. Body: %.*s\n",
		       (int)resp.response_len, resp.response ? resp.response : "");
		return -EIO;
	}

	return 0;
}
