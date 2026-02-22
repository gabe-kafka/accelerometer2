# PRD — PR-SHM Sensor Node

> Wireless accelerometer node for structural health monitoring of lattice
> steel transmission towers through the 2026 Atlantic hurricane season.

```
Author:   Gabriel Kafka-Gibbons
Org:      NYU Tandon School of Engineering
Type:     Solo MSc thesis — self-funded proof of concept
Status:   DRAFT v0.2 | 2026-02-22
```

## Problem

PREPA transmission towers lack continuous structural monitoring. Post-hurricane
assessment is visual-only. Vibration-based SHM can detect degradation (bolt
loosening, fatigue, settlement) through natural frequency and damping shifts
before visible failure occurs.

## Scope

**In:** 3–5 nodes, 1–3 structures. Phase 1 at CityLab (NYC), Phase 2 on PR
towers. Tri-axial accel → on-node FFT → LTE-M → cloud → frequency trends.

**Out:** Production enclosure, real-time alerts, mesh networking, SCADA
integration, >5 nodes.

## Success Criteria

```
SC-1  ≥30 days continuous data collection, no manual intervention
SC-2  Extracted frequencies match expected modes (±10% of hand calc)
SC-3  End-to-end pipeline: sensor → LTE-M → Supabase → dashboard
SC-4  Survives outdoor deployment (rain, wind, UV, temp cycling)
```

Stretch:
```
SC-5  Detect frequency shift correlated with named storm
SC-6  Deploy ≥1 node on actual PREPA tower
```

## Hardware

```
Platform:       Nordic Thingy:91 X — $126.25
                  nRF9151 SiP, LTE-M/NB-IoT, 1350 mAh LiPo, IP54
Accelerometer:  ADXL367 (onboard Thingy:91 X)
                  200 µg/√Hz noise, 14-bit, ±2/4/8g, I2C
                  Built-in — no external wiring required
Power:          Onboard LiPo + 2W external solar panel
Enclosure:      Stock Thingy:91 (Phase 1), IP67 upgrade (Phase 2)
```

## Software

```
Firmware:       Zephyr RTOS (nRF Connect SDK v2.9)
Sample:         On-demand 3-axis accel read (every 10 sec)
Processing:     None — raw 14-bit counts sent directly, normalize in post-processing
Transmit:       JSON: {ts, x_raw, y_raw, z_raw, battery_v} (~85 bytes)
Protocol:       HTTPS POST over LTE-M (Supabase REST API)
Cloud:          Supabase (PostgreSQL + PostgREST) → Custom frontend dashboard
```

## Timeline

```
Wk 1–2   Firmware: ADXL367 driver, battery read, LTE connect ✓
Wk 3–4   Firmware: HTTPS POST to Supabase, end-to-end pipeline ✓
Wk 5–6   Firmware: On-node FFT, feature extraction (if needed)
Wk 7–8   Phase 1: CityLab deploy + validate
Wk 9–10  Iterate, fix field issues
Wk 11+   Phase 2: Ship to PR, deploy on towers
          ─── June 1: Hurricane season starts ───
          Collect through November 30
```

## Budget

```
5x Thingy:91          $631
5x Mounting hardware    $25
5x Weatherproofing      $40
5x Solar panels         $60
5x SIM data (6 mo)    $150
Shipping                $50
─────────────────────────────
TOTAL               ~$956
```

## Constraints

```
C-1  Budget ≤ $2,000
C-2  Solo build — no fab team
C-3  Baseline data collecting by June 1, 2026
C-4  Dev kits only — no custom PCB
C-5  Zephyr RTOS (nRF Connect SDK)
C-6  Cellular-only backhaul (no Wi-Fi at tower sites)
```

## Risks

```
R-1  PR tower access falls through → thesis succeeds on CityLab data
R-2  LTE-M coverage gap → NB-IoT fallback + local SD logging
R-3  No hurricane in PR → trade winds provide daily ambient data
R-4  Onboard ADXL367 noise too high for calm-day baselines → rely on moderate-wind data
```

## Open Questions

```
OQ-1  Specific CityLab test structure type?
OQ-2  Advisor requires FEA comparison or empirical-only acceptable?
OQ-3  Cloud platform selection (AWS vs Azure)?
OQ-4  SIM provider for PR LTE-M coverage?
OQ-5  NYU IP constraints on thesis hardware/software?
```

## Doc Chain

```
PRD.md  ← you are here
├── SRS.md            Numbered testable requirements
├── ARCHITECTURE.md   Block diagram, interfaces
├── POWER_BUDGET.md   Current draw by state, autonomy
├── BOM.md            Part numbers, costs, justifications
├── FIRMWARE.md       State machine, data flow
├── TEST_PLAN.md      Verification per SRS requirement
└── FIELD_GUIDE.md    Installation, provisioning, maintenance
```
