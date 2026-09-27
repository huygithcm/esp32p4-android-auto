"""Run only after the user approves compilation. Host tests, no flash.

python scripts/test_can_host.py --cc <host-gcc>
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--cc', default='gcc')
args = ap.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build/can_host_tests'
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
compiler = Path(args.cc)
if compiler.is_absolute():
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
tests = [
    ('comm_can', 'transport_mocks', ['components/vesc_can/buffer.c', 'components/vesc_can/crc.c']),
    ('ride_target', 'ride_mocks', ['components/vesc_can/buffer.c', 'components/vesc_can/vesc_ride_mode_parse.c']),
]
for name, mocks, sources in tests:
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    cmd = [args.cc, '-std=c11', '-Wall', '-Wextra',
           '-I', 'scripts/tests/can/' + mocks,
           '-I', 'components/vesc_can/include',
           'scripts/tests/can/test_' + name + '.c', *sources, '-lm', '-o', str(exe)]
    subprocess.run(cmd, cwd=root, env=env, check=True)
    subprocess.run([str(exe)], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'scripts/test_can_integration.py', '--cc', args.cc],
               cwd=root, env=env, check=True)
print('Host checks complete. No firmware build, hardware CAN or flash performed.')
