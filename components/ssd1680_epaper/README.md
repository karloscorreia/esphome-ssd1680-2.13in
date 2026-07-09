# ESPHome SSD1680 E-Paper Display Component

A custom ESPHome component for SSD1680-based e-paper displays. Originally developed for the **Elecrow CrowPanel ESP32 2.9" E-Paper HMI Display**, and adapted in this configuration for the **Elecrow CrowPanel ESP32 2.13" E-Paper HMI Display** (122x250, GDEM0213B74 panel). Compatible with other SSD1680/SSD1680Z displays with the appropriate dimension adjustments.

## Features

- Support for SSD1680-based e-paper displays, including 128x296 (2.9") and 128x250/122x250 (2.13") panels
- Compatible with SSD1680 and SSD1680Z driver chips
- Proper full refresh using internal LUT
- Handles inverted pixel polarity common in these displays
- Deferred initialization for reliable startup logging
- Watchdog-friendly long refresh operations

## Supported Hardware

### Tested
- [Elecrow CrowPanel ESP32 2.13" E-Paper HMI Display](https://www.elecrow.com/crowpanel-esp32-2-13-e-paper-hmi-display-with-122-250-resolution-black-white-color-driven-by-spi-interface.html)
  - Resolution: 122x250 visible pixels (128x250 internal RAM/gate geometry)
  - Driver: SSD1680Z
  - ESP32-S3 based
- [Elecrow CrowPanel ESP32 2.9" E-Paper HMI Display](https://www.elecrow.com/crowpanel-esp32-2-9-e-paper-hmi-display-with-128-296-resolution-black-white-color-driven-by-spi-interface.html)
  - Resolution: 128x296 pixels
  - Driver: SSD1680Z
  - ESP32-S3 based

### Should Work (Untested)
- Other SSD1680-based e-paper displays with matching resolution and dimension constants adjusted accordingly
- Good Display GDEW029T5 (2.9")
- Good Display GDEM0213B74 (2.13", same family as the tested CrowPanel 2.13")
- Waveshare 2.9" V2 (SSD1680)

> **Important:** This driver hardcodes `WIDTH`/`HEIGHT` and the SSD1680 `0x01`/`0x45` register values for a specific panel geometry. If you use a different SSD1680 panel size, see [Changing Display Dimensions](#changing-display-dimensions-for-other-panel-sizes) before assuming it will work out of the box.

## Installation

### Using External Components (Recommended)

Add this to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/apadua/esphome-ssd1680
      ref: main
    refresh: 0s          # ensures ESPHome always pulls the latest commit instead of using a stale cache
    components: [ssd1680_epaper]
```

> If ESPHome doesn't seem to pick up recent changes pushed to the repository, confirm `refresh: 0s` is set, and/or clear the local external component cache (usually under `~/.esphome/external_components/` or the equivalent path in your Home Assistant add-on config).

### Manual Installation

1. Copy the `components/ssd1680_epaper` folder to your ESPHome configuration directory
2. Reference it as a local external component:

```yaml
external_components:
  - source:
      type: local
      path: components
```

## Configuration

### Basic Configuration (CrowPanel 2.13")

```yaml
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: ssd1680_epaper
    id: epaper_display
    cs_pin: GPIO14
    dc_pin: GPIO13
    reset_pin: GPIO10
    busy_pin: GPIO9
    rotation: 270
    update_interval: 60s
    lambda: |-
      it.printf(it.get_width()/2, 100, id(my_font), TextAlign::CENTER, "Hello World!");
```

### CrowPanel ESP32 2.13" Full Example

```yaml
esphome:
  name: crowpanel-epaper
  friendly_name: CrowPanel E-Paper

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
  flash_size: 8MB

psram:
  mode: octal
  speed: 80MHz

logger:

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

external_components:
  - source:
      type: git
      url: https://github.com/apadua/esphome-ssd1680
      ref: main
    refresh: 0s
    components: [ssd1680_epaper]

spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: ssd1680_epaper
    id: epaper_display
    cs_pin: GPIO14
    dc_pin: GPIO13
    reset_pin: GPIO10
    busy_pin: GPIO9
    rotation: 270
    update_interval: 60s
    lambda: |-
      // Fill background with white
      it.fill(COLOR_OFF);

      // On this panel, the visible area (122px) is narrower than the
      // internal RAM width (128px). Calculate an explicit usable origin
      // instead of relying purely on it.get_width()/2 for centering.
      auto W = it.get_width();
      const int usable_w = 122;
      const int x_origin = W - usable_w;  // adjust empirically for your rotation

      // Draw title
      it.printf(x_origin + usable_w / 2, 10, id(font_title), COLOR_ON, TextAlign::TOP_CENTER, "E-Paper Display");

      // Draw time if available
      if (id(ha_time).now().is_valid()) {
        it.strftime(x_origin + usable_w / 2, 50, id(font_large), COLOR_ON, TextAlign::TOP_CENTER, "%H:%M", id(ha_time).now());
        it.strftime(x_origin + usable_w / 2, 100, id(font_medium), COLOR_ON, TextAlign::TOP_CENTER, "%A", id(ha_time).now());
        it.strftime(x_origin + usable_w / 2, 130, id(font_medium), COLOR_ON, TextAlign::TOP_CENTER, "%B %d, %Y", id(ha_time).now());
      }

font:
  - file: "gfonts://Roboto@700"
    id: font_title
    size: 18
  - file: "gfonts://Roboto@700"
    id: font_large
    size: 40
  - file: "gfonts://Roboto@400"
    id: font_medium
    size: 16

time:
  - platform: sntp
    id: ha_time
    timezone: America/Sao_Paulo
```

## Pin Configuration

### Elecrow CrowPanel ESP32 2.13"

| Function | GPIO |
|----------|------|
| SPI CLK  | GPIO12 |
| SPI MOSI | GPIO11 |
| CS       | GPIO14 |
| DC       | GPIO13 |
| RESET    | GPIO10 |
| BUSY     | GPIO9 |

### Elecrow CrowPanel ESP32 2.9" (reference / other panel)

| Function | GPIO |
|----------|------|
| SPI CLK  | GPIO12 |
| SPI MOSI | GPIO11 |
| CS       | GPIO45 |
| DC       | GPIO46 |
| RESET    | GPIO47 |
| BUSY     | GPIO48 |

### CrowPanel Buttons (Optional)

| Button | GPIO |
|--------|------|
| Exit   | GPIO1 |
| Home   | GPIO2 |
| Up     | GPIO6 |
| Down   | GPIO4 |
| Center | GPIO5 |

## Configuration Options

| Option | Required | Description |
|--------|----------|--------------|
| `cs_pin` | Yes | SPI Chip Select pin |
| `dc_pin` | Yes | Data/Command pin |
| `reset_pin` | No | Hardware reset pin (recommended) |
| `busy_pin` | No | Busy status pin (recommended) |
| `rotation` | No | Display rotation (0, 90, 180, 270) |
| `update_interval` | No | How often to refresh (default: 60s) |
| `lambda` | No | Drawing code |

## Drawing

The display uses a binary color model:
- `COLOR_ON` (or `Color::BLACK`) - Black pixels
- `COLOR_OFF` (or `Color::WHITE`) - White pixels

### Examples (CrowPanel 2.13", 122x250 visible area)

```yaml
lambda: |-
  // Fill entire screen white
  it.fill(COLOR_OFF);

  // Draw black rectangle
  it.filled_rectangle(10, 10, 50, 27, COLOR_ON);

  // Draw text
  it.printf(it.get_width()/2, 100, id(my_font), COLOR_ON, TextAlign::CENTER, "Hello!");

  // Draw line (bounds match the 128x250 internal buffer size for this panel)
  it.line(0, 0, 128, 250, COLOR_ON);

  // Draw circle
  it.circle(64, 125, 30, COLOR_ON);
```

## Troubleshooting

### Display not updating
1. Check all pin connections
2. Verify the BUSY pin is connected - without it, timing may be off
3. E-paper full refresh takes 2-4 seconds; the timeout warning in logs is often normal

### Inverted colors
The component handles pixel polarity inversion internally. If colors appear inverted, there may be a display variant issue - please open an issue.

### Diagonal or skewed image
This usually means the `WIDTH`/`HEIGHT` constants (and the corresponding SSD1680 `0x01`/`0x45` register values) don't match the actual number of gate lines on your panel. For example, the 2.9" panel has 296 gate lines, while the 2.13" panel (GDEM0213B74) has only 250. Configuring 296 on a 250-line panel produces visible scan artifacts. Confirm your panel's real geometry against its datasheet or an equivalent reference driver (e.g. `GxEPD2` for the same panel model) before adjusting these values.

### Timeout warnings in logs
Messages like "Update timeout after 5000 ms" are often normal. The SSD1680's BUSY pin doesn't always behave as expected, but the display typically still updates correctly.

### Ghosting or artifacts
E-paper displays can retain previous images. Try:
- Doing a few full refreshes
- Power cycling the device
- This is normal e-paper behavior, not a driver issue

### ESPHome not picking up the latest changes from GitHub
ESPHome caches externally-sourced git components locally and doesn't always re-fetch automatically. If you pushed changes to the repository but they don't appear in your build:
1. Confirm the commit was actually pushed to the branch/ref referenced in `external_components.source.ref`.
2. Add `refresh: 0s` under `source` to force a fresh fetch on every compile.
3. Clear the local cache folder (e.g. `~/.esphome/external_components/`) and rebuild if the issue persists.

## Technical Notes

- Uses 0xF7 update sequence for full refresh with internal LUT
- Pixel data is inverted before sending (this display uses inverted polarity)
- BUSY pin behavior varies; timeout is handled gracefully
- Full refresh takes approximately 2-4 seconds
- **Panel geometry is hardcoded**, not auto-detected. `WIDTH`, `HEIGHT`, and the SSD1680 `0x01`/`0x45` register payloads must match the actual gate line count of your specific panel

## Changing Display Dimensions (for other panel sizes)

If you're adapting this component to a different SSD1680 panel size, update:

- `ssd1680_epaper.cpp`:
  - `WIDTH` and `HEIGHT` constants
  - `ALLSCREEN_BYTES = (WIDTH * HEIGHT) / 8`
  - Register `0x01` (Driver Output Control): low byte = `HEIGHT - 1`
  - Register `0x45` (RAM Y Address): end value = `HEIGHT - 1`
- `ssd1680_epaper.h`:
  - `get_width_internal()` and `get_height_internal()`

Cross-check your values against an existing, known-working driver for your exact panel model (e.g. the corresponding `GxEPD2` driver class) rather than assuming the advertised "visible resolution" equals the internal RAM/gate geometry — these are often different (as is the case for the 2.13" panel, where 122 columns are visible but the RAM width is 128).

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## License

MIT License - See [LICENSE](LICENSE) file for details.

## Acknowledgments

- Developed with assistance from Claude (Anthropic)
- Based on SSD1680 datasheet, Elecrow's Arduino examples, and the `GxEPD2_213_B74` reference driver for the 2.13" panel geometry
- Thanks to the ESPHome community
