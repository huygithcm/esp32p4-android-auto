#!/usr/bin/env python3
"""Use the documented Linux parent-PID method in Component Manager.

Managed Linux exposes a different /proc PID namespace, so psutil cannot
inspect its own process. Only dependency-manager temporary filenames use
this PID; firmware sources and compilation flags are unaffected.
"""
import importlib.util
from pathlib import Path

spec = importlib.util.find_spec(
    "idf_component_manager.prepare_components.cmake_pid"
)
if spec is None or spec.origin is None:
    raise SystemExit("Component Manager cmake_pid module not found")
path = Path(spec.origin)
source = path.read_text()
marker = "    current = psutil.Process()"
replacement = (
    "    # Linux CMake invokes this script directly; parent PID is stable.\n"
    "    if os.name == 'posix':\n"
    "        return os.getppid()\n\n"
    + marker
)
if replacement in source:
    print("Linux parent-PID compatibility already installed")
elif marker in source:
    path.write_text(source.replace(marker, replacement, 1))
    print("Installed Linux parent-PID compatibility")
else:
    raise SystemExit("Unsupported Component Manager implementation")
