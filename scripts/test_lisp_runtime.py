"""Run the actual packaged Lisp in pinned VESC 6.05/7.00's 32-bit LispBM VM.

The VM, reader, const heap, traps and scheduler are upstream code. ESC API
fixtures are explicit host stubs; this does not validate ADC pins, CAN, motor
output, ESC timing or a different deployed firmware. No hardware is accessed.
Source downloads are immutable references; builds use a private temp directory.
Requires a 32-bit MinGW GCC (Windows). Run --source with a release main.lisp
to reproduce old failures with the same test fixture.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REF = '20cbb362687291242ab90b99f25fbfe8835540fc'
TREE = '8364d3d29596f1a6663d017cfbab52de9ed57be6'
VERSIONS = {
    '6.05': ('a0d40e2c5a42c810888d8c379307e6b0a118a125',
             'b5298e92f310094a71a6bd3fa812b47ed0301a1c'),
    '7.00': (REF, TREE),
}
CACHE = ROOT / 'research/_sources/lisp-runtime-audit' / REF
FIXTURES = ROOT / 'scripts/tests/lisp_runtime'
BASE = f'https://raw.githubusercontent.com/vedderb/bldc/{REF}/lispBM/lispBM/'


def fetch(path):
    target = CACHE / path
    if not target.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(BASE + path, timeout=30) as response:
            data = response.read()
        target.write_bytes(data)
    data = target.read_bytes()
    return {'path': path, 'url': BASE + path,
            'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}


def sources():
    CACHE.mkdir(parents=True, exist_ok=True)
    tree_file = CACHE / 'tree.json'
    if not tree_file.is_file():
        with urllib.request.urlopen(
                f'https://api.github.com/repos/vedderb/bldc/git/trees/{TREE}?recursive=1',
                timeout=30) as response:
            tree_file.write_bytes(response.read())
    tree = json.loads(tree_file.read_bytes())['tree']
    paths = [entry['path'] for entry in tree if entry['type'] == 'blob' and (
        entry['path'].startswith(('include/', 'src/', 'platform/windows/'))
        or entry['path'] in ('lispbm.mk', 'LICENSE', 'tests/test_lisp_code_cps.c'))]
    with ThreadPoolExecutor(max_workers=8) as pool:
        manifest = list(pool.map(fetch, paths))
    # Verify cached bytes against Git's immutable blob identifiers as well.
    blob_ids = {entry['path']: entry['sha'] for entry in tree}
    for entry in manifest:
        data = (CACHE / entry['path']).read_bytes()
        blob = b'blob ' + str(len(data)).encode() + b'\0' + data
        if hashlib.sha1(blob).hexdigest() != blob_ids[entry['path']]:
            raise RuntimeError(f'Upstream blob mismatch: {entry["path"]}')
    (CACHE / 'provenance.json').write_text(json.dumps(
        {'bldc_ref': REF, 'lispbm_tree': TREE, 'files': manifest}, indent=2) + '\n')
    makefile = (CACHE / 'lispbm.mk').read_text()
    block = makefile.split('LISPBM_SRC =', 1)[1].split('LISPBM_H =', 1)[0]
    return [CACHE / path for path in re.findall(r'\$\(LISPBM\)/(\S+\.c)', block)]


def firmware_sources():
    """Verify firmware bindings and, on 6.05, use its exact macro loader."""
    tree_file = CACHE / 'firmware-tree.json'
    if not tree_file.is_file():
        with urllib.request.urlopen(
                f'https://api.github.com/repos/vedderb/bldc/git/trees/{REF}?recursive=1',
                timeout=30) as response:
            tree_file.write_bytes(response.read())
    ids = {e['path']: e['sha'] for e in json.loads(tree_file.read_bytes())['tree']}
    manifest = []
    for name in ('lispif_vesc_extensions.c', 'lispif_vesc_dynamic_loader.c', 'lispif.c'):
        path = 'lispBM/' + name
        if path not in ids:
            continue  # 7.00 moved dynamic helpers into the VM library.
        target = CACHE / name
        url = f'https://raw.githubusercontent.com/vedderb/bldc/{REF}/{path}'
        if not target.is_file():
            with urllib.request.urlopen(url, timeout=30) as response:
                target.write_bytes(response.read())
        data = target.read_bytes()
        blob = b'blob ' + str(len(data)).encode() + b'\0' + data
        if hashlib.sha1(blob).hexdigest() != ids[path]:
            raise RuntimeError(f'Upstream blob mismatch: {path}')
        manifest.append({'path': path, 'url': url, 'sha256': hashlib.sha256(data).hexdigest()})
    (CACHE / 'firmware-provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    extensions = (CACHE / 'lispif_vesc_extensions.c').read_text()
    fixture = (FIXTURES / 'esc_api_fixture.lisp').read_text()
    names = re.findall(r'\(defun ([^ ]+)', fixture)
    missing = [name for name in names if f'lbm_add_extension("{name}"' not in extensions]
    if missing:
        raise RuntimeError(f'Fixture would mask absent firmware APIs: {missing}')
    for key in re.findall(r"\(\(eq key '([^ )]+)\)", fixture):
        if f'"{key}"' not in extensions:
            raise RuntimeError(f'Fixture would mask absent config key: {key}')
    print(f'Bindings audit: {len(names)} fixture APIs present in official firmware {REF}', flush=True)
    return extensions


def main():
    global REF, TREE, CACHE, BASE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vesc-version', choices=VERSIONS, default='6.05')
    parser.add_argument('--heap-cells', type=int, default=4096,
                        help='host cons cells, including fixture/test overhead; 6.05 firmware uses 2464')
    parser.add_argument('--source', type=Path, default=ROOT / 'lisp/main.lisp')
    parser.add_argument('--fault-diagnostics', action='store_true',
                        help='add fresh-VM first-fault tests; requires diagnostic Lisp source')
    parser.add_argument('--rx-startup', action='store_true',
                        help='add reverse-input startup race regression scenarios')
    parser.add_argument('--reverse-flow', action='store_true',
                        help='add reverse command flow and input-gate scenarios')
    parser.add_argument('--reverse-brake', action='store_true',
                        help='add reverse brake priority and release-ramp scenarios')
    parser.add_argument('--cc', default=shutil.which('gcc') or 'C:/msys64/mingw32/bin/gcc.exe')
    args = parser.parse_args()
    if not 64 <= args.heap_cells <= 1048576:
        parser.error('--heap-cells must be between 64 and 1048576')
    REF, TREE = VERSIONS[args.vesc_version]
    CACHE = ROOT / 'research/_sources/lisp-runtime-audit' / REF
    BASE = f'https://raw.githubusercontent.com/vedderb/bldc/{REF}/lispBM/lispBM/'
    src = sources()
    extension_source = firmware_sources()
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='vesc-lisp-runtime-') as tmp:
        tmp = Path(tmp)
        exe = tmp / 'lisp_runtime.exe'
        if args.vesc_version == '6.05':
            block = extension_source.split('static lbm_value make_list(', 1)[1].split(
                'static lbm_value ext_uavcan_last_rawcmd', 1)[0]
            registration = '\n'.join(line for line in extension_source.splitlines()
                                      if re.search(r'lbm_add_symbol_const\(.*&sym_(res|loop|break|brk|rst|return)\)', line)
                                      or 'lbm_add_extension("me-' in line)
            (tmp / 'firmware_macros.h').write_text(
                '#include <stdarg.h>\nstatic lbm_value make_list(' + block +
                '\nstatic void firmware_macros_init(void) {\n' + registration + '\n}\n')
            print('Unchanged 6.05 macro block SHA256: ' + hashlib.sha256(
                ('static lbm_value make_list(' + block).encode()).hexdigest(), flush=True)
            (tmp / 'lispif.h').write_text('#include <string.h>\n')
            platform = [str(FIXTURES / 'platform605/platform_mutex.c'),
                        str(CACHE / 'lispif_vesc_dynamic_loader.c')]
            platform_include = FIXTURES / 'platform605'
        else:
            platform = [str(CACHE / 'platform/windows/src/platform_mutex.c'),
                        str(CACHE / 'platform/windows/src/platform_timestamp.c')]
            platform_include = CACHE / 'platform/windows/include'
        command = [args.cc, '-std=gnu11', '-O1', '-DFULL_RTS_LIB',
                   '-DHOST_VESC_605=' + str(int(args.vesc_version == '6.05')),
                   '-DHOST_HEAP_CELLS=' + str(args.heap_cells),
                   # MinGW exposes C99 isnan/isinf, not the GNU float aliases.
                   '-Disnanf=__builtin_isnan', '-Disinff=__builtin_isinf',
                   '-DLBM_USE_DYN_FUNS', '-DLBM_USE_DYN_MACROS', '-DLBM_USE_DYN_LOOPS',
                   '-DLBM_USE_DYN_ARRAYS', '-DLBM_USE_ERROR_LINENO',
                   '-I' + str(CACHE / 'include'), '-I' + str(CACHE / 'include/extensions'),
                   '-I' + str(CACHE / 'src'), '-I' + str(platform_include), '-I' + str(tmp),
                   *map(str, src), *platform,
                   str(FIXTURES / 'runner.c'), '-o', str(exe), '-lm']
        subprocess.run(command, check=True, env=env, cwd=tmp)
        program = tmp / 'test.lisp'
        source = args.source.read_text(encoding='utf-8-sig')
        print(f'Upstream LispBM: VESC {args.vesc_version}; bldc {REF}; 32-bit host; '
              f'source {args.source}; SHA256 {hashlib.sha256(args.source.read_bytes()).hexdigest()}',
              flush=True)
        print(f'Host heap: {args.heap_cells} cons cells; auxiliary memory 18 KiB; GC stack 160; '
              'ESC APIs/test code add host overhead', flush=True)
        if args.vesc_version == '6.05':
            config = (CACHE / 'lispif.c').read_text()
            print('Verified official 6.05 lispif.c resource definitions:', flush=True)
            for line in config.splitlines():
                if re.match(r'#define\s+(HEAP_SIZE|LISP_MEM_SIZE|GC_STACK_SIZE)\s', line):
                    print(line, flush=True)
        result_code = 0
        scenarios = [
                ('ADC NONE / UI / PARK / range failure', '', 'cases.lisp'),
                ('native ADC Current at boot', '(setq host-adc-type 1)', 'cases_native_adc.lisp')]
        if args.fault_diagnostics:
            scenarios.extend([
                ('diagnostic healthy', '', 'cases_fault_healthy.lisp'),
                ('diagnostic native ADC at boot', '(setq host-adc-type 8)', 'cases_fault_boot.lisp'),
                ('diagnostic native ADC changed at runtime', '', 'cases_fault_native.lisp'),
                ('diagnostic transient ADC range', '', 'cases_fault_range.lisp'),
                ('diagnostic TX worker failure', '', 'cases_fault_tx.lisp'),
                ('diagnostic RX worker failure', '', 'cases_fault_rx.lisp'),
                ('diagnostic motor worker failure', '', 'cases_fault_motor.lisp'),
                ('diagnostic competing writers', '', 'cases_fault_race.lisp'),
            ])
        if args.rx_startup:
            startup = (FIXTURES / 'setup_rx_startup.lisp').read_text()
            # Canonical source has no recorder; assertions still exercise its
            # real safety/readiness behavior, and only skip diagnostic fields.
            startup += '\n(def host-has-diag ' + ('t' if '(def diag-first ' in source else 'nil') + ')\n'
            if '(def diag-first ' not in source:
                startup += '(def diag-first 0) (def diag-at 0) (def diag-tx-age 0) (def diag-rx-age 0) (def diag-motor-age 0)\n'
            scenarios.extend([
                ('RX delayed first read at nonzero uptime', startup, 'cases_rx_startup_delayed.lisp'),
                ('RX failed first read remains unsupported', startup, 'cases_rx_startup_failed.lisp'),
                ('RX initialized then stale still faults', startup, 'cases_rx_startup_stale.lisp'),
                ('RX reverse disabled startup', startup + '\n(setq host-reverse-config 0)', 'cases_rx_startup_disabled.lisp'),
            ])
        if args.reverse_flow:
            reverse = (FIXTURES / 'setup_reverse_flow.lisp').read_text()
            scenarios.extend([
                ('reverse Mode2 input gates and negative current', reverse, 'cases_reverse_flow.lisp'),
                ('reverse held button at boot', reverse + '\n(setq host-rx 0)', 'cases_reverse_held.lisp'),
                ('reverse unavailable RX hardware', reverse + "\n(defun gpio-configure (pin mode) (if (eq pin 'pin-rx) (exit-error 'unsupported-rx) t))", 'cases_reverse_unsupported.lisp'),
            ])
        if args.reverse_brake:
            reverse = (FIXTURES / 'setup_reverse_flow.lisp').read_text()
            scenarios.extend([
                ('reverse brake priority and release', reverse, 'cases_reverse_brake.lisp'),
                ('reverse maximum brake release ramp', reverse + '\n(setq host-ramp-neg 5.0)', 'cases_reverse_brake_max_ramp.lisp'),
            ])
        for name, setup, cases in scenarios:
            checks = (FIXTURES / cases).read_text()
            if cases.startswith('cases_fault_') and cases != 'cases_fault_healthy.lisp':
                checks += '\n' + (FIXTURES / 'cases_fault_assertions.lisp').read_text()
            program.write_text((FIXTURES / 'esc_api_fixture.lisp').read_text() + '\n' +
                               setup + '\n' + source + '\n' +
                               checks, encoding='utf-8')
            print(f'SCENARIO {name}', flush=True)
            result = subprocess.run([str(exe), str(program)], env=env, cwd=tmp,
                                    timeout=30, check=False)
            result_code |= result.returncode
        return result_code


if __name__ == '__main__':
    raise SystemExit(main())
