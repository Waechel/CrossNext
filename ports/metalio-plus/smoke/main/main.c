// CrossNext Metalio E-Ink4-Plus port, stage 1 smoke test.
//
// Checks, on real hardware: the cloud-built image boots under the device's
// IDF release/v6.1 9a97f6c5 bootloader from app1, PSRAM comes up, the TPS65185
// rails power the panel, and EPDiy can drive a full GC16 refresh.
//
// Expected screen (portrait, cover keys at the bottom):
//   - black frame around the edge
//   - 16 grey bars, white -> black, from top to bottom
//   - a large black circle in the lower half with a white dot in the middle
//   - a black square in the top-left corner (orientation marker)
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_board_metalio_eink4_plus.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "metalio_epd.h"

static const char* TAG = "plus_smoke";

static void draw_test_pattern(uint8_t* fb) {
  // epd_set_rotation(EPD_ROT_LANDSCAPE) in MetalioEpd_Init: coordinates are native
  // 1216 x 684. On the device, native x runs along the long (portrait vertical) edge.
  const int w = epd_width();
  const int h = epd_height();
  ESP_LOGI(TAG, "drawing on %dx%d", w, h);

  // Frame.
  for (int t = 0; t < 12; ++t) {
    epd_draw_rect((EpdRect){t, t, w - 2 * t, h - 2 * t}, 0x00, fb);
  }

  // 16 grey bars across the first half of the long axis.
  const int bars_start = 40, bars_len = w / 2 - 60;
  for (int i = 0; i < 16; ++i) {
    const int x0 = bars_start + i * bars_len / 16;
    const int x1 = bars_start + (i + 1) * bars_len / 16;
    epd_fill_rect((EpdRect){x0, 40, x1 - x0, h - 80}, (uint8_t)(0xFF - i * 0x11), fb);
  }

  // Circle in the second half.
  const int cx = w * 3 / 4, cy = h / 2, r = h / 3;
  epd_fill_circle(cx, cy, r, 0x00, fb);
  epd_fill_circle(cx, cy, r / 6, 0xFF, fb);

  // Orientation marker: native (0,0) corner.
  epd_fill_rect((EpdRect){24, 24, 60, 60}, 0x00, fb);
}

void app_main(void) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* app = esp_app_get_description();
  ESP_LOGI(TAG, "CrossNext Plus smoke test %s (IDF %s) running from %s @0x%lx",
           app->version, app->idf_ver, running ? running->label : "?",
           running ? (unsigned long)running->address : 0UL);
  ESP_LOGI(TAG, "PSRAM free %u KB, internal free %u KB",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  if (!MetalioEpd_Init()) {
    ESP_LOGE(TAG, "panel power (TPS65185) not ready; nothing drawn");
  } else {
    EpdiyHighlevelState* hl = MetalioEpd_Hl();
    uint8_t* fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    draw_test_pattern(fb);

    epd_poweron();
    const int64_t t0 = esp_timer_get_time();
    enum EpdDrawError err = epd_hl_update_screen_full(hl, MODE_GC16, 25);
    const int64_t t1 = esp_timer_get_time();
    epd_poweroff();
    ESP_LOGI(TAG, "full GC16 refresh result=%d in %lld ms", (int)err, (long long)((t1 - t0) / 1000));
  }

  // Stay alive and visible on the USB console.
  for (int n = 0;; ++n) {
    ESP_LOGI(TAG, "alive %d s, tps_ok=%d", n * 5, (int)epd_board_metalio_eink4_plus_tps_ok());
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
