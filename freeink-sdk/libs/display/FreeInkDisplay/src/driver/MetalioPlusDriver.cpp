// Metalio E-Ink4-Plus panel driver. Adapted from RickyOS-Plus
// components/rickyos_display/src/driver/MetalioPlusDriver.cpp and
// include/PlusFramebuffer.h (MIT). This first version keeps RickyOS's refresh
// policy (clean first frame from an unknown optical state, GC16 for full,
// DU for fast B/W, GL16 for the 4-level overlay) without its reader-turn and
// native 16-level paths.
#include <BoardConfig.h>

#if FREEINK_DRIVER_METALIO_PLUS

#include <esp_log.h>
#include <esp_timer.h>

#include <cstring>

#include "MetalioPlusDriver.h"

extern "C" {
#include <epd_board_metalio_eink4_plus.h>
#include <epd_highlevel.h>
#include <epdiy.h>
#include <metalio_epd.h>
}

namespace freeink {
namespace {

constexpr const char* TAG = "PlusDisplay";
constexpr uint16_t kWidth = 1216;
constexpr uint16_t kHeight = 684;
constexpr uint16_t kWidthBytes = kWidth / 8;
constexpr uint32_t kBufferSize = static_cast<uint32_t>(kWidthBytes) * kHeight;  // 103,968
constexpr int kTemperature = 25;

// FreeInk frames are native landscape, MSB-first 1 bpp, white = 1. EPDiy wants
// even pixels in the low nibble, white = 15. `dst` holds 4x `srcBytes` bytes.
void expandMonochrome(const uint8_t* src, size_t srcBytes, uint8_t* dst) {
  for (size_t i = 0; i < srcBytes; ++i) {
    const uint8_t v = src[i];
    for (unsigned pair = 0; pair < 4; ++pair) {
      const uint8_t low = (v & (0x80u >> (pair * 2))) ? 0x0f : 0x00;
      const uint8_t high = (v & (0x40u >> (pair * 2))) ? 0xf0 : 0x00;
      dst[i * 4 + pair] = low | high;
    }
  }
}

// Overlay selectors on top of a committed B/W base: (LSB, MSB) = 11 dark gray,
// 01 light gray, 00 keep the base. Staged in the EPDiy front buffer.
void stageOverlayLsb(const uint8_t* base, const uint8_t* lsb, size_t srcBytes, uint8_t* frame) {
  for (size_t i = 0; i < srcBytes; ++i) {
    for (unsigned pair = 0; pair < 4; ++pair) {
      uint8_t value = base[i * 4 + pair];
      if (lsb[i] & (0x80u >> (pair * 2))) value = (value & 0xf0) | 5;
      if (lsb[i] & (0x40u >> (pair * 2))) value = (value & 0x0f) | 0x50;
      frame[i * 4 + pair] = value;
    }
  }
}

void finishOverlayMsb(const uint8_t* base, const uint8_t* msb, size_t srcBytes, uint8_t* frame) {
  for (size_t i = 0; i < srcBytes; ++i) {
    for (unsigned pair = 0; pair < 4; ++pair) {
      const uint8_t staged = frame[i * 4 + pair];
      const uint8_t original = base[i * 4 + pair];
      const uint8_t low =
          (msb[i] & (0x80u >> (pair * 2))) ? ((staged & 15) == 5 ? 5 : 10) : (original & 15);
      const uint8_t high =
          (msb[i] & (0x40u >> (pair * 2))) ? ((staged >> 4) == 5 ? 5 : 10) : (original >> 4);
      frame[i * 4 + pair] = static_cast<uint8_t>(low | (high << 4));
    }
  }
}

enum class Optical : uint8_t { Unknown, Bw, Gray };

class MetalioPlusDriver final : public PanelDriver {
 public:
  uint32_t spiHz() const override { return 0; }
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveHigh; }
  PanelGeometry geometry() const override { return {kWidth, kHeight, kWidthBytes, kBufferSize}; }
  bool usesExternalBus() const override { return true; }

  void begin(EpdBus&) override {
    overlayStage_ = 0;
    ready_ = MetalioEpd_Init();
    state_ = Optical::Unknown;
    if (!ready_) ESP_LOGE(TAG, "panel initialization failed (TPS65185 not ready)");
  }

  void deepSleep(EpdBus&) override {
    overlayStage_ = 0;
    if (ready_) epd_poweroff();
  }

  void setBackgroundHint(bool darkBackground) override { darkBackground_ = darkBackground; }

  void display(EpdBus&, const uint8_t* fb, const uint8_t*, RefreshMode mode, bool) override {
    overlayStage_ = 0;
    if (!ready_ || !fb) return;
    EpdiyHighlevelState* hl = MetalioEpd_Hl();
    if (!hl || !hl->front_fb) return;
    expandMonochrome(fb, kBufferSize, hl->front_fb);
    // Night mode is B/W only, so its HALF refresh must clean here or it is DU.
    const bool full = mode == RefreshMode::Full || (darkBackground_ && mode == RefreshMode::Half);
    EpdDrawMode waveform = MODE_DU;
    if (full || mode == RefreshMode::Half) waveform = full ? MODE_GC16 : MODE_GL16;
    const EpdDrawError result = refresh(hl, waveform, full);
    state_ = result == EPD_DRAW_SUCCESS ? Optical::Bw : Optical::Unknown;
    if (result != EPD_DRAW_SUCCESS) ESP_LOGE(TAG, "refresh failed: 0x%x", static_cast<unsigned>(result));
  }

  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const override {
    if (mode != GrayscaleMode::Overlay) return {};
    return {GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, false, false, false};
  }

  void copyGrayscaleLsb(EpdBus&, const uint8_t* lsb) override {
    overlayStage_ = 0;
    EpdiyHighlevelState* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (!lsb || !hl || !hl->front_fb || !hl->back_fb || state_ != Optical::Bw) return;
    stageOverlayLsb(hl->back_fb, lsb, kBufferSize, hl->front_fb);
    overlayStage_ = 1;
  }

  void copyGrayscaleMsb(EpdBus&, const uint8_t* msb) override {
    EpdiyHighlevelState* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (overlayStage_ != 1 || !msb || !hl || !hl->front_fb || !hl->back_fb || state_ != Optical::Bw) {
      overlayStage_ = 0;
      return;
    }
    finishOverlayMsb(hl->back_fb, msb, kBufferSize, hl->front_fb);
    overlayStage_ = 2;
  }

  void displayGray(EpdBus&, const uint8_t*, bool, const unsigned char* lut, bool factoryMode) override {
    // fb is the final MSB selector, not the page: never fall back to display(fb).
    if (overlayStage_ != 2 || lut || state_ != Optical::Bw) {
      overlayStage_ = 0;
      return;
    }
    overlayStage_ = 0;
    const EpdDrawError result = refresh(MetalioEpd_Hl(), factoryMode ? MODE_GC16 : MODE_GL16, factoryMode);
    state_ = result == EPD_DRAW_SUCCESS ? Optical::Gray : Optical::Unknown;
    if (result != EPD_DRAW_SUCCESS) ESP_LOGE(TAG, "overlay refresh failed: 0x%x", static_cast<unsigned>(result));
  }

  void cleanupGrayscaleBuffers(EpdBus&, const uint8_t*) override { overlayStage_ = 0; }

 private:
  EpdDrawError refresh(EpdiyHighlevelState* hl, EpdDrawMode waveform, bool full) {
    // A panel that may still show another firmware's image, or a frame after a
    // gray page, needs a clean optical baseline rather than a RAM diff.
    const bool clean = state_ == Optical::Unknown;
    if (clean || full || (state_ == Optical::Gray && waveform == MODE_DU)) waveform = MODE_GC16;
    const int64_t started = esp_timer_get_time();
    epd_poweron();
    EpdDrawError result = EPD_DRAW_EMPTY_LINE_QUEUE;
    if (epd_board_metalio_eink4_plus_tps_ok()) {
      if (clean) {
        result = epd_clear_checked();
        if (result == EPD_DRAW_SUCCESS) result = epd_hl_update_screen_from_white(hl, waveform, kTemperature);
      } else if (full || waveform == MODE_GC16) {
        result = epd_hl_update_screen_full(hl, waveform, kTemperature);
      } else {
        result = epd_hl_update_screen(hl, waveform, kTemperature);
      }
    }
    epd_poweroff();  // TPS rails are only needed while the waveform runs.
    ESP_LOGI(TAG, "refresh mode=%d full=%d clean=%d result=0x%x ms=%lld", static_cast<int>(waveform), full, clean,
             static_cast<unsigned>(result), static_cast<long long>((esp_timer_get_time() - started) / 1000));
    return result;
  }

  uint8_t overlayStage_ = 0;  // 0 none, 1 LSB staged, 2 complete target
  bool ready_ = false;
  bool darkBackground_ = false;
  Optical state_ = Optical::Unknown;
};

}  // namespace

PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}

}  // namespace freeink

#endif  // FREEINK_DRIVER_METALIO_PLUS
