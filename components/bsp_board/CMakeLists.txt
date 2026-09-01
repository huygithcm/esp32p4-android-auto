# Placeholder for the per-board support package. See README.md in this
# directory for the contract the firmware expects, and reference/bsp_p4/ for
# the ESP32-P4 implementation it was extracted from.
#
# No sources yet: main/ will fail to compile at `#include "bsp/esp-bsp.h"`
# until a real implementation lands here. That is the intended state of this
# branch — see PORT_README.md.
idf_component_register(INCLUDE_DIRS "include")
