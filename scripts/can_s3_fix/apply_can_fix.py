"""Review/apply the prepared S3 CAN fix. This script NEVER builds or flashes.

Default mode only checks hashes and prints the planned changes. --apply writes
the reviewed payload after backing up every existing file. Any unexpected
source change causes refusal; no Git reset/checkout/stash is used.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import subprocess

PACKAGE = Path(__file__).resolve().parent
PRODUCTION = {
    'main/main.c', 'main/ble_nus.c',
    'components/vesc_can/comm_can.c',
    'components/vesc_can/vesc_ride_mode.c',
    'components/vesc_can/vesc_lisp_code.c',
    'components/vesc_can/include/vesc_can/comm_can.h',
    'components/bsp_board/waveshare_esp32_s3_touch_lcd_7.c',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(root, relative):
    rel = Path(relative)
    if rel.is_absolute() or '..' in rel.parts:
        raise ValueError(f'Unsafe path: {relative}')
    result = (root / rel).resolve()
    if not result.is_relative_to(root):
        raise ValueError(f'Path leaves target root: {relative}')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True, help='Existing S3 clone root')
    ap.add_argument('--apply', action='store_true', help='Apply files only; NEVER build/flash')
    args = ap.parse_args()
    root = args.root.resolve(strict=True)
    manifest = json.loads((PACKAGE / 'manifest.json').read_text(encoding='utf-8'))
    head = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    if head != manifest['head']:
        raise SystemExit(f'Clone HEAD changed: {head}; review/regenerate the package first.')
    config = (root / 'sdkconfig').read_text(encoding='utf-8')
    if 'CONFIG_IDF_TARGET="esp32s3"' not in config:
        raise SystemExit('Refusing: this is not the reviewed ESP32-S3 configuration.')
    planned = []
    for rel, record in manifest['files'].items():
        if rel not in PRODUCTION and not rel.startswith('scripts/tests/can/') and rel not in {
                'scripts/test_can_integration.py', 'scripts/test_can_host.py'}:
            raise SystemExit(f'File outside CAN-only allowlist: {rel}')
        dst = safe_path(root, rel)
        payload = safe_path(PACKAGE, 'payload/' + rel).read_bytes()
        if digest(payload) != record['after']:
            raise SystemExit(f'Payload hash mismatch: {rel}')
        current = dst.read_bytes() if dst.exists() else None
        actual = digest(current) if current is not None else None
        if actual == record['after']:
            print('ALREADY APPLIED', rel)
            continue
        if actual != record['before']:
            raise SystemExit(f'Collaborator changes detected, refusing to overwrite: {rel}')
        planned.append((rel, dst, current, payload))
        print('UPDATE' if current is not None else 'ADD', rel)
    for rel, expected in manifest['protected'].items():
        if digest(safe_path(root, rel).read_bytes()) != expected:
            raise SystemExit(f'Protected UI/Lisp file changed since preparation: {rel}')
    if not args.apply:
        print(f'CHECK ONLY: {len(planned)} file(s). No source writes, tests, build or flash.')
        return
    if not planned:
        print('All payload files already applied. No build or flash.')
        return
    log_path = safe_path(root, 'COLLABORATION_LOG.md')
    old_log = log_path.read_bytes()
    newline = '\r\n' if b'\r\n' in old_log else '\n'
    log_text = old_log.decode('utf-8').replace('\r\n', '\n')
    marker = '## Entries\n'
    if log_text.count(marker) != 1:
        raise SystemExit('Log format unexpected; refusing before any source writes.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup = safe_path(root, 'build/can_fix_backups/' + stamp)
    backup.mkdir(parents=True, exist_ok=False)
    for rel, dst, current, payload in planned:
        if current is not None:
            saved = safe_path(backup, rel)
            saved.parent.mkdir(parents=True, exist_ok=True)
            saved.write_bytes(current)
            if saved.read_bytes() != current:
                raise SystemExit(f'Backup verification failed: {rel}. No source writes.')
    (backup / 'COLLABORATION_LOG.md').write_bytes(old_log)
    (backup / 'manifest.json').write_bytes((PACKAGE / 'manifest.json').read_bytes())
    # All paths/hashes validated and backups persisted before the first source write.
    for rel, dst, current, payload in planned:
        now = dst.read_bytes() if dst.exists() else None
        if now != current:
            raise SystemExit(f'Concurrent change: {rel}. Stopped; backups at {backup}.')
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(payload)
        if dst.read_bytes() != payload:
            raise SystemExit(f'Write verification failed: {rel}; backups at {backup}.')
    if log_path.read_bytes() != old_log:
        raise SystemExit(f'Sources applied; log changed concurrently. Backups: {backup}. No build.')
    entry = (
        '\n### ' + datetime.now(timezone.utc).strftime('%Y-%m-%d') + ' - Codex - Apply CAN communication fixes\n'
        '- Files: CAN transport/header, format1 ride backend, main callbacks, BLE/Lisp\n'
        '  polling pause, BSP mux initialization, and host regression scripts.\n'
        '- Checks: package SHA256/source checks and persisted backups; UI/Lisp unchanged.\n'
        '- Status: source patch applied only. No firmware build or flash by this script.\n'
        '- Handoff: user confirmation required before building; retain format1 ESC Lisp.\n'
        '  Run host tests and S3 build after approval, then separate physical CAN checks.\n'
    )
    log_path.write_bytes(log_text.replace(marker, marker + entry, 1).replace('\n', newline).encode('utf-8'))
    print(f'Applied {len(planned)} file(s). Backups: {backup}')
    print('STOP: no tests, firmware build, flash, commit or push was performed.')


if __name__ == '__main__':
    main()
