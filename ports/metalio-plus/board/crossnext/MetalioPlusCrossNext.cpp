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
extern char g_metalio_plus_cores[160];
extern char g_metalio_plus_shared[192];
extern char g_metalio_plus_intr[3072];
}

namespace metalio_plus {
namespace {

constexpr uint32_t kKeyPollMs = 15;  // the hook runs every input update; cap the I2C reads
uint8_t s_lastMask = 0;
uint32_t s_lastPollMs = 0;
bool s_keysReady = false;
bool s_earlyOk = false;

// Cover keys under the panel are FT6336U touch points reported off-panel
// (raw Y ~1600). InputManager's Plus touch path calls metalio_plus_touch_key().
volatile uint8_t s_touchKeyMask = 0;
volatile int s_lastTouchKeyX = -1;

uint8_t keyHook() {
  if (!s_keysReady) return s_touchKeyMask;
  const uint32_t now = millis();
  if (s_lastPollMs != 0 && now - s_lastPollMs < kKeyPollMs) return s_lastMask | s_touchKeyMask;
  s_lastPollMs = now;
  uint8_t port1 = 0xFF;
  if (!epd_board_metalio_eink4_plus_ioexp_read_port(1, &port1)) return s_lastMask | s_touchKeyMask;
  uint8_t mask = 0;
  // Port 1 bit n = TCA P1.n = board IO 8+n. Pressed = 0.
  // CrossNext moves list selection with LEFT/RIGHT (the X4 front pair); UP/DOWN
  // are the configurable side keys (page jumps), so volume maps to LEFT/RIGHT.
  if (!(port1 & (1u << (EPD_IO_VOL_UP - 8)))) mask |= 1u << InputManager::BTN_LEFT;
  if (!(port1 & (1u << (EPD_IO_VOL_DOWN - 8)))) mask |= 1u << InputManager::BTN_RIGHT;
  // Power selects. BTN_POWER would request sleep, which only restarts the Plus
  // until a wake path exists.
  if (!(port1 & (1u << (EPD_IO_POWER - 8)))) mask |= 1u << InputManager::BTN_CONFIRM;
  if (mask != s_lastMask) ESP_LOGE("PLUS", "diag keys port1=0x%02X mask=0x%02X", port1, mask);
  s_lastMask = mask;
  return mask | s_touchKeyMask;
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
  for (int i = 0; i < 40; ++i) {
    vTaskDelay(pdMS_TO_TICKS(i == 0 ? 8000 : 15000));
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_selftest);
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_bench);
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_cores);
    ESP_LOGE("PLUS", "diag %s", g_metalio_plus_shared);
    if (i == 0 || i == 3) ESP_LOGE("PLUS", "diag interrupts:\n%s", g_metalio_plus_intr);
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

// --- Touch glue for InputManager (FREEINK_DEVICE_METALIO_PLUS) ---------------
extern "C" bool metalio_plus_touch_begin(void) {
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  epd_board_metalio_eink4_plus_tp_reset();
  uint8_t chip = 0, fw = 0;
  const bool ok = plus_i2c_read(TOUCH_I2C_ADDR, 0xA3, &chip, 1) == ESP_OK;
  plus_i2c_read(TOUCH_I2C_ADDR, 0xA6, &fw, 1);
  if (ok) {
    const uint8_t intMode = 0x00;  // INT held low while touched
    plus_i2c_write(TOUCH_I2C_ADDR, 0xA4, &intMode, 1);
  }
  ESP_LOGE("PLUS", "diag touch FT6336U %s chip=0x%02X fw=0x%02X", ok ? "ok" : "NOT FOUND", chip, fw);
  return ok;
}

extern "C" bool metalio_plus_touch_read(uint8_t reg, uint8_t* buf, size_t len) {
  return plus_i2c_read(TOUCH_I2C_ADDR, reg, buf, len) == ESP_OK;
}

extern "C" bool metalio_plus_touch_irq_active(void) { return digitalRead(TOUCH_INT_PIN) == LOW; }

// rawX < 0 = released. Vendor calibration: home ~100, back/prev ~200, next ~300.
extern "C" void metalio_plus_touch_key(int rawX, bool down) {
  using metalio_plus::s_touchKeyMask;
  uint8_t mask = 0;
  if (down && rawX >= 0) {
    if (rawX < 165) mask = 1u << InputManager::BTN_BACK;        // orange dot: back/home
    else if (rawX < 255) mask = 1u << InputManager::BTN_LEFT;   // left pill: previous
    else mask = 1u << InputManager::BTN_RIGHT;                  // right pill: next
  }
  if (mask != s_touchKeyMask) ESP_LOGE("PLUS", "diag cover key x=%d mask=0x%02X", rawX, mask);
  metalio_plus::s_lastTouchKeyX = rawX;
  s_touchKeyMask = mask;
}

extern "C" bool metalio_plus_touch_key_active(void) { return metalio_plus::s_touchKeyMask != 0; }
