#pragma once

#include "PanelDriver.h"

// Metalio E-Ink4-Plus: raw-parallel E0470A14-AF (native 1216x684) driven by
// EPDiy + TPS65185 through the MetalioPlusBoard library. Linked only when
// -DFREEINK_DRIVER_METALIO_PLUS=1 (derived from FREEINK_DEVICE_METALIO_PLUS).
// Adapted from RickyOS-Plus components/rickyos_display MetalioPlusDriver (MIT).

namespace freeink {
PanelDriver& metalioPlusDriver();
}  // namespace freeink
