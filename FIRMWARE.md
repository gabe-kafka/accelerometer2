# FIRMWARE — PR-SHM Sensor Node

> Firmware architecture, data flow, and module structure.
> Parent: ARCHITECTURE.md · Author: G. Kafka-Gibbons · DRAFT v0.2 · 2026-02-22

---

## Platform

| Parameter | Value |
|-----------|-------|
| MCU | nRF9151 SiP (Cortex-M33 @ 64 MHz, FPU) |
| RTOS | Zephyr 3.7 via nRF Connect SDK v2.9 |
| Board target | `thingy91x/nrf9151/ns` (non-secure, TF-M) |
| Build system | west + CMake |
| Language | C (Zephyr kernel APIs) |
| Debug | Serial console via USB (tty.usbmodem) |
| DFU | MCUboot over USB (`nrfutil device program`) |

---

## Data Flow

```
ADXL367 (I2C)          nPM1300 (PMIC)
     │                       │
     ▼                       ▼
 raw 14-bit counts      battery_mv (int32)
 (direct I2C read)           │
     │                       │
     ▼                       ▼
 ┌─────────────────────────────┐
 │ transport_send_reading()    │
 │  - build JSON body          │
 │  - HTTPS POST via rest_client│
 │  - TLS via modem (sec_tag 42)│
 └──────────────┬──────────────┘
                │
                ▼
 Supabase REST API → accel_readings table
                │
                ▼
         Frontend dashboard
```

---

## Module Structure

```
firmware/
├── CMakeLists.txt
├── prj.conf                         # Kconfig: I2C, LTE, REST, TLS
├── boards/
│   └── thingy91x_nrf9151_ns.overlay # DTS: enable ADXL367
├── src/
│   ├── main.c                       # Boot → raw I2C accel → modem → POST loop
│   ├── power.c / power.h            # nPM1300 battery voltage + percentage
│   └── transport.c / transport.h    # TLS cert provisioning + HTTPS POST
└── certs/
    └── GlobalSignRootCA.pem         # Root CA for Supabase TLS chain
```

---

## Module Details

### main.c — Application Entry

```
Boot sequence:
  1. Confirm MCUboot image (prevent bootloader revert)
  2. ADXL367 driver inits sensor into measurement mode (via Zephyr)
  3. Init nPM1300 battery reads (power_init)
  4. Init modem (nrf_modem_lib_init)
  5. Provision TLS cert (transport_init) — before LTE connect
  6. Read SIM ICCID + IMSI
  7. Connect LTE-M (lte_lc_connect, blocking)
  8. Print RSRP signal strength
  9. Wait 3 sec for date_time sync
  10. Enter POST loop (every 10 sec)

POST loop (infinite):
  1. Read ADXL367 raw 14-bit registers via I2C (read_accel_raw)
     - Poll STATUS register (0x0B) for DATA_RDY
     - Burst read 6 bytes from X_DATA_H (0x0E)
     - Parse 14-bit signed values with sign extension
  2. Read battery voltage (power_read_battery)
  3. POST raw counts to Supabase (transport_send_reading)
  4. Sleep 10 sec (k_msleep)
```

### transport.c — TLS + HTTPS POST

```
transport_init():
  1. Check if CA cert exists at sec_tag 42 (modem_key_mgmt_exists)
  2. If exists, compare (modem_key_mgmt_cmp)
  3. If mismatch or missing, write cert (modem_key_mgmt_write)
  Must be called after modem init, before LTE connect.

transport_send_reading(x_raw, y_raw, z_raw, battery_mv):
  1. Get wall-clock time via date_time_now()
  2. Format ISO-8601 timestamp with gmtime_r
  3. Build JSON: {"ts":"...","x_raw":N,"y_raw":N,"z_raw":N,"battery_v":N.NNN}
  4. Set REST client headers (apikey, Content-Type, Prefer)
  5. rest_client_request() — blocking HTTPS POST
  6. Check HTTP status (expect 201 Created)

TLS:
  - GlobalSign Root CA → GTS Root R4 → WE1 → supabase.co
  - Certificate provisioned to modem sec_tag 42
  - Modem handles TLS offloading (no app-side MbedTLS)
```

### power.c — Battery Monitoring

```
power_init():
  Get nPM1300 charger device handle via Zephyr sensor API.

power_read_battery(int32_t *voltage_mv, uint8_t *pct):
  Read SENSOR_CHAN_GAUGE_VOLTAGE → millivolts
  Read SENSOR_CHAN_GAUGE_STATE_OF_CHARGE → percentage (0–100)
```

---

## JSON Payload

```json
{
  "ts": "2026-02-22T23:24:17Z",
  "x_raw": -47,
  "y_raw": 100,
  "z_raw": -3673,
  "battery_v": 4.228
}
```

~85 bytes per POST. Raw 14-bit signed counts (range: -8192 to +8191).
At ±2g range: 1 LSB = 250 µg → ~4000 counts = 1g.
`battery_v` formatted as `millivolts / 1000 . millivolts % 1000`.

---

## Build + Flash

```bash
# Activate NCS virtual environment
source ~/ncs-venv/bin/activate

# Build (from NCS workspace)
cd ~/ncs
west build -b thingy91x/nrf9151/ns -p always ~/projects/accelerometer-easy/firmware

# Flash via MCUboot DFU (device connected via USB)
nrfutil device program --firmware ~/ncs/build/dfu_application.zip --serial-number <SERIAL>

# Monitor serial output
screen /dev/tty.usbmodem102 115200
```

---

## Kconfig Highlights (prj.conf)

```
# Accelerometer
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_ADXL367=y

# LTE modem
CONFIG_NRF_MODEM_LIB=y
CONFIG_LTE_LINK_CONTROL=y
CONFIG_MODEM_INFO=y

# Networking (modem-offloaded)
CONFIG_NETWORKING=y
CONFIG_NET_NATIVE=n
CONFIG_NET_SOCKETS=y
CONFIG_NET_SOCKETS_OFFLOAD=y

# HTTPS POST
CONFIG_REST_CLIENT=y
CONFIG_MODEM_KEY_MGMT=y
CONFIG_DATE_TIME=y
CONFIG_POSIX_API=y

# Battery (nPM1300 PMIC)
CONFIG_MFD=y
CONFIG_REGULATOR=y

# MCUboot
CONFIG_MCUBOOT_IMG_MANAGER=y

# Memory
CONFIG_HEAP_MEM_POOL_SIZE=16384
CONFIG_MAIN_STACK_SIZE=16384
```

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Raw 14-bit I2C over Zephyr sensor API | Zephyr ADXL367 driver has 10x scale bug in NCS v2.9. Direct I2C reads preserve full 14-bit resolution; normalize in post-processing. |
| Raw accel POST (no FFT) | Ship working product first. FFT can be layered on later. |
| Integer-only JSON formatting | Avoids float printf libc dependencies on embedded Zephyr. |
| Modem TLS offloading | nRF9151 handles TLS natively — saves ~50KB RAM vs app-side MbedTLS. |
| NCS rest_client over raw sockets | Handles DNS resolution, socket lifecycle, HTTP framing. |
| GlobalSign Root CA | Verified Supabase TLS chain: GlobalSign → GTS R4 → WE1 → supabase.co |
| 10-second POST interval | Fast enough for demo/validation; easily configurable. |
| date_time library for timestamps | Auto-syncs from modem after LTE attach. Falls back to epoch 0. |
| MCUboot DFU over J-Link | Field-updatable over USB without debug probe. |

---

## Doc Chain

```
PRD.md → SRS.md → ARCHITECTURE.md → POWER_BUDGET.md → BOM.md → FIRMWARE.md  ← you are here
                                                                 ├── TEST_PLAN.md
                                                                 └── FIELD_GUIDE.md
```
