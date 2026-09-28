"""Run actual CAN reassembly -> RT/IO decoder M-TEMP regression checks.

The suite exits nonzero when a transport/source/freshness contract is violated.
It does not change firmware or infer that synthetic traffic occurred on hardware.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

CASES = (
    "valid_fragmented_video_values", "adc_custom_interleaving",
    "overlapping_replies_crc_drop", "missing_changed_tail_crc_drop",
    "missing_unchanged_tail_refresh", "missing_tail_numeric_replay",
    "wrong_sender_temperature", "status4_is_separate", "short_status4_dlc",
    "no_prefix_or_duplicate_finish", "out_of_order_fragments",
    "long_fragment_roundtrip", "malformed_control_dlc", "status_dlc_validation",
    "overflow_and_recovery", "sender_scope_short_bridge",
    "duplicate_fragment_recovery",
    "dual_motor_base_sender", "matching_sender_wrong_motor",
    "inconsistent_sender_identity", "missing_motor_identity",
    "dual_motor_sender_wrap",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--baseline-ref", help="compile production CAN sources and headers from this Git ref")
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "scripts/tests/mtemp_transport"
    cc = shutil.which(args.cc)
    if not cc:
        parser.error(f"compiler not found: {args.cc}")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="mtemp-transport-") as temporary:
        output = Path(temporary)
        exe = output / ("test.exe" if os.name == "nt" else "test")
        production = root / "components/vesc_can"
        revision = "working tree"
        names = ("comm_can.c", "vesc_rt_data.c", "vesc_io_data.c", "buffer.c", "crc.c")
        if args.baseline_ref:
            revision = subprocess.check_output(
                ["git", "rev-parse", "--verify", args.baseline_ref + "^{commit}"],
                cwd=root, text=True).strip()
            production = output / "production"
            headers = subprocess.check_output(
                ["git", "ls-tree", "-r", "--name-only", revision, "--",
                 "components/vesc_can/include"], cwd=root, text=True).splitlines()
            paths = ["components/vesc_can/" + name for name in names] + headers
            for relative in paths:
                target = production / Path(relative).relative_to("components/vesc_can")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(
                    ["git", "show", f"{revision}:{relative}"], cwd=root))
        sources = [fixture / "test_mtemp_transport.c"]
        sources += [production / name for name in names if name != "comm_can.c"]
        command = [cc, "-std=c11", "-O0", "-g", "-Wall", "-Wextra",
                   '-DMTEMP_COMM_CAN_SOURCE="' + (production / "comm_can.c").as_posix() + '"',
                   "-I" + (fixture / "stubs").as_posix(),
                   "-I" + (production / "include").as_posix()]
        command += [p.as_posix() for p in sources] + ["-lm", "-o", exe.as_posix()]
        if "comm_can_get_packet_sender_id" in (production / "include/vesc_can/comm_can.h").read_text():
            command.insert(1, "-DMTEMP_HAS_SENDER_API=1")
        print("Compiling CAN/RT/IO/buffer/CRC at " + revision, flush=True)
        subprocess.run(command, cwd=root, env=env, check=True)
        failed = []
        cases = args.case or CASES
        for case in cases:
            result = subprocess.run([str(exe), case], cwd=root, env=env,
                                    timeout=10, capture_output=True, text=True)
            print(result.stdout, end="", flush=True)
            print(result.stderr, end="", flush=True)
            if result.returncode:
                print(f"Case {case} exited {result.returncode}", flush=True)
                failed.append(case)
        print(f"M-TEMP transport: {len(cases)-len(failed)}/{len(cases)} PASS")
        if failed:
            print("Violated contracts: " + ", ".join(failed))
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
