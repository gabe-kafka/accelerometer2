#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

/**
 * Provision TLS CA certificate into the modem.
 * Must be called after modem init but before LTE connect.
 * Returns 0 on success, negative errno on failure.
 */
int transport_init(void);

/**
 * POST an accelerometer + battery reading to Supabase.
 * Call after LTE is connected.
 *
 * @param x_raw     Raw 14-bit X acceleration count [-8192, +8191]
 * @param y_raw     Raw 14-bit Y acceleration count [-8192, +8191]
 * @param z_raw     Raw 14-bit Z acceleration count [-8192, +8191]
 * @param battery_mv Battery voltage in millivolts (0 if unavailable)
 * Returns 0 on success, negative errno on failure.
 */
int transport_send_reading(int16_t x_raw, int16_t y_raw, int16_t z_raw, int battery_mv);

#endif /* TRANSPORT_H */
