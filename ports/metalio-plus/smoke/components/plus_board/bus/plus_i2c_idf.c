// Plain ESP-IDF replacement for RickyOS's Arduino Wire1 wrapper (plus_i2c_wire.cc).
// Same bus as the vendor board: I2C port 1, SDA GPIO0, SCL GPIO1, 100 kHz, 100 ms
// timeout. Used only by the stage-1 smoke test, which has no Arduino core.
#include "plus_i2c_wire.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define PLUS_I2C_PORT 1
#define PLUS_I2C_SDA 0
#define PLUS_I2C_SCL 1
#define PLUS_I2C_HZ 100000
#define PLUS_I2C_TIMEOUT_MS 100
#define PLUS_I2C_MAX_DEVICES 8

static const char* TAG = "plus_i2c";
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_lock;
static struct {
  uint8_t address;
  i2c_master_dev_handle_t handle;
} s_devices[PLUS_I2C_MAX_DEVICES];

bool plus_i2c_initialized(void) { return s_bus != NULL; }

esp_err_t plus_i2c_begin(void) {
  if (s_bus) return ESP_OK;
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  if (!s_lock) return ESP_ERR_NO_MEM;
  i2c_master_bus_config_t cfg = {
      .i2c_port = PLUS_I2C_PORT,
      .sda_io_num = PLUS_I2C_SDA,
      .scl_io_num = PLUS_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(err));
    s_bus = NULL;
  }
  return err;
}

static i2c_master_dev_handle_t device(uint8_t address) {
  for (int i = 0; i < PLUS_I2C_MAX_DEVICES; ++i) {
    if (s_devices[i].handle && s_devices[i].address == address) return s_devices[i].handle;
  }
  for (int i = 0; i < PLUS_I2C_MAX_DEVICES; ++i) {
    if (!s_devices[i].handle) {
      i2c_device_config_t dev = {
          .dev_addr_length = I2C_ADDR_BIT_LEN_7,
          .device_address = address,
          .scl_speed_hz = PLUS_I2C_HZ,
      };
      if (i2c_master_bus_add_device(s_bus, &dev, &s_devices[i].handle) != ESP_OK) return NULL;
      s_devices[i].address = address;
      return s_devices[i].handle;
    }
  }
  return NULL;
}

esp_err_t plus_i2c_probe(uint8_t address) {
  if (!s_bus) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t err = i2c_master_probe(s_bus, address, PLUS_I2C_TIMEOUT_MS);
  xSemaphoreGive(s_lock);
  return err;
}

esp_err_t plus_i2c_read(uint8_t address, uint8_t reg, uint8_t* data, size_t size) {
  if (!s_bus) return ESP_ERR_INVALID_STATE;
  if (size == 0) return ESP_OK;
  if (!data || size > 128) return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  i2c_master_dev_handle_t dev = device(address);
  esp_err_t err = dev ? i2c_master_transmit_receive(dev, &reg, 1, data, size, PLUS_I2C_TIMEOUT_MS)
                      : ESP_ERR_NO_MEM;
  xSemaphoreGive(s_lock);
  return err;
}

esp_err_t plus_i2c_write(uint8_t address, uint8_t reg, const uint8_t* data, size_t size) {
  if (!s_bus) return ESP_ERR_INVALID_STATE;
  if (size > 128 || (size && !data)) return ESP_ERR_INVALID_ARG;
  uint8_t buf[129];
  buf[0] = reg;
  if (size) memcpy(buf + 1, data, size);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  i2c_master_dev_handle_t dev = device(address);
  esp_err_t err = dev ? i2c_master_transmit(dev, buf, size + 1, PLUS_I2C_TIMEOUT_MS) : ESP_ERR_NO_MEM;
  xSemaphoreGive(s_lock);
  return err;
}
