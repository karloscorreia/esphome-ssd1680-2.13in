#include "ssd1680_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "driver/gpio.h"
#include <cstring>

namespace esphome {
namespace ssd1680_epaper {

static const char *const TAG = "ssd1680_epaper";

// Ajustado para display 2.13" 122x250
static const uint16_t WIDTH = 122;
static const uint16_t HEIGHT = 250;
static const uint16_t WIDTH_BYTES = (WIDTH + 7) / 8;   // 16 bytes
static const uint32_t ALLSCREEN_BYTES = WIDTH_BYTES * HEIGHT;

void SSD1680EPaper::setup() {
  ESP_LOGI(TAG, "=== SSD1680 SETUP - 122x250 ===");

  // Mantido do seu código original para CrowPanel/alimentação do display
  gpio_config_t pwr_conf = {};
  pwr_conf.pin_bit_mask = (1ULL << 7);
  pwr_conf.mode = GPIO_MODE_OUTPUT;
  pwr_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  pwr_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&pwr_conf);
  gpio_set_level(GPIO_NUM_7, 1);
  ESP_LOGI(TAG, "GPIO7 (display power) set HIGH");
  delay(100);

  this->dc_pin_->setup();
  this->dc_pin_->digital_write(false);

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }

  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
  }

  this->spi_setup();

  this->init_internal_(ALLSCREEN_BYTES);
  memset(this->buffer_, 0xFF, ALLSCREEN_BYTES);

  this->initialized_ = false;
  ESP_LOGI(TAG, "Setup complete, display init deferred");
}

void SSD1680EPaper::dump_config() {
  LOG_DISPLAY("", "SSD1680 E-Paper", this);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  if (this->busy_pin_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Current BUSY state: %s",
                  this->busy_pin_->digital_read() ? "HIGH (busy)" : "LOW (idle)");
  }
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", WIDTH, HEIGHT);
  LOG_UPDATE_INTERVAL(this);
}

void SSD1680EPaper::hw_reset_() {
  if (this->reset_pin_ == nullptr) {
    ESP_LOGW(TAG, "No reset pin configured!");
    return;
  }

  ESP_LOGD(TAG, "Hardware reset...");
  this->reset_pin_->digital_write(true);
  delay(10);
  this->reset_pin_->digital_write(false);
  delay(10);
  this->reset_pin_->digital_write(true);
  delay(10);
}

void SSD1680EPaper::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    ESP_LOGD(TAG, "No busy pin, using fixed delay");
    delay(100);
    return;
  }

  uint32_t start = millis();
  bool initial = this->busy_pin_->digital_read();
  ESP_LOGD(TAG, "Waiting for idle, initial busy pin state: %d (HIGH=busy)", initial);

  while (this->busy_pin_->digital_read()) {
    if (millis() - start > 10000) {
      ESP_LOGE(TAG, "Timeout waiting for display (busy pin stuck HIGH)");
      return;
    }
    delay(10);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "Display idle after %lu ms", millis() - start);
  delay(10);
}

void SSD1680EPaper::command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void SSD1680EPaper::data_(uint8_t data) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(data);
  this->disable();
}

void SSD1680EPaper::send_data_(const uint8_t *data, size_t len) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_array(data, len);
  this->disable();
}

void SSD1680EPaper::init_display_() {
  ESP_LOGI(TAG, ">>> INIT DISPLAY START <<<");

  if (this->busy_pin_ != nullptr) {
    ESP_LOGI(TAG, "BUSY before reset: %d", this->busy_pin_->digital_read());
  }

  if (this->reset_pin_ != nullptr) {
    ESP_LOGI(TAG, "Setting RESET HIGH...");
    this->reset_pin_->digital_write(true);
    delay(10);

    if (this->busy_pin_ != nullptr) {
      ESP_LOGI(TAG, "  BUSY state: %d", this->busy_pin_->digital_read());
    }

    ESP_LOGI(TAG, "Setting RESET LOW (active reset)...");
    this->reset_pin_->digital_write(false);
    delay(10);

    if (this->busy_pin_ != nullptr) {
      ESP_LOGI(TAG, "  BUSY state: %d", this->busy_pin_->digital_read());
    }

    ESP_LOGI(TAG, "Setting RESET HIGH (release)...");
    this->reset_pin_->digital_write(true);
    delay(10);

    if (this->busy_pin_ != nullptr) {
      ESP_LOGI(TAG, "  BUSY state: %d", this->busy_pin_->digital_read());
    }
  }

  delay(100);

  if (this->busy_pin_ != nullptr) {
    ESP_LOGI(TAG, "BUSY after 100ms post-reset delay: %d", this->busy_pin_->digital_read());
  }

  // Software reset
  ESP_LOGD(TAG, "Sending SW reset (0x12)");
  this->command_(0x12);
  delay(20);

  if (this->busy_pin_ != nullptr) {
    ESP_LOGI(TAG, "BUSY after SW reset: %d", this->busy_pin_->digital_read());
  }

  uint32_t start = millis();
  while (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()) {
    if (millis() - start > 2000) {
      ESP_LOGE(TAG, "SW Reset timeout after 2s - continuing anyway");
      break;
    }
    delay(10);
    App.feed_wdt();
  }

  // Driver output control: HEIGHT - 1 = 249 = 0x00F9
  ESP_LOGD(TAG, "Setting driver output (0x01)");
  this->command_(0x01);
  this->data_(0xF9);  // 249 LSB
  this->data_(0x00);  // 249 MSB
  this->data_(0x00);

  // Data entry mode: X+, Y+
  ESP_LOGD(TAG, "Setting data entry mode (0x11)");
  this->command_(0x11);
  this->data_(0x03);

  // RAM X address in BYTES: 0 .. 15  (16 bytes = 122 pixels rounded up)
  ESP_LOGD(TAG, "Setting RAM X (0x44)");
  this->command_(0x44);
  this->data_(0x00);
  this->data_(WIDTH_BYTES - 1);   // 0x0F

  // RAM Y address in PIXELS: 0 .. 249
  ESP_LOGD(TAG, "Setting RAM Y (0x45)");
  this->command_(0x45);
  this->data_(0x00);  // Y start LSB
  this->data_(0x00);  // Y start MSB
  this->data_(0xF9);  // Y end LSB
  this->data_(0x00);  // Y end MSB

  // Border waveform
  ESP_LOGD(TAG, "Setting border (0x3C)");
  this->command_(0x3C);
  this->data_(0x05);

  // Temperature sensor
  ESP_LOGD(TAG, "Setting temp sensor (0x18)");
  this->command_(0x18);
  this->data_(0x80);

  // RAM counters
  this->command_(0x4E);
  this->data_(0x00);

  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);

  if (this->busy_pin_ != nullptr) {
    ESP_LOGI(TAG, "BUSY after all init commands: %d", this->busy_pin_->digital_read());
  }

  ESP_LOGI(TAG, ">>> INIT DISPLAY COMPLETE <<<");
}

void SSD1680EPaper::full_update_() {
  ESP_LOGD(TAG, "Full refresh with 0xF7");

  this->command_(0x22);
  this->data_(0xF7);
  this->command_(0x20);

  uint32_t start = millis();
  while (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()) {
    if (millis() - start > 5000) {
      ESP_LOGD(TAG, "Update timeout (normal for this display) - took %lu ms", millis() - start);
      break;
    }
    delay(100);
    App.feed_wdt();
  }
}

void SSD1680EPaper::display_frame_() {
  ESP_LOGD(TAG, "Writing frame buffer: %u bytes", ALLSCREEN_BYTES);

  // Set RAM pointer to origin
  this->command_(0x4E);
  this->data_(0x00);

  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);

  // Write BW RAM
  this->command_(0x24);
  this->send_data_(this->buffer_, ALLSCREEN_BYTES);

  this->full_update_();
}

void SSD1680EPaper::update() {
  if (!this->initialized_) {
    this->init_display_();
    this->initialized_ = true;
  }

  this->do_update_();
  this->display_frame_();
}

void SSD1680EPaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    return;

  const uint32_t pos = (x / 8) + y * WIDTH_BYTES;
  const uint8_t mask = 0x80 >> (x & 0x07);

  if (color.is_on()) {
    this->buffer_[pos] &= ~mask;   // preto
  } else {
    this->buffer_[pos] |= mask;    // branco
  }
}

}  // namespace ssd1680_epaper
}  // namespace esphome
