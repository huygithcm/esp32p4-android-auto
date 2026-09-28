"""Replay official ESC temperature conversion through production P4 decode/render.

Extracts unchanged source blocks from pinned official VESC 6.05/7.00 and the current
P4 cockpit. The ADC array/configuration are controlled host fixtures, NOT a
model of the user's unidentified ESC or its wiring. No hardware is accessed.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSIONS = {
    '6.05': ('a0d40e2c5a42c810888d8c379307e6b0a118a125', {
        'motor/mc_interface.c': 'ee171bd2f7f7b73c4cf7ae017cc0b691908ba9675eab4242fa1d0a3c3e5c3955',
        'hwconf/hw.h': '89e334db7172152a704e614ceded58846dc4289af7c88d86ec34375b0fbf4c05',
        'hwconf/trampa/vesc6/hw_60_core.h': '1303a9df97e953fe263f77ca8de40e589d8056ddf109dc84b3aee85ec275c41a',
        'util/utils_math.h': '646c3d227e0cd50665f139e187fc913ffd5ef3fb54ef6e960134ccf4975a8e12',
        'util/buffer.c': 'c4a5f539425482a918ce527da4b444404d29a8cfd41d193390dcc453074c5848',
        'datatypes.h': 'e34a29c4483a2d84fb60c634ffc15b848339be54eed222355be4de5b259fcc97',
    }),
    '7.00': ('20cbb362687291242ab90b99f25fbfe8835540fc', {
        'motor/mc_interface.c': 'c725a5562f722172a93592913a06119b77070089d8f563ff742708ddf1408a16',
        'hwconf/hw.h': '81e586f489ff2384a6dff9e5bb9f7325749499a172882252179ec7f397d40f01',
        'hwconf/trampa/vesc6/hw_60_core.h': '1303a9df97e953fe263f77ca8de40e589d8056ddf109dc84b3aee85ec275c41a',
        'util/utils_math.h': 'dfbf5a740fa0a8c568e85be935b48d85d72a2c5e124171381ada6fc388f9d171',
        'util/buffer.c': 'c4a5f539425482a918ce527da4b444404d29a8cfd41d193390dcc453074c5848',
        'datatypes.h': 'b0c82a0af140e46ba35d3db7b2784fc8e56f817c2c46404e766bed2bd06eeefc',
    }),
}


def source(path, version):
    ref, hashes = VERSIONS[version]
    target = ROOT / 'research/_sources/vesc-official-audit' / ('bldc-' + version) / path
    if not target.exists():
        with urllib.request.urlopen(f'https://raw.githubusercontent.com/vedderb/bldc/{ref}/{path}', timeout=30) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != hashes[path]:
            raise RuntimeError('Upstream hash mismatch: ' + path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    data = target.read_bytes()
    if hashlib.sha256(data).hexdigest() != hashes[path]:
        raise RuntimeError('Cached upstream hash mismatch: ' + path)
    return data.decode('utf-8').replace('\r\n', '\n')


def function(text, name):
    match = re.search(r'^(?:static )?(?:void|float) ' + name + r'\([^;]*?\)\s*\{', text, re.M)
    if not match:
        raise ValueError(name)
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def macro(text, name):
    return re.search(r'^#define[ \t]+' + name + r'(?:\(|[ \t]).*$', text, re.M).group(0)


def generated(version):
    src = {key: source(key, version) for key in VERSIONS[version][1]}
    board = src['hwconf/trampa/vesc6/hw_60_core.h']
    common = src['hwconf/hw.h']
    math = src['util/utils_math.h']
    blocks = [macro(board, n) for n in ('NTC_RES_MOTOR', 'NTC_TEMP_MOTOR')]
    blocks += [macro(common, n) for n in ('PTC_TEMP_MOTOR', 'PTC_TEMP_MOTOR_2',
               'NTC100K_TEMP_MOTOR', 'NTC100K_TEMP_MOTOR_2', 'NTCX_TEMP_MOTOR',
               'NTCX_TEMP_MOTOR_2', 'NTC_TEMP_MOTOR_2', 'MOTOR_TEMP_LPF')]
    blocks += [macro(math, n) for n in ('UTILS_IS_NAN', 'UTILS_IS_INF', 'UTILS_LP_FAST')]
    blocks.append(re.search(r'typedef enum \{\s*TEMP_SENSOR_NTC_10K_25C.*?\} temp_sensor_type;',
                            src['datatypes.h'], re.S).group(0))
    motor = src['motor/mc_interface.c']
    start = motor.index('\tfloat temp_motor = 0.0;')
    finish = motor.index('\tUTILS_LP_FAST(motor->m_temp_motor, temp_motor, MOTOR_TEMP_LPF);', start)
    finish += len('\tUTILS_LP_FAST(motor->m_temp_motor, temp_motor, MOTOR_TEMP_LPF);')
    blocks.append('static void official_temperature_step(motor_t *motor, conf_t *conf) {\n'
                  'bool is_motor_1 = true;\n' + motor[start:finish] + '\n}')
    blocks.append(function(src['util/buffer.c'], 'buffer_append_float16').replace(
        'void buffer_append_float16', 'static void official_append_float16', 1))
    blocks.append(function((ROOT/'Super_VESC_Display/custom/settings_wrapper.c').read_text(encoding='utf-8'),
                           'settings_wrapper_temp_to_display'))
    blocks.append(function((ROOT/'Super_VESC_Display/custom/custom.c').read_text(encoding='utf-8'),
                           'cockpit_temp_motor'))
    return '\n\n'.join(blocks) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--vesc-version', choices=VERSIONS, default='7.00')
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if not cc:
        parser.error('Compiler not found: ' + args.cc)
    fixture = ROOT / 'scripts/tests/mtemp_conversion'
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='mtemp-conversion-') as tmp:
        tmp = Path(tmp)
        (tmp/'extracted.inc').write_text(generated(args.vesc_version), encoding='utf-8')
        exe = tmp / 'conversion.exe'
        cmd = [cc, '-std=c11', '-O0', '-Wall', '-Wextra', '-I'+tmp.as_posix(),
               '-I'+(ROOT/'scripts/tests/vesc_telemetry/stubs').as_posix(),
               '-I'+(ROOT/'components/vesc_can/include').as_posix(),
               str(fixture/'test_mtemp_conversion.c'),
               str(ROOT/'components/vesc_can/vesc_rt_data.c'),
               str(ROOT/'components/vesc_can/vesc_io_data.c'),
               str(ROOT/'components/vesc_can/buffer.c'), '-lm', '-o', str(exe)]
        subprocess.run(cmd, check=True, env=env)
        print(f'Pinned VESC {args.vesc_version} source ({VERSIONS[args.vesc_version][0]}) + example VESC6 10k pull-up + production P4 decoder/cockpit', flush=True)
        print('Fixture beta=3380, LPF=0.01; ADC/config inputs are hypothetical, not measured hardware.', flush=True)
        return subprocess.run([str(exe)], env=env, timeout=15).returncode


if __name__ == '__main__':
    raise SystemExit(main())
