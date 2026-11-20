#pragma once

// CrossNext-side bring-up for the Metalio E-Ink4-Plus: logs an I2C bus scan and
// the TCA9555/TPS65185 state, and installs the power/volume key hook. Call once
// after the display driver has initialized the board (it owns the TCA9555).
namespace metalio_plus {
void begin();
}  // namespace metalio_plus
