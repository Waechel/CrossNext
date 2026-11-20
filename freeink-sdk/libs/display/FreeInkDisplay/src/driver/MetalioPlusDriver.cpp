// Metalio E-Ink4-Plus panel driver. Adapted from RickyOS-Plus
// components/rickyos_display/src/driver/MetalioPlusDriver.cpp and
// include/PlusFramebuffer.h (MIT). This first version keeps RickyOS's refresh
// policy (clean first frame from an unknown optical state, GC16 for full,
// DU for fast B/W, GL16 for the 4-level overlay) without its reader-turn and
// native 16-level paths.
#include <BoardConfig.h>

#if FREEINK_DRIVER_METALIO_PLUS

#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <esp_memory_utils.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include "MetalioPlusDriver.h"

extern "C" {
#include <epd_board_metalio_eink4_plus.h>
#include <epd_highlevel.h>
#include <epdiy.h>
#include <metalio_epd.h>
}

extern "C" uint32_t epd_lcd_get_first_fault(void);
// Read by the board diag task, which repeats it after the serial monitor attaches.
extern "C" char g_metalio_plus_selftest[96];
char g_metalio_plus_selftest[96] = "selftest not run";
extern "C" char g_metalio_plus_bench[192];
char g_metalio_plus_bench[192] = "bench not run";
extern "C" char g_metalio_plus_cores[160];
char g_metalio_plus_cores[160] = "cores not measured";
extern "C" char g_metalio_plus_intr[3072];
char g_metalio_plus_intr[3072] = "";

namespace {
// Sequential 32-bit reads; returns MB/s. volatile sink keeps the loop.
volatile uint32_t s_benchSink;
unsigned readMBps(const uint8_t* p, size_t bytes, int passes) {
  const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
  const size_t n = bytes / 4;
  uint32_t acc = 0;
  const int64_t t0 = esp_timer_get_time();
  for (int k = 0; k < passes; ++k)
    for (size_t i = 0; i < n; i += 4) acc += w[i] ^ w[i + 1] ^ w[i + 2] ^ w[i + 3];
  const int64_t us = esp_timer_get_time() - t0;
  s_benchSink = acc;
  return us > 0 ? static_cast<unsigned>((static_cast<int64_t>(bytes) * passes) / us) : 0;
}

// Measures the things the EPDiy line feed depends on, to compare with the
// stage-1 smoke app where the same driver keeps up.
void runBench(const uint8_t* fb, size_t fbBytes) {
  const uint32_t c0 = esp_cpu_get_cycle_count();
  const int64_t t0 = esp_timer_get_time();
  while (esp_timer_get_time() - t0 < 20000) {
  }
  const uint32_t cycles = esp_cpu_get_cycle_count() - c0;
  const int64_t us = esp_timer_get_time() - t0;
  const unsigned mhz = static_cast<unsigned>(cycles / us);

  unsigned intMB = 0;
  if (uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) {
    memset(buf, 0x5A, 16384);
    intMB = readMBps(buf, 16384, 64);
    heap_caps_free(buf);
  }
  const unsigned psMB = fb ? readMBps(fb, fbBytes, 4) : 0;

  unsigned flMB = 0;
  const esp_partition_t* part =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "app1");
  if (part) {
    const void* map = nullptr;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(part, 0, 1 << 20, ESP_PARTITION_MMAP_DATA, &map, &h) == ESP_OK) {
      flMB = readMBps(static_cast<const uint8_t*>(map), 1 << 20, 2);
      esp_partition_munmap(h);
    }
  }
  snprintf(g_metalio_plus_bench, sizeof(g_metalio_plus_bench),
           "bench cpu %u MHz | iram %u MB/s | psram %u MB/s | flash %u MB/s | fb %s | int free %u",
           mhz, intMB, psMB, flMB, fb ? (esp_ptr_external_ram(fb) ? "psram" : "internal") : "none",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  ESP_LOGE("PlusDisplay", "diag %s", g_metalio_plus_bench);
}
}  // namespace

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

// Bring-up bisect: right after panel init, before CrossNext's other tasks run,
// draw the stage-1 smoke pattern with the stage-1 call. Success here with
// failures later means runtime contention; failure here means build/link.
void bootSelfTest() {
  EpdiyHighlevelState* hl = MetalioEpd_Hl();
  if (!hl || !hl->front_fb) {
    snprintf(g_metalio_plus_selftest, sizeof(g_metalio_plus_selftest), "selftest skipped (no framebuffer)");
    return;
  }
  uint8_t* fb = epd_hl_get_framebuffer(hl);
  runBench(fb, static_cast<size_t>(epd_width()) * epd_height() / 2);
  epd_hl_set_all_white(hl);
  const int w = epd_width(), h = epd_height();
  for (int t = 0; t < 12; ++t) epd_draw_rect((EpdRect){t, t, w - 2 * t, h - 2 * t}, 0x00, fb);
  const int len = w / 2 - 60;
  for (int i = 0; i < 16; ++i) {
    const int x0 = 40 + i * len / 16, x1 = 40 + (i + 1) * len / 16;
    epd_fill_rect((EpdRect){x0, 40, x1 - x0, h - 80}, static_cast<uint8_t>(0xFF - i * 0x11), fb);
  }
  epd_fill_circle(w * 3 / 4, h / 2, h / 3, 0x00, fb);
  epd_fill_circle(w * 3 / 4, h / 2, h / 18, 0xFF, fb);
  epd_fill_rect((EpdRect){24, 24, 60, 60}, 0x00, fb);
  epd_poweron();
  const int64_t t0 = esp_timer_get_time();
  const EpdDrawError err = epd_hl_update_screen_full(hl, MODE_GC16, 25);
  const int64_t t1 = esp_timer_get_time();
  epd_poweroff();
  snprintf(g_metalio_plus_selftest, sizeof(g_metalio_plus_selftest),
           "selftest core %d result=0x%x ms=%lld fault=0x%08lx", xPortGetCoreID(), static_cast<unsigned>(err),
           static_cast<long long>((t1 - t0) / 1000), static_cast<unsigned long>(epd_lcd_get_first_fault()));
  ESP_LOGE(TAG, "diag %s", g_metalio_plus_selftest);
}

// epd_init() allocates the LCD peripheral interrupts on the calling core. The
// EOF handler refills bounce buffers in ISR context, and EPDiy pins render
// worker 1 to core 1. CrossNext calls display.begin() from Arduino's loopTask,
// pinned to core 1, so the ISR load landed on worker 1's core and every refresh
// underran at the same row (fault 0x01b50101: row 437, worker 1, not ready).
// The vendor app, RickyOS-Plus and the stage-1 smoke test all init from core 0.
struct InitOnCore0 {
  SemaphoreHandle_t done;
  bool ok;
};

// Internal-RAM read speed on a given core, to see whether panel init leaves
// something (an interrupt) eating that core. Smoke app: 422 MB/s.
unsigned iramOnce() {
  unsigned mb = 0;
  if (uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) {
    memset(buf, 0x5A, 16384);
    mb = readMBps(buf, 16384, 64);
    heap_caps_free(buf);
  }
  return mb;
}
struct CoreProbe {
  SemaphoreHandle_t done;
  unsigned mb;
};
void coreProbeTask(void* arg) {
  auto* p = static_cast<CoreProbe*>(arg);
  p->mb = iramOnce();
  xSemaphoreGive(p->done);
  vTaskDelete(nullptr);
}
unsigned iramOnCore(int core) {
  CoreProbe p = {xSemaphoreCreateBinary(), 0};
  if (!p.done) return 0;
  if (xTaskCreatePinnedToCore(coreProbeTask, "plus_probe", 4096, &p, configMAX_PRIORITIES - 2, nullptr, core) == pdPASS)
    xSemaphoreTake(p.done, pdMS_TO_TICKS(2000));
  vSemaphoreDelete(p.done);
  return p.mb;
}

void initOnCore0Task(void* arg) {
  auto* init = static_cast<InitOnCore0*>(arg);
  const unsigned pre0 = iramOnce(), pre1 = iramOnCore(1);
  init->ok = MetalioEpd_Init();
  const unsigned post0 = iramOnce(), post1 = iramOnCore(1);
  snprintf(g_metalio_plus_cores, sizeof(g_metalio_plus_cores),
           "cores iram MB/s: before panel init c0 %u c1 %u | after c0 %u c1 %u (smoke: 422)", pre0, pre1, post0, post1);
  ESP_LOGE(TAG, "diag %s", g_metalio_plus_cores);
  if (FILE* f = fmemopen(g_metalio_plus_intr, sizeof(g_metalio_plus_intr) - 1, "w")) {
    esp_intr_dump(f);
    fclose(f);
  }
  if (init->ok) bootSelfTest();
  xSemaphoreGive(init->done);
  vTaskDelete(nullptr);
}

bool initPanelOnCore0() {
  InitOnCore0 init = {xSemaphoreCreateBinary(), false};
  if (!init.done) return MetalioEpd_Init();
  if (xTaskCreatePinnedToCore(initOnCore0Task, "plus_epd_init", 8192, &init, configMAX_PRIORITIES - 2, nullptr, 0) !=
      pdPASS) {
    vSemaphoreDelete(init.done);
    ESP_LOGE(TAG, "could not start init task on core 0; initializing on core %d", xPortGetCoreID());
    return MetalioEpd_Init();
  }
  xSemaphoreTake(init.done, portMAX_DELAY);
  vSemaphoreDelete(init.done);
  return init.ok;
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
    ready_ = initPanelOnCore0();
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
      if (clean || full || waveform == MODE_GC16) {
        // No epd_clear_checked() here: its short push_pixels frames underran the
        // line feed on hardware (fault row 420, worker 0) and a failed clear
        // aborted the draw, leaving the panel blank. A full GC16 drives every
        // pixel from the assumed state; it is what the stage-1 smoke test used.
        result = epd_hl_update_screen_full(hl, waveform, kTemperature);
      } else {
        result = epd_hl_update_screen(hl, waveform, kTemperature);
      }
    }
    epd_poweroff();  // TPS rails are only needed while the waveform runs.
    ESP_LOGE(TAG, "diag refresh mode=%d full=%d clean=%d result=0x%x ms=%lld", static_cast<int>(waveform), full, clean,
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
