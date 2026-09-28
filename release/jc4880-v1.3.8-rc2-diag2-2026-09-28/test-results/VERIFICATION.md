# RX startup fix verification

| Source | Host checks | Exit |
|---|---:|---:|
| Unchanged diag1 baseline | 45 pass / 11 fail | 1 expected |
| diag2 with fault diagnostics and RX startup suite | 127/127 pass | 0 |
| Canonical Lisp with RX startup suite | 56/56 pass | 0 |
| Canonical branch evaluator | 17/17 pass | 0 |

Actual official VESC 6.05 LispBM, 32-bit host, heap 2464 cells, auxiliary18KiB.
ESC ADC/GPIO/current/CAN APIs are mocked. Baseline original27 checks pass;
new29 startup assertions reveal11 failures. See [root-cause evidence](ROOT_CAUSE.md).

Commands from repository root (append --cc C:/msys64/mingw32/bin/gcc.exe if needed):

```powershell
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source release/jc4880-v1.3.8-rc2-diag1-2026-09-28/main.lisp --rx-startup
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp --fault-diagnostics --rx-startup
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source lisp/main.lisp --rx-startup
python -B scripts/test_lisp_safety.py --source lisp/main.lisp
```

Independent source and fixture review: no blocking defect. Primary verified
log counts, tested source hashes, runner syntax and canonical whitespace diff.
Initial clock-wrapper fixture timeout is retained separately and is not evidence
of a production failure. Corrected fixtures use the original native host clock.

Hardware feedback: diag2 returned healthy PARK/no recorded fault twice. RX
readiness and mode/reverse interactions still require confirmation; all-zero
recorder alone does not prove the RX worker completed its first read. No new P4
build, hardware flash by agents, configuration write, or M-TEMP claim.
