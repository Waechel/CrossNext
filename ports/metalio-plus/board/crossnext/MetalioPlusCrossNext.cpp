// CrossNext glue for the Metalio E-Ink4-Plus board layer.
//
// Keys (from the vendor board config.h, V1.5): BOOT/AI on GPIO61 is CrossNext's
// CONFIRM pin (BoardConfig input.confirm). Power (TCA9555 P1.7) and volume
// up/down (P1.0/P1.1) are pulled up and read LOW when pressed; they are merged
// into InputManager through its board button hook.
#include "MetalioPlusCrossNext.h"

#include <Arduino.h>
#include <InputManager.h>
#include <Logging.h>
#include <esp_log.h>

extern "C" {
#include <config.h>
#include <epd_board_metalio_eink4_plus.h>
#include <plus_i2c_wire.h>
extern char g_metalio_plus_selftest[96];
extern char g_metalio_plus_bench[192];
}

namespace metalio_plus {
namespace {

constexpr uint32_t kKeyPollMs = 15;  // the hook runs every input update; cap the I2C reads
uint8_t s_lastMask = 0;
uint32_t s_lastPollMs = 0;
bool s_keysReady = false;
bool s_earlyOk = false;

uint8_t keyHook() {
  if (!s_keysReady) return 0;
  const uint32_t now = millis();
  if (s_lastPollMs != 0 && now - s_lastPollMs < kKeyPollMs) return s_lastMask;
  s_lastPollMs = now;
  uint8_t port1 = 0xFF;
  if (!epd_board_metalio_eink4_plus_ioexp_read_port(1, &port1)) return s_lastMask;
  uint8_t mask = 0;
  // Port 1 bit n = TCA P1.n = board IO 8+n. Pressed = 0.
  if (!(port1 & (1u << (EPD_IO_VOL_UP - 8)))) mask |= 1u << InputManager::BTN_UP;
  if (!(port1 & (1u << (EPD_IO_VOL_DOWN - 8)))) mask |= 1u << InputManager::BTN_DOWN;
  // Power acts as BACK during bring-up: there is no Plus wake path yet, and
  // BTN_POWER would request sleep (which restarts the Plus for now).
  if (!(port1 & (1u << (EPD_IO_POWER - 8)))) mask |= 1u << InputManager::BTN_BACK;
  if (mask != s_lastMask) ESP_LOGE("PLUS", "diag keys port1=0x%02X mask=0x%02X", port1, mask);
  s_lastMask = mask;
  return mask;
}

char s_scanSummary[160] = "not scanned";

void scanBus() {
  char found[128] = {0};
  size_t used = 0;
  int count = 0;
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    if (plus_i2c_probe(addr) == ESP_OK) {
      used += snprintf(found + used, sizeof(found) - used, " 0x%02X", addr);
      if (used >= sizeof(found)) used = sizeof(found) - 1;
      ++count;
    }
  }
  // Expected on V1.5: 0x19 SC7A20H, 0x20 TCA9555, 0x38 FT6336U (may sleep),
  // 0x51 PCF8563, 0x55 BQ27220, 0x68 TPS65185.
  snprintf(s_scanSummary, sizeof(s_scanSummary), "I2C bus SDA0/SCL1: %d device(s):%s", count,
           count ? found : " none");
  ESP_LOGE("PLUS", "diag %s", s_scanSummary);
}

// The serial monitor usually attaches after boot; repeat the bring-up summary
// for the first two minutes so it is always captured. 4 KB stack, then exits.
void diagTask(void*) {
  for (int i = 0; i < 10; ++i) {
    vTaskDelay(pdMS_TO_TICKS(i == 0 ? 8000 : 15000));
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_selftest);
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_bench);
    ESP_LOGE("PLUS", "diag: early bus %s | %s | TCA9555 %s | TPS65185 %s | keys mask 0x%02X | CPU %u MHz | heap %u, PSRAM %u",
            s_earlyOk ? "ok" : "FAILED", s_scanSummary,
            s_keysReady ? "ok" : "MISSING", epd_board_metalio_eink4_plus_tps_ok() ? "ok" : "not ready", s_lastMask,
            static_cast<unsigned>(getCpuFrequencyMhz()), static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getFreePsram()));
  }
  vTaskDelete(nullptr);
}

}  // namespace

bool earlyBegin() {
  const bool ok = epd_board_metalio_eink4_plus_early_bus_init();
  // Serial may not be attached yet; diagTask repeats the outcome later.
  s_earlyOk = ok;
  return ok;
}

void begin() {
  if (plus_i2c_begin() != ESP_OK) {
    ESP_LOGE("PLUS", "I2C bus init failed");
    return;
  }
  scanBus();
  s_keysReady = epd_board_metalio_eink4_plus_ioexp_ok();
  ESP_LOGE("PLUS", "TCA9555 %s, TPS65185 %s, CPU %u MHz", s_keysReady ? "ok" : "MISSING",
          epd_board_metalio_eink4_plus_tps_ok() ? "ok" : "not ready", static_cast<unsigned>(getCpuFrequencyMhz()));
  InputManager::setButtonHook(keyHook);
  xTaskCreate(diagTask, "plus_diag", 4096, nullptr, 1, nullptr);
}

}  // namespace metalio_plus
