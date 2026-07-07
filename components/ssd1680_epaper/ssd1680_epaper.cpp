#include "ssd1680_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "driver/gpio.h"

namespace esphome {
namespace ssd1680_epaper {

static const char *const TAG = "ssd1680_epaper";

// VERSION 2 - Deferred init for debugging
// Display dimensions for 2.13" display
static const uint16_t WIDTH = 122;
static const uint16_t HEIGHT = 250;
static const uint32_t ALLSCREEN_BYTES = (WIDTH * HEIGHT) / 8;

void SSD1680EPaper::setup() {
  ESP_LOGI(TAG, "=== SSD1680 SETUP V4 - WITH POWER PIN ===");
  
  // CRITICAL: Enable display power on GPIO7
  // The CrowPanel requires GPIO7 HIGH to power the e-paper display
  gpio_config_t pwr_conf = {};
  pwr_conf.pin_bit_mask = (1ULL << 7);
  pwr_conf.mode = GPIO_MODE_OUTPUT;
  pwr_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  pwr_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&pwr_conf);
  gpio_set_level(GPIO_NUM_7, 1);
  ESP_LOGI(TAG, "GPIO7 (display power) set HIGH");
  delay(100);  // Give power time to stabilize
  
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
  
  // Initialize the display buffer
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
    ESP_LOGCONFIG(TAG, "  Current BUSY state: %s", this->busy_pin_->digital_read() ? "HIGH (busy)" : "LOW (idle)");
  }
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
  
  // Log both pins before we do anything
  if (this->busy_pin_ != nullptr) {
    ESP_LOGI(TAG, "BUSY (GPIO48) before reset: %d", this->busy_pin_->digital_read());
  }
  if (this->reset_pin_ != nullptr) {
    // Temporarily set reset pin as input to read its state
    ESP_LOGI(TAG, "RESET pin is configured as output, current drive: HIGH");
  }
  
  // Hardware reset - log each step
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
  
  // Wait a bit after reset
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
  
  // Wait for SW reset - short timeout for debugging
  uint32_t start = millis();
  while (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()) {
    if (millis() - start > 2000) {
      ESP_LOGE(TAG, "SW Reset timeout after 2s - continuing anyway");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
  
  // Driver output control
  ESP_LOGD(TAG, "Setting driver output (0x01)");
  this->command_(0x01);
  this->data_(0x27);
  this->data_(0x01);
  this->data_(0x00);
  
  // Data entry mode
  ESP_LOGD(TAG, "Setting data entry mode (0x11)");
  this->command_(0x11);
  this->data_(0x03);
  
  // RAM X address
  ESP_LOGD(TAG, "Setting RAM X (0x44)");
  this->command_(0x44);
  this->data_(0x00);
  this->data_(0x0F);
  
  // RAM Y address  
  ESP_LOGD(TAG, "Setting RAM Y (0x45)");
  this->command_(0x45);
  this->data_(0x00);
  this->data_(0x00);
  this->data_(0x27);
  this->data_(0x01);
  
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
  
  // 0xF7 = Enable clock, Load temperature, Load LUT, Display, Disable Analog, Disable OSC
  // This is the full sequence that actually refreshes the e-paper panel
  this->command_(0x22);
  this->data_(0xF7);
  this->command_(0x20);
  
  // Wait for refresh to complete
  // Note: BUSY pin may not go LOW on this display, but refresh still works
  // Typical full refresh takes 2-4 seconds
  uint32_t start = millis();
  while (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()) {
    if (millis() - start > 5000) {  // 5 second timeout
      // This is normal - BUSY doesn't always go LOW on this display
      ESP_LOGD(TAG, "Update timeout (normal for this display) - took %lu ms", millis() - start);
      break;
    }
    delay(100);
    App.feed_wdt();
  }
  
  if (millis() - start < 5000) {
    ESP_LOGD(TAG, "Update completed in %lu ms", millis() - start);
  }
}

void SSD1680EPaper::display_frame_() {
  ESP_LOGD(TAG, "Writing frame to display");
  
  // Hardware reset to recover from any stuck state
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }
  
  // Wait for display to be ready after reset
  this->wait_until_idle_();
  
  // Re-send minimal init commands
  this->command_(0x12);  // SW reset
  delay(10);
  this->wait_until_idle_();
  
  // Driver output control
  this->command_(0x01);
  this->data_(0x27);  // 296 - 1 = 0x127, low byte
  this->data_(0x01);  // high byte
  this->data_(0x00);  // GD=0, SM=0, TB=0
  
  // Data entry mode
  this->command_(0x11);
  this->data_(0x03);  // X inc, Y inc
  
  // Set RAM X address
  this->command_(0x44);
  this->data_(0x00);
  this->data_(0x0F);  // 16 bytes = 128 pixels
  
  // Set RAM Y address  
  this->command_(0x45);
  this->data_(0x00);
  this->data_(0x00);
  this->data_(0x27);
  this->data_(0x01);
  
  // Set RAM address counters
  this->command_(0x4E);
  this->data_(0x00);
  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);
  
  // Write B/W RAM (0x24) - INVERT data for correct polarity
  // This display: 0xFF = black, 0x00 = white (confirmed by testing)
  // ESPHome buffer: bits set = foreground (COLOR_ON), cleared = background
  // We need to invert so drawing shows up correctly
  this->command_(0x24);
  for (uint32_t i = 0; i < ALLSCREEN_BYTES; i++) {
    this->data_(~this->buffer_[i]);  // INVERTED for correct polarity
  }
  
  // Write RED RAM (0x26) - all 0x00 to not interfere
  this->command_(0x4E);
  this->data_(0x00);
  this->command_(0x4F);
  this->data_(0x00);
  this->data_(0x00);
  
  this->command_(0x26);
  for (uint32_t i = 0; i < ALLSCREEN_BYTES; i++) {
    this->data_(0x00);
  }
  
  this->wait_until_idle_();
  
  ESP_LOGD(TAG, "Frame written, starting update");
  this->full_update_();
  ESP_LOGD(TAG, "Display update complete");
}

void SSD1680EPaper::update() {
  if (!this->initialized_) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FIRST UPDATE - INITIALIZING DISPLAY");
    ESP_LOGI(TAG, "  VERSION 3 - Pin swap detection");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Configured pins: CS=45, DC=46, RST=47, BUSY=48");
    ESP_LOGI(TAG, "SPI: CLK=12, MOSI=11");
    
    // Test: Try reading GPIO47 as input to see if BUSY/RESET are swapped
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== PIN SWAP TEST ===");
    ESP_LOGI(TAG, "Reading GPIO48 (configured as BUSY): %d", this->busy_pin_->digital_read());
    
    // Temporarily configure GPIO47 as input to read it
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << 47);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Reading GPIO47 (configured as RESET, now input): %d", gpio_get_level(GPIO_NUM_47));
    
    // Restore GPIO47 as output for reset
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_47, 1);  // Keep high (not in reset)
    ESP_LOGI(TAG, "=== END PIN SWAP TEST ===");
    ESP_LOGI(TAG, "");
    
    this->init_display_();
    this->initialized_ = true;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  INITIALIZATION COMPLETE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
  }
  
  this->do_update_();
  this->display_frame_();
}

void SSD1680EPaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= this->get_width_internal() || y < 0 || y >= this->get_height_internal())
    return;
    
  uint32_t pos = (y * (WIDTH / 8)) + (x / 8);
  uint8_t bit = 0x80 >> (x % 8);
  
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
