"""Run real LVGL pointer/animation tests against the device Lisp drawer.

Only the CAN model/action boundary and dashboard lookup are stubbed. The
production LV_REALDEVICE panel, managed LVGL, and generated font are compiled.
This is a headless host test, not touch-controller/CAN/hardware validation.

python scripts/test_lisp_panel_ui.py --cc <host-gcc>
Use --baseline-ref HEAD to demonstrate failures against the committed panel.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


CASES = (
    "opening_release_outside", "opening_release_inside", "next_outside_tap",
    "mode_and_park_actions", "missing_descriptor", "late_descriptor",
    "dashboard_only", "screen_change", "reopen_during_close",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--baseline-ref", help="compile the panel from this Git ref")
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    fixture = root / "scripts/tests/lisp_panel_ui"
    lvgl = root / "managed_components/lvgl__lvgl"
    panel_rel = "Super_VESC_Display/custom/lisp_panel.c"
    cc = shutil.which(args.cc)
    if cc is None:
        parser.error(f"host compiler not found: {args.cc}")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="lisp-panel-ui-") as temporary:
        output = Path(temporary)
        panel = root / panel_rel
        if args.baseline_ref:
            panel = output / "baseline_lisp_panel.c"
            panel.write_bytes(subprocess.check_output(
                ["git", "show", f"{args.baseline_ref}:{panel_rel}"], cwd=root))
        sources = sorted((lvgl / "src").rglob("*.c"))
        sources += [panel, fixture / "test_lisp_panel_ui.c", root /
                    "Super_VESC_Display/generated/guider_fonts/lv_font_montserratMedium_16.c"]
        executable = output / ("panel_ui.exe" if os.name == "nt" else "panel_ui")
        command = ["-std=c11", "-O0", "-g", "-DLV_REALDEVICE=1",
                   "-DLV_CONF_INCLUDE_SIMPLE=1", "-DLV_LVGL_H_INCLUDE_SIMPLE=1"]
        for include in (fixture, lvgl, lvgl / "src", root / "Super_VESC_Display/custom",
                        root / "Super_VESC_Display/generated",
                        root / "components/vesc_can/include",
                        root / "components/vesc_ui/include"):
            command += ["-I" + include.as_posix()]
        command += [source.as_posix() for source in sources]
        command += ["-lm", "-o", executable.as_posix()]
        response = output / "compile.rsp"
        response.write_text("\n".join('"' + arg.replace('"', '\\"') + '"'
                                     for arg in command), encoding="utf-8")
        print(f"Compiling managed LVGL and {panel_rel}"
              + (f" at {args.baseline_ref}" if args.baseline_ref else " (working tree)"),
              flush=True)
        subprocess.run([cc, "@" + response.as_posix()], cwd=root, env=env, check=True)
        failed = []
        for case in args.case or CASES:
            try:
                result = subprocess.run([str(executable), case], cwd=root, env=env,
                                        timeout=15)
                passed = result.returncode == 0
            except subprocess.TimeoutExpired:
                print(f"FAIL {case}: timed out", flush=True)
                passed = False
            if not passed:
                failed.append(case)
        count = len(args.case or CASES)
        print(f"LVGL device drawer: {count - len(failed)}/{count} PASS", flush=True)
        if failed:
            print("Failed: " + ", ".join(failed))
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
