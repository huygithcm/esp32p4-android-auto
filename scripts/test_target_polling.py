"""Host regression for target-ID changes; actual callback and RT/IO lifecycle.

No CAN hardware or FreeRTOS scheduling is exercised.
Run: python scripts/test_target_polling.py --cc <host-gcc>
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = root / 'build/target_polling_test'
    out.mkdir(parents=True, exist_ok=True)
    rt = (root / 'components/vesc_can/vesc_rt_data.c').read_text(encoding='utf-8')
    io = (root / 'components/vesc_can/vesc_io_data.c').read_text(encoding='utf-8')
    app = (root / 'main/main.c').read_text(encoding='utf-8')
    preamble = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
#define CONFIG_VESC_CAN_RT_INTERVAL_MS 100
#define CONFIG_VESC_CAN_LISP_INTERVAL_MS 100
#define COMM_GET_DECODED_ADC 32
#define COMM_GET_DECODED_PPM 31
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint32_t now = 1000;
static uint32_t millis_now(void) { return now; }
static bool s_active, s_data_received;
static uint8_t s_target_vesc_id, rt_sent, io_sent, other[4];
static uint32_t s_request_interval_ms, s_last_request_ms, s_poll_interval_ms, s_last_poll_ms;
static int s_rt_data, s_io;
static unsigned rt_count, io_count;
static void vesc_rt_data_request(void) { rt_sent = s_target_vesc_id; ++rt_count; }
'''
    rt_code = '\n'.join(function(rt, n) for n in (
        'vesc_rt_data_init', 'vesc_rt_data_start', 'vesc_rt_data_stop',
        'vesc_rt_data_is_active', 'vesc_rt_data_loop'))
    # Separate the file-static IO state from RT state in this combined harness.
    io_code = '\n'.join(function(io, n) for n in (
        'vesc_io_data_init', 'vesc_io_data_set_active',
        'vesc_io_data_is_active', 'vesc_io_data_loop'))
    io_code = re.sub(r'\bs_active\b', 'io_active_state', io_code)
    io_code = re.sub(r'\bs_target_vesc_id\b', 'io_target', io_code)
    mocks = r'''
static bool io_active_state, lisp_active;
static uint8_t io_target;
static void send_cmd(uint8_t cmd) { (void)cmd; io_sent = io_target; ++io_count; }
static void vesc_lisp_poll_init(uint8_t id, unsigned ms) { other[0] = id; (void)ms; lisp_active = false; }
static bool vesc_lisp_poll_is_active(void) { return lisp_active; }
static void vesc_lisp_poll_start(void) { lisp_active = true; }
static void vesc_lisp_code_set_target(uint8_t id) { other[1] = id; }
static void vesc_lisp_panel_set_target(uint8_t id) { other[2] = id; }
static void vesc_ride_mode_set_target(uint8_t id) { other[3] = id; }
'''
    checks = r'''
int main(void) {
    vesc_rt_data_init(10, 100);
    vesc_rt_data_start();
    vesc_io_data_init(10, 150);
    vesc_rt_data_loop();
    CHECK(rt_count == 1 && rt_sent == 10);
    lisp_active = CONFIG_VESC_CAN_LISP_POLL_ENABLE;
    for (unsigned open = 0; open < 2; ++open) {
        vesc_io_data_set_active(open);
        unsigned before_rt = rt_count, before_io = io_count;
        on_target_id_changed(20 + open);
        now += 1000;
        vesc_rt_data_loop();
        vesc_io_data_loop();
        CHECK(rt_count == before_rt + 1 && rt_sent == 20 + open);
        CHECK(vesc_io_data_is_active() == (bool)open);
        CHECK(io_count == before_io + (open ? 2 : 0));
        if (open) CHECK(io_sent == 20 + open);
        for (unsigned i = 0; i < 4; ++i) CHECK(other[i] == 20 + open);
        CHECK(lisp_active == (bool)CONFIG_VESC_CAN_LISP_POLL_ENABLE);
    }
    vesc_rt_data_stop();
    lisp_active = false;
    vesc_io_data_set_active(false);
    unsigned before_rt = rt_count, before_io = io_count;
    on_target_id_changed(30);
    now += 1000;
    vesc_rt_data_loop();
    vesc_io_data_loop();
    CHECK(rt_count == before_rt && io_count == before_io);
    CHECK(!vesc_rt_data_is_active() && !lisp_active && !vesc_io_data_is_active());
    vesc_rt_data_start();
    vesc_rt_data_loop();
    CHECK(rt_count == before_rt + 1 && rt_sent == 30);
    puts("PASS target changes preserve active/paused RT, Lisp and IO polling");
    return 0;
}
'''
    source = out / 'target_polling.c'
    source.write_text(preamble + rt_code + mocks + io_code +
                      function(app, 'on_target_id_changed') + checks, encoding='utf-8')
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).resolve().parent) + os.pathsep + env['PATH']
    for enabled in (0, 1):
        exe = out / f'target_polling_{enabled}.exe'
        subprocess.run([args.cc, '-std=c11', f'-DCONFIG_VESC_CAN_LISP_POLL_ENABLE={enabled}',
                        str(source), '-o', str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)


if __name__ == '__main__':
    main()
