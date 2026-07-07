#include "ssd1680_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "driver/gpio.h"
#include <cstring>

namespace esphome {
namespace ssd1680_epaper {

static const char *const TAG = "ssd1680_epaper";

// Tratar este módulo como 250x122 no canvas lógico
static const uint16_t WIDTH = 250;
static const uint16_t HEIGHT = 122;
static const uint16_t WIDTH_BYTES = (WIDTH + 7) / 8;   // 32 bytes
static const uint32_t ALLSCREEN_BYTES = WIDTH_BYTES * HEIGHT;

// Offset conhecido em alguns SSD1680 250x122
static const uint8_t COLSTART = 0x01;   // 8 pixels / 1 byte

void SSD1680EPaper::setup() {
  ESP_LOGI(TAG, "=== SSD1680 SETUP - 250x122 + colstart ===");

  gpio_config_t pwr_conf = {};
  pwr_conf.pin_bit_mask = (1ULL << 7);
  pwr_conf.mode = GPIO_MODE_OUTPUT;
  pwr_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  pwr_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&pwr_conf);
  gpio_set_level(GPIO_NUM_7, 1);
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
  memset(this->buffer_, 0x00, ALLSCREEN_BYTES);

  this->initialized_ = false;
}

void SSD1680EPaper::dump_config() {
  LOG_DISPLAY("", "SSD1680 E-Paper", this);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", WIDTH, HEIGHT);
  ESP_LOGCONFIG(TAG, "  Width bytes: %u", WIDTH_BYTES);
  ESP_LOGCONFIG(TAG, "  Colstart: %u", COLSTART);
  LOG_UPDATE_INTERVAL(this);
}

void SSD1680EPaper::hw_reset_() {
  if (this->reset_pin_ == nullptr)
    return;

  this->reset_pin_->digital_write(true);
  delay(10);
  this->reset_pin_->digital_write(false);
  delay(10);
  this->reset_pin_->digital_write(true);
  delay(10);
}

void SSD1680EPaper::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(100);
    return;
  }

  uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {
    if (millis() - start > 10000) {
      ESP_LOGE(TAG, "Timeout waiting for busy");
      return;
    }
    delay(10);
    App.feed_wdt();
  }
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
  this->hw_reset_();

  this->command_(0x12);
  delay(20);
  this->wait_until_idle_();

  // Driver output control: 122 - 1 = 121 = 0x79
  this->command_(0x01);
  this->data_(0x79);
  this->data_(0x00);
  this->data_(0x00);

  // Data entry mode
  this->command_(0x11);
  this->data_(0x03);

  // X = 250 pixels => 32 bytes. Com offset de 1 byte.
  this->command_(0x44);
  this->data_(COLSTART);
  this->data_(COLSTART + WIDTH_BYTES - 1);

  // Y = 122 linhas
  this->command_(0x45);
  this->data_(0x00);
  this->data_(0x00);
  this->data_(0x79);
  this->data_(0x00);

  this->command_(0x3C);
  this->data_(0x05);

  this->command_(0x18);
  this->data_(0x80);

  this->command_(0x4E);
  this->data_(COLSTART);

  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);
}

void SSD1680EPaper::full_update_() {
  this->command_(0x22);
  this->data_(0xF7);
  this->command_(0x20);

  uint32_t start = millis();
  while (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()) {
    if (millis() - start > 5000) {
      break;
    }
    delay(100);
    App.feed_wdt();
  }
}

void SSD1680EPaper::display_frame_() {
  this->command_(0x4E);
  this->data_(COLSTART);

  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);

  this->command_(0x24);
  for (uint32_t i = 0; i < ALLSCREEN_BYTES; i++) {
    this->data_(~this->buffer_[i]);
  }

  this->command_(0x4E);
  this->data_(COLSTART);

  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);

  this->command_(0x26);
  for (uint32_t i = 0; i < ALLSCREEN_BYTES; i++) {
    this->data_(0x00);
  }

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

  const uint32_t pos = y * WIDTH_BYTES + (x / 8);
  const uint8_t bit = 0x80 >> (x & 0x07);

  if (pos >= ALLSCREEN_BYTES)
    return;

  if (color.is_on()) {
    this->buffer_[pos] |= bit;
  } else {
    this->buffer_[pos] &= ~bit;
  }
}

}  // namespace ssd1680_epaper
}  // namespace esphome
