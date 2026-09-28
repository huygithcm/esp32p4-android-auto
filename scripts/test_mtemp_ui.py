"""Compile exact production UI-pump functions plus complete RT/IO/head2 sources.

This is a host call-path test, not LVGL rendering, CAN transport or hardware.
Unrelated UI/platform boundaries are stubbed. No production files are modified.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


CASES = (
    "video_values_adc_current_gear_independent", "freshness_holds_then_recovers",
    "screen_gate", "demo_exit", "head2_id_age_and_primary",
    "selective_without_temperature_does_not_refresh",
    "legacy_truncation_rejected", "thermal_age_wrap_reset_inject",
    "realtime_viewer_temperature_age",
)


def extract_function(source, name):
    # Stop at the next top-level declaration. Braces in prose/strings therefore
    # cannot confuse extraction; fail closed if the expected signature drifts.
    match = re.search(r"^static void " + re.escape(name) + r"\([^\n]*\)\n\{", source, re.M)
    if not match:
        raise ValueError(f"production function not found: {name}")
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--baseline-ref", help="compile matching production files from this Git ref")
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "scripts/tests/mtemp_ui"
    cc = shutil.which(args.cc)
    if not cc:
        parser.error(f"compiler not found: {args.cc}")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
    def read_source(relative):
        if args.baseline_ref:
            return subprocess.check_output(["git", "show", f"{args.baseline_ref}:{relative}"], cwd=root).decode("utf-8")
        return (root / relative).read_text(encoding="utf-8")
    source = read_source("main/vesc_ui_updater.c")
    print("Production source: " + (subprocess.check_output(
        ["git", "rev-parse", args.baseline_ref], cwd=root, text=True).strip()
        if args.baseline_ref else "working tree"))
    names = ("push_zeros_locked", "push_rt_locked", "push_cruise_locked", "updater_lv_timer_cb")
    with tempfile.TemporaryDirectory(prefix="mtemp-ui-") as tmp:
        out = Path(tmp)
        extracted = "static bool s_zeros_pushed;\n"
        for name in names:
            block = extract_function(source, name)
            print(f"EXTRACT main/vesc_ui_updater.c:{name} sha256={hashlib.sha256(block.encode()).hexdigest()}")
            extracted += block
        (out / "updater_functions.inc").write_text(extracted, encoding="utf-8")
        viewer = read_source("Super_VESC_Display/custom/realtime_viewer.c")
        enum = re.search(r"typedef enum \{.*?\} rt_field_t;", viewer, re.S).group(0)
        (out / "viewer_enum.inc").write_text(enum, encoding="utf-8")
        block = extract_function(viewer, "update_cb")
        print(f"EXTRACT Super_VESC_Display/custom/realtime_viewer.c:update_cb sha256={hashlib.sha256(block.encode()).hexdigest()}")
        (out / "viewer_functions.inc").write_text(block, encoding="utf-8")
        binary = out / ("mtemp_ui.exe" if os.name == "nt" else "mtemp_ui")
        includes = [out, fixture / "stubs", root / "scripts/tests/vesc_telemetry/stubs",
                    root / "components/vesc_can/include", root / "components/dev_settings/include",
                    root / "main"]
        sources = [fixture / "test_mtemp_ui.c"]
        relative_sources = ["components/dev_settings/vesc_head2.c"]
        relative_sources += ["components/vesc_can/" + f for f in ("vesc_rt_data.c", "vesc_io_data.c", "buffer.c")]
        for relative in relative_sources:
            path = root / relative
            if args.baseline_ref:
                path = out / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(read_source(relative), encoding="utf-8")
            sources.append(path)
        if args.baseline_ref:
            # Pair changed protocol headers with the selected implementation.
            header_root = out / "baseline_include"
            for name in ("vesc_rt_data.h", "vesc_io_data.h", "vesc_datatypes.h"):
                path = header_root / "vesc_can" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(read_source("components/vesc_can/include/vesc_can/" + name), encoding="utf-8")
            includes.insert(1, header_root)
        command = [cc, "-std=c11", "-O0", "-g", "-Wall", "-Wextra", "-Werror"]
        command += ["-I" + str(p) for p in includes] + [str(p) for p in sources]
        command += ["-lm", "-o", str(binary)]
        subprocess.run(command, cwd=root, env=env, check=True)
        failed = 0
        cases = args.case or CASES
        for case in cases:
            result = subprocess.run([str(binary), case], cwd=root, env=env,
                                    timeout=15, capture_output=True, text=True)
            print(result.stdout + result.stderr, end="", flush=True)
            failed += result.returncode != 0
        print(f"M-TEMP UI path: {len(cases)-failed}/{len(cases)} PASS "
              "(correctness assertions; hardware not tested)")
        return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
