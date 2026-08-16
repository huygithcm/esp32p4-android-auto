# BMS frontend/backend contract

Owner split:

- Codex: LVGL frontend, tab lifecycle, simulator fixture.
- Claude: BLE central/session, vendor driver/parser, normalized backend model.

Claude's canonical backend contract is
`components/bms/include/bms/bms_model.h`. The frontend reads only
`bms_model_get()`, `bms_model_age_ms()` and `bms_model_link_state()` and maps
the result into its display-only structure in
`components/vesc_ui/include/bms_ui_contract.h`.

Pairing uses the backend control surface in `main/ble_bms_client.h`: the FE
starts/stops discovery, renders scan results and submits the selected address
to `ble_bms_bind()`. The FE does not perform GAP/GATT operations or parse
vendor frames.

Rules:

- Both calls are non-blocking and never call LVGL.
- `get_snapshot` copies one internally consistent snapshot; FE never retains a
  pointer into backend storage.
- `valid_mask` distinguishes an unavailable field from a real zero.
- `age_ms = UINT32_MAX` means no sample has arrived.
- Current and power are positive while discharging, negative while charging.
- Cell and temperature counts are bounded before copying.
- FE tab visibility never owns connection/reconnect policy. The current JK
  backend streams after probing, so the visibility hook is a no-op; it can
  become a request-rate hint for a future polled driver.
- FE is read-only. No backend setting/control command is exposed through this
  ABI.

Before the first backend sample, the real-device view displays
`BMS not configured`; the simulator supplies a 10S/40 V fixture. The current
backend binding is RAM-only, so persistence/rebind after reboot remains a BE
handoff. Balance-wire resistance is shown only when a future backend model
marks that field valid; the current JK model does not publish it.

## Parallel BE additions from the reference screens

The scrollable FE can ship independently. Claude has added an initial
`bms_snapshot_t`/JK decoder implementation for most optional fields, and the
FE maps the fields whose contract is currently safe. Every group needs a
`BMS_V_*` bit, and per-cell wire resistance also needs an element-valid mask;
zero is a legitimate value and must not mean “missing”.

- `wire_res_mohm[32]` plus `wire_res_valid_mask`: balance-lead resistance.
- `balance_current_ma`: active-balancer current, normalized as a non-negative
  magnitude.
- `cycle_capacity_mah`: lifetime/cycle-throughput capacity reported by BMS.
- `heater_current_ma`, `heater_on`, `charger_present`: heater and charger
  state. Do not infer heater state from temperature.
- `emergency_timer_s`, `sleep_timer_s`, `detail_log_count`: diagnostic values
  shown by the JK app.
- Additional temperature probes remain in `temp_deci_c[]`; FE computes cell
  average, voltage delta, used capacity, pack power, and charge/discharge flow,
  so BE must not duplicate those derived values.

If an offset is not verified for both `JK02_24S` and `JK02_32S`, leave its
valid bit clear. The UI will show it as unavailable instead of displaying a
plausible but incorrect zero.

Current BE follow-up discovered during integration:

- `sleep_timer_s` must be at least `uint32_t` because the reference shows
  `86400 s`; the current `uint16_t` cannot represent it.
- Do not set the timer valid bit until every advertised timer field has been
  decoded, or split emergency/sleep timers into separate valid bits.
- The display ABI already splits emergency and sleep validity. It maps the
  tested 32S emergency timer now and leaves sleep unavailable. SoH and the
  extended parser cases are covered by the current host test, but still need a
  capture from the user's physical BMS.
