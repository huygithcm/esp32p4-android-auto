# Actual VESC LispBM host runtime checks

Run `python scripts/test_lisp_runtime.py --vesc-version 6.05` (default) or
`--vesc-version 7.00`; on this Windows host use
`--cc C:/msys64/mingw32/bin/gcc.exe`. `--source` selects an old release Lisp
without editing it. Builds use separate temporary directories; no ESC is opened.

The runner verifies immutable Git blobs from official `vedderb/bldc`:

- 6.05: `a0d40e2c5a42c810888d8c379307e6b0a118a125`, LispBM tree
  `b5298e92f310094a71a6bd3fa812b47ed0301a1c`.
- 7.00: `20cbb362687291242ab90b99f25fbfe8835540fc`, LispBM tree
  `8364d3d29596f1a6663d017cfbab52de9ed57be6`.

Both compile the unmodified VM, reader, scheduler, trap handling and extensions.
6.05 uses its actual firmware dynamic loader and an unchanged extraction of
`make_list` through `ext_me_loopforeach` from `lispif_vesc_extensions.c`, together
with the exact symbol/extension registrations. The extraction hash is printed.
7.00 uses its bundled dynamic library and Windows platform implementation.
6.05 predates that Windows platform, so a host mutex adapter and Windows clock
provide the existing platform API. Its actual const-heap API is used, rather
than substituting the newer image API.

Before execution, every ESC function supplied by `esc_api_fixture.lisp` must
have an official firmware extension registration, and every stubbed config key
must exist in that source. This prevents silently supplying absent firmware
APIs. API behavior remains a fixture, not an integration of ESC drivers; a
symbol check alone does not prove every argument/configuration variant.

Two fresh VM runs cover 18 healthy/range-fault checks and 9 native ADC Current
conflict checks. The actual canonical source is loaded unchanged, including
`@const-start/end`, all workers and the panel event receiver. Checks cover mode
2/3 while PARKed, physical TX button cycling, held throttle in PARK, range-fault
refusal, trap handling and correctly reported safety/mode faults. Saved native
ADC Current (nonzero) is intentionally rejected: this verifies the configured
safety interlock, not a workaround that enables competing motor controllers.

The default host heap has 4096 cons cells for source plus test/fixture overhead.
Use `--heap-cells 2464` to reproduce the pinned 6.05 firmware's cons-heap size.
The complete 27-check suite also passes with **2464 cells**, with source and
fixtures unchanged; see `docs/diagnostics/rc2-2026-09-28/lisp-runtime-6.05-heap2464.txt`.
The runner verifies the upstream `lispif.c` blob and prints its actual resource
definitions, plus the selected host heap size. Auxiliary VM memory is 18 KiB and GC
stack160, following its `lispif.c`; the bundled header lacks the 18K alias, so
the equivalent public size macros are used. Host success is **not** on-target
memory/timing, ADC pin, CAN, motor-output or custom-firmware validation.
The earlier 16 KiB host fixture could exhaust memory on 6.05; that fixture
resource issue is not presented as a reproduced ESC defect.
