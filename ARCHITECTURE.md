# ARCHITECTURE — PR-SHM Sensor Node

> System block diagram, subsystems, interfaces, data flow.
> Parent: SRS.md · Author: G. Kafka-Gibbons · DRAFT v0.2 · 2026-02-22

---

## System Diagram

```
                         ┌──────────────────────────────────────┐
                         │         SOLAR (2W panel)             │
                         │         5.5V open circuit            │
                         └──────────┬───────────────────────────┘
                                    │ Voc
                         ┌──────────▼───────────────────────────┐
                         │  THINGY:91 X                         │
                         │                                      │
                         │  ┌─────────────┐   ┌──────────────┐ │
                         │  │  nRF9151    │   │  nRF5340     │ │
                         │  │  SiP        │   │  (BLE debug) │ │
                         │  │             │   └──────────────┘ │
                         │  │  Cortex-M33 │                    │
                         │  │  1MB flash  │   ┌──────────────┐ │
                         │  │  256KB RAM  │   │  nPM1300     │ │
                         │  │             │   │  PMIC        │ │
                         │  │  I2C master │   │  LiPo 1350   │ │
                         │  │  GPIO       │   │  mAh         │ │
                         │  │             │   └──────────────┘ │
                         │  │  LTE-M      │                    │
                         │  │  modem      │───► LTE-M antenna  │
                         │  └──────┬──────┘                    │
                         │         │ I2C (onboard)             │
                         │  ┌──────▼──────┐                    │
                         │  │  ADXL367    │                    │
                         │  │  (onboard)  │                    │
                         │  └─────────────┘                    │
                         └──────────────────────────────────────┘
                                    │
                                    │ LTE-M (Band 2/4/12/13)
                                    │ HTTPS POST (REST)
                                    ▼
                         ┌──────────────────────────────────────┐
                         │  CLOUD                               │
                         │  Supabase (PostgreSQL + REST API)    │
                         │  ─► Frontend dashboard               │
                         └──────────────────────────────────────┘
```

---

## S1 — Sensing

The Thingy:91 X includes an onboard ADXL367 accelerometer connected to the nRF9151 via I2C. No hardware modifications are required — the stock platform is used as-is.

**Data rate:** 3 axes × 14-bit — sampled on demand (once per 10-second POST cycle).
**Interface:** Direct I2C register reads (bypass Zephyr sensor API due to 10x scale bug in NCS v2.9 driver).
**Output:** Raw 14-bit signed counts. At ±2g range: 1 LSB = 250 µg, ~4000 counts = 1g. Normalization deferred to post-processing.

→ Satisfies SRS-101 through SRS-107

---

## S2 — Processing

**Current implementation:** No on-node signal processing. Raw tri-axial acceleration (14-bit counts) and battery voltage are sent directly to the cloud every 10 seconds. This "stream raw" approach prioritizes simplicity, accuracy, and fast time-to-field.

**Future:** On-node FFT and feature extraction can be added later to reduce data volume and power consumption.

→ Partially satisfies SRS-201 through SRS-208 (raw data path; FFT deferred)

---

## S3 — Communications

Readings transmit every 10 seconds over LTE-M via HTTPS POST to Supabase REST API.

| Parameter | Value |
|-----------|-------|
| Modem | nRF9151 integrated LTE-M / NB-IoT |
| Protocol | HTTPS POST (TLS 1.2, port 443) |
| Endpoint | `https://rcaglkgoyemcjaszaahu.supabase.co/rest/v1/accel_readings` |
| Auth | Supabase anon key (API key header) |
| Payload | JSON, ~80 bytes |
| TLS | Modem-offloaded, GlobalSign Root CA provisioned to sec_tag 42 |
| Library | NCS `rest_client` (blocking HTTPS) |

→ Satisfies SRS-301, SRS-305

---

## S4 — Power

The system is continuously active (no sleep/wake duty cycle in current firmware). Power consumption is dominated by the LTE-M modem.

**Continuous operation (10-sec POST cycle):**

| Component | Current | Notes |
|-----------|---------|-------|
| nRF9151 active | ~5 mA | CPU + I2C |
| ADXL367 | ~3 µA | Measurement mode |
| nPM1300 | ~15 µA | Active, regulating |
| LTE-M TX (burst every 10s) | ~50 mA peak | ~3.5 sec per POST |
| **Estimated average** | **~20 mA** | **Continuous** |

**Autonomy (battery only, no solar):**
```
Battery:    1350 mAh × 0.8 usable = 1080 mAh
Avg draw:   ~20 mA
Runtime:    ~54 hr ≈ 2.3 days
```

**With solar:** Indefinite operation in PR conditions (same surplus as original design).

**Future:** Adding PSM sleep between POST cycles would dramatically reduce average current.

→ Partially satisfies SRS-401 through SRS-409

---

## S5 — Enclosure & Mounting

| | Phase 1 (CityLab) | Phase 2 (PR towers) |
|---|---|---|
| Enclosure | Stock Thingy:91 X shell (IP54) | Polycase WC-21 or equiv (IP67, UV-stabilized) |
| Sensor | ADXL367 onboard — no modifications | ADXL367 onboard — Thingy:91 X PCB bolted inside enclosure |
| Mount | Hose clamp or U-bolt | 316 SS L-bracket, 2× M6 bolts |
| Antenna | Onboard (plastic shell is RF-transparent) | External LTE-M patch if metal enclosure |
| Cable entry | N/A | IP67 gland for solar panel lead |

**Coupling path:** Structural member → bracket → enclosure wall → Thingy:91 X PCB → ADXL367.
Total compliance must keep **mount first resonance ≥ 200 Hz** (SRS-602).

→ Satisfies SRS-501 through SRS-507, SRS-601 through SRS-606

---

## S6 — Cloud

```
┌──────────┐     ┌──────────────────────┐     ┌──────────────┐
│  Nodes   │────►│ Supabase REST API    │────►│  Frontend    │
│ (HTTPS)  │     │ (PostgREST + Postgres)│     │  Dashboard   │
└──────────┘     └──────────────────────┘     └──────────────┘
```

| Component | Choice | Rationale |
|-----------|--------|-----------|
| API | Supabase PostgREST | Zero backend code, instant REST API over Postgres |
| Database | Supabase PostgreSQL | Hosted Postgres, row-level security, free tier |
| Dashboard | Custom frontend (Vite) | Real-time accel visualization |
| Auth | Supabase anon key | Simple API key auth for device POST |
| Export | SQL via Supabase dashboard | For thesis data analysis |

**Table schema:**
```sql
CREATE TABLE accel_readings (
  id         BIGSERIAL PRIMARY KEY,
  ts         TIMESTAMPTZ NOT NULL,
  x_raw      INT,
  y_raw      INT,
  z_raw      INT,
  battery_v  FLOAT
);
```

→ Satisfies SRS-801 through SRS-807 (adapted for Supabase)

---

## Firmware State Machine

```
           ┌──────┐
           │ BOOT │
           └──┬───┘
              │ init accel, battery, modem, TLS cert
              ▼
         ┌──────────┐
         │ LTE CONN │ provision cert → attach LTE-M
         └─────┬────┘
               │ connected, time synced
               ▼
         ┌──────────┐
    ┌───►│  READ +  │ read accel (raw counts) + battery (mV)
    │    │  POST    │ HTTPS POST JSON to Supabase
    │    └─────┬────┘
    │          │ wait 10 sec
    └──────────┘
```

→ Satisfies SRS-701 (simplified for current implementation)

---

## Key Decisions

| ID | Decision | Rationale |
|----|----------|-----------|
| D-1 | Thingy:91 X over nRF9160 DK | Integrated battery, PMIC, antenna, enclosure — weeks of work saved |
| D-2 | Onboard ADXL367 over external sensor | Zero hardware modifications, stock platform, faster to field |
| D-3 | Raw accel POST over on-node FFT | Ship working product first; FFT can be added later |
| D-4 | HTTPS REST over MQTT | Supabase has a REST API; no broker to manage |
| D-5 | Supabase over AWS IoT + TimescaleDB | Zero backend code, instant API, free tier, simpler |
| D-6 | JSON over CBOR | Human-readable, trivial to debug, ~80 bytes is fine for LTE-M |
| D-7 | NCS rest_client over raw sockets | Handles DNS, TLS setup, HTTP framing automatically |
| D-8 | Store battery_v only in DB, derive % in frontend | Voltage is ground truth — percentage is a display concern |
| D-9 | Modem-offloaded TLS over app-side MbedTLS | nRF9151 modem handles TLS natively, saves RAM and complexity |

---

## Doc Chain

```
PRD.md → SRS.md → ARCHITECTURE.md  ← you are here
                  ├── POWER_BUDGET.md
                  ├── BOM.md
                  ├── FIRMWARE.md
                  ├── TEST_PLAN.md
                  └── FIELD_GUIDE.md
```
