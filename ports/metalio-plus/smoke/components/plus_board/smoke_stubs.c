// Stage-1 smoke test has no SD support; the board code only releases SD pin holds.
#include "metalio_sd.h"

#include <driver/gpio.h>

void MetalioSd_ReleaseHold(void) {
  // SDMMC slot 0 pins (CLK/CMD/D0..D3) per config.h: release any deep-sleep holds.
  static const gpio_num_t pins[] = {GPIO_NUM_24, GPIO_NUM_25, GPIO_NUM_20,
                                    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23};
  for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) gpio_hold_dis(pins[i]);
}
