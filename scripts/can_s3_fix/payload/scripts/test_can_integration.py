"""Host regression of actual main dispatch/target callbacks and BSP mux writes.

Run from the S3 root: python scripts/test_can_integration.py --cc <host-gcc>
No hardware access; this does not emulate I2C electrical behavior or FreeRTOS.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|esp_err_t) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(f'Function not found: {name}')
    start = match.start()
    depth = 1
    i = match.end()
    while depth:
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
        i += 1
    return source[start:i]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--out', type=Path, default=Path('build/can_host_tests'))
    args = ap.parse_args()
    root = args.root.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    main_c = (root / 'main/main.c').read_text(encoding='utf-8')
    bsp = (root / 'components/bsp_board/waveshare_esp32_s3_touch_lcd_7.c').read_text(encoding='utf-8')
    pins = (root / 'components/bsp_board/include/bsp/waveshare_esp32_s3_touch_lcd_7.h').read_text(encoding='utf-8')
    pin_def = re.search(r'^#define BSP_EXIO_USB_SEL\s+[^\n]+', pins, re.M).group(0)
    latch = re.search(r'^static uint8_t\s+ioext_shadow\s*=[^;]+;', bsp, re.M).group(0)
    preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr, ...) do { int e = (expr); if(e) return e; } while(0)
#define CONFIG_VESC_CAN_RT_INTERVAL_MS 100
#define CONFIG_VESC_CAN_LISP_INTERVAL_MS 100
#define BSP_IOEXT_REG_OUTPUT 3
static uint8_t target = 10, sender, targets[6], output;
static unsigned calls, ride_calls, ble_calls;
static bool i2c_fail;
static int ioext_init(void) { return ESP_OK; }
static int ioext_write(uint8_t reg, uint8_t value) {
    assert(reg == BSP_IOEXT_REG_OUTPUT);
    if (i2c_fail) return ESP_FAIL;
    output = value; return ESP_OK;
}
static uint8_t settings_get_target_vesc_id(void) { return target; }
static uint8_t comm_can_get_packet_sender_id(void) { return sender; }
#define TARGET_INIT(name, slot) static void name(uint8_t id, int ms) { (void)ms; targets[slot] = id; }
#define TARGET_SET(name, slot) static void name(uint8_t id) { targets[slot] = id; }
TARGET_INIT(vesc_rt_data_init, 0)
TARGET_INIT(vesc_lisp_poll_init, 1)
TARGET_INIT(vesc_io_data_init, 2)
TARGET_SET(vesc_lisp_code_set_target, 3)
TARGET_SET(vesc_lisp_panel_set_target, 4)
TARGET_SET(vesc_ride_mode_set_target, 5)
#define HANDLER(name) static void name(const uint8_t *p, unsigned n) { assert(p && n == 4); ++calls; }
HANDLER(vesc_rt_data_process_response)
HANDLER(vesc_lisp_poll_process_response)
HANDLER(vesc_lisp_console_process_response)
HANDLER(vesc_io_data_process_response)
HANDLER(vesc_lisp_code_process_response)
HANDLER(vesc_config_transport_process_response)
HANDLER(vesc_lisp_panel_process_response)
static void vesc_ride_mode_process_response(const uint8_t *p, unsigned n) { assert(p && n == 4); ++ride_calls; }
static void ble_nus_forward_response(const uint8_t *p, uint16_t n) { assert(p && n == 4); ++ble_calls; }
'''
    checks = r'''
int main(void) {
    const uint8_t packet[4] = {36, 'V', 'P', 0x89};
    assert(!(ioext_shadow & BSP_EXIO_USB_SEL));
    assert(bsp_exio_set(1u << 3, false) == ESP_OK);
    assert(!(output & BSP_EXIO_USB_SEL));
    assert(bsp_exio_set(1u << 3, true) == ESP_OK);
    assert(!(output & BSP_EXIO_USB_SEL));
    assert(bsp_can_mux_enable() == ESP_OK);
    assert(output & BSP_EXIO_USB_SEL);
    assert(bsp_exio_set(1u << 4, false) == ESP_OK);
    assert(output & BSP_EXIO_USB_SEL);
    uint8_t previous = ioext_shadow;
    i2c_fail = true;
    assert(bsp_exio_set(BSP_EXIO_USB_SEL, false) == ESP_FAIL);
    assert(ioext_shadow == previous);
    i2c_fail = false;
    on_target_id_changed(23);
    for (unsigned i = 0; i < 6; ++i) assert(targets[i] == 23);
    target = 23; sender = 10;
    vesc_packet_dispatch(packet, sizeof packet);
    assert(ride_calls == 0 && ble_calls == 1 && calls == 7);
    sender = 23;
    vesc_packet_dispatch(packet, sizeof packet);
    assert(ride_calls == 1 && ble_calls == 2 && calls == 14);
    puts("CAN integration: mux state, failed I2C write, six target routes, stale-sender rejection and BLE forwarding PASS");
    return 0;
}
'''
    text = preamble + '\n' + pin_def + '\n' + latch + '\n'
    text += function(bsp, 'bsp_exio_set') + '\n' + function(bsp, 'bsp_can_mux_enable') + '\n'
    text += function(main_c, 'on_target_id_changed') + '\n' + function(main_c, 'vesc_packet_dispatch') + '\n' + checks
    c_file = args.out / 'test_can_integration.c'
    exe = args.out / ('test_can_integration.exe' if os.name == 'nt' else 'test_can_integration')
    c_file.write_text(text, encoding='utf-8')
    env = os.environ.copy()
    compiler = Path(args.cc)
    if compiler.is_absolute():
        env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', str(c_file), '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe.resolve())], check=True, env=env)


if __name__ == '__main__':
    main()
