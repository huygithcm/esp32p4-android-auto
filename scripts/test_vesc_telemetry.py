"""Compile and exercise the production VESC RT/ADC packet decoders on the host.

Only time, task creation, logging and CAN transmit boundaries are stubbed.
Fixtures follow pinned official VESC 6.05/6.06/7.00 commands.c encodings.
These are decoded-packet tests, not CAN reassembly or physical sensor tests.
Use --baseline-ref HEAD to reproduce malformed-packet failures before a fix.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


CASES = (
    "video_temperature_values", "adc_does_not_change_temperature",
    "selective_motor_only", "full_setup_layout", "truncated_adc",
    "selective_mixed_fields", "unknown_trailing_field", "legacy_all_prefixes_rejected",
    "truncated_selective_no_refresh", "truncated_selective_atomic",
    "all_selective_prefixes_and_recovery",
    "truncated_current_no_field_shift", "short_header", "other_commands",
    "request_includes_motor_identity", "can_motor_identity",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--baseline-ref", help="compile RT/IO sources from this Git ref")
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "scripts/tests/vesc_telemetry"
    cc = shutil.which(args.cc)
    if not cc:
        parser.error(f"host compiler not found: {args.cc}")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="vesc-telemetry-") as temporary:
        output = Path(temporary)
        sources = []
        for name in ("vesc_rt_data.c", "vesc_io_data.c", "buffer.c"):
            relative = "components/vesc_can/" + name
            source = root / relative
            if args.baseline_ref:
                source = output / name
                source.write_bytes(subprocess.check_output(
                    ["git", "show", f"{args.baseline_ref}:{relative}"], cwd=root))
            sources.append(source)
        sources.append(fixture / "test_vesc_telemetry.c")
        baseline_include = output / "baseline_include"
        if args.baseline_ref:
            print("Baseline commit: " + subprocess.check_output(
                ["git", "rev-parse", args.baseline_ref], cwd=root, text=True).strip(), flush=True)
            for name in ("vesc_rt_data.h", "vesc_io_data.h", "vesc_datatypes.h"):
                path = baseline_include / "vesc_can" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(subprocess.check_output(["git", "show",
                    f"{args.baseline_ref}:components/vesc_can/include/vesc_can/{name}"], cwd=root))
        executable = output / ("telemetry.exe" if os.name == "nt" else "telemetry")
        command = [cc, "-std=c11", "-O0", "-g", "-Wall", "-Wextra"]
        if args.baseline_ref:
            command += ["-DTEST_BASELINE_LEGACY_RT", "-I" + baseline_include.as_posix()]
        command += ["-I" + (fixture / "stubs").as_posix(),
                    "-I" + (root / "components/vesc_can/include").as_posix()]
        command += [source.as_posix() for source in sources]
        command += ["-lm", "-o", executable.as_posix()]
        print("Compiling production RT/IO decoders and buffer.c "
              + (f"at {args.baseline_ref}" if args.baseline_ref else "(working tree)"),
              flush=True)
        subprocess.run(command, cwd=root, env=env, check=True)
        failed = []
        cases = args.case or CASES
        for case in cases:
            try:
                result = subprocess.run([str(executable), case], cwd=root, env=env,
                                        timeout=10, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
                print(result.stdout, end="", flush=True)
                passed = result.returncode == 0
            except subprocess.TimeoutExpired:
                print(f"FAIL {case}: timed out", flush=True)
                passed = False
            if not passed:
                failed.append(case)
        print(f"VESC telemetry: {len(cases) - len(failed)}/{len(cases)} PASS", flush=True)
        if failed:
            print("Failed: " + ", ".join(failed), flush=True)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
