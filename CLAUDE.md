# CLAUDE.md - AI Assistant Guide for ESPHome SSD1680 Component

## Project Overview

This repository contains a custom ESPHome component for controlling SSD1680-based e-paper displays. The primary target hardware in this fork/configuration is the **Elecrow CrowPanel ESP32 2.13" E-Paper HMI Display** (ESP32-S3 based, panel GDEM0213B74, 122x250 visible resolution).

**Repository:** [https://github.com/apadua/esphome-ssd1680](https://github.com/apadua/esphome-ssd1680)
**License:** MIT (Copyright 2026 Andre Padua)

> **Note:** This component was originally written for the CrowPanel 2.9" (128x296). It has been adapted here for the CrowPanel 2.13" (122x250 visible / 128x250 RAM). See [Changing Display Dimensions](#changing-display-dimensions) for what was modified.

### Key Features
- Full driver for SSD1680 e-paper controller
- Handles inverted pixel polarity quirk
- Deferred initialization for reliable debugging
- Watchdog-friendly long refresh operations
- Works with or without BUSY pin
- Adapted RAM/gate geometry for the 2.13" panel (128x250 instead of 128x296)

## Directory Structure

```
esphome-ssd1680/
├── CLAUDE.md                    # This file
├── .gitignore                   # Repository-wide ignore rules
└── components/
    └── ssd1680_epaper/          # ESPHome component package
        ├── __init__.py          # Empty marker file (required by ESPHome)
        ├── display.py           # Python config schema & code generation
        ├── ssd1680_epaper.h     # C++ header - class interface
        ├── ssd1680_epaper.cpp   # C++ implementation - driver logic
        ├── crowpanel-clock.yaml # Complete example configuration
        ├── README.md            # User documentation
        ├── LICENSE              # MIT License
        └── .gitignore           # Component-level ignores
```


## Architecture

### Component Model (Three-Layer Design)

1. **Python Layer** (`display.py`): Validates YAML configuration and generates C++ initialization code
2. **C++ Header** (`ssd1680_epaper.h`): Defines class interface inheriting from ESPHome base classes
3. **C++ Implementation** (`ssd1680_epaper.cpp`): Contains driver logic and hardware communication

### Class Hierarchy

```cpp
SSD1680EPaper : public display::DisplayBuffer,
                public spi::SPIDevice<BIT_ORDER_MSB_FIRST, CLOCK_POLARITY_LOW,
                                      CLOCK_PHASE_LEADING, DATA_RATE_4MHZ>
```

The class inherits from:
- `DisplayBuffer` - Provides drawing primitives and buffer management
- `SPIDevice` - Provides SPI communication (4 MHz, Mode 0)

## Key Files

### `display.py` (Configuration Layer)

- **DEPENDENCIES:** `["spi"]`
- **CONFIG_SCHEMA:** Defines valid YAML options
  - `dc_pin` (required) - Data/Command control GPIO
  - `reset_pin` (optional) - Hardware reset GPIO
  - `busy_pin` (optional) - Status signal GPIO
  - Default polling interval: 60 seconds
- **`to_code()`:** Async function generating C++ initialization

### `ssd1680_epaper.h` (Interface)

- Display dimensions (logical/reported to ESPHome): **128 x 250 pixels** (CrowPanel 2.13")
- Visible area on the glass: **122 x 250** (6 columns of RAM fall outside the visible area — a hardware characteristic of this panel, not a bug)
- Display type: Binary (black/white only)
- Key methods: `setup()`, `update()`, `dump_config()`
- Protected methods for SPI commands and display control

### `ssd1680_epaper.cpp` (Implementation)

Key implementation details:

| Function | Purpose |
|----------|---------|
| `setup()` | Enables GPIO7 power, initializes pins/SPI, allocates buffer |
| `init_display_()` | Sends initialization command sequence to display |
| `display_frame_()` | Writes buffer to display RAM with pixel inversion |
| `full_update_()` | Triggers refresh using 0xF7 sequence |
| `update()` | Called on polling interval, handles deferred init |

## Critical Hardware Details

### CrowPanel-Specific Requirements

1. **GPIO7 Power Control**: MUST be set HIGH or display won't work
   ```cpp
   gpio_set_level(GPIO_NUM_7, 1);  // Required!
   ```

2. **Pin Assignments (actual YAML / current device)**:
   | Function | GPIO |
   |----------|------|
   | SPI CLK  | 12   |
   | SPI MOSI | 11   |
   | CS       | 14   |
   | DC       | 13   |
   | RESET    | 10   |
   | BUSY     | 9    |

   > **Note:** Some debug logging inside `update()` still prints hardcoded reference values from the original 2.9" bring-up (`CS=45, DC=46, RST=47, BUSY=48`, and a legacy "pin swap test" reading `GPIO47`/`GPIO48`). These are leftover debug artifacts from earlier hardware revisions and do **not** reflect the pins actually configured via YAML on the 2.13" board. They can be safely removed or updated to avoid confusion — see [Common Issues and Solutions](#common-issues-and-solutions).

### Display Quirks

1. **Inverted Pixel Polarity**: Data must be XORed before sending
   ```cpp
   this->data_(~this->buffer_[i]);  // critical!
   ```

2. **BUSY Pin Behavior**: May not go LOW reliably after refresh. The driver handles this gracefully with timeouts.

3. **Refresh Time**: Full refresh takes ~2.3-4 seconds (5 second timeout is normal)

4. **Panel geometry (2.13" specific)**: The glass has **250 gate lines**, not 296 like the 2.9" model. Configuring `Driver Output Control (0x01)` for 296 lines instead of 250 causes visible scan mismatches (diagonal/skewed artifacts). This was confirmed against the official reference driver `GxEPD2_213_B74` (GDEM0213B74 panel), which uses `WIDTH = 128`, `HEIGHT = 250`.

### SSD1680 Commands Used

| Command | Purpose | 2.13" specific value |
|---------|---------|----------------------|
| 0x12 | Software Reset | — |
| 0x01 | Driver Output Control | `0xF9, 0x00, 0x00` (HEIGHT-1 = 249) |
| 0x11 | Data Entry Mode | `0x03` |
| 0x18 | Temperature Sensor | `0x80` |
| 0x22/0x20 | Display Update Control | `0xF7` full refresh |
| 0x24 | Write B/W RAM | inverted buffer data |
| 0x26 | Write RED RAM | all `0x00` (unused) |
| 0x3C | Border Waveform | `0x05` |
| 0x44 | Set RAM X Address | `0x00, 0x0F` (16 bytes = 128 px) |
| 0x45 | Set RAM Y Address | `0x00, 0x00, 0xF9, 0x00` (0..249) |
| 0x4E/0x4F | Set RAM Counters | `0x00` / `0x00, 0x00` |
| 0xF7 | Full refresh with internal LUT | — |

## Development Workflow

### Adding to an ESPHome Project

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/apadua/esphome-ssd1680
      ref: main
    refresh: 0s          # forces ESPHome to fetch the latest commit, avoids stale cache
    components: [ssd1680_epaper]
```

> **Tip:** Without `refresh: 0s`, ESPHome caches the cloned repo locally and may not pick up new commits pushed to `main`. If updates still don't apply, clear `~/.esphome/external_components/` (or the equivalent path under the Home Assistant add-on config folder) and rebuild.

### Local Development

1. Clone the repository
2. Edit files in `components/ssd1680_epaper/`
3. Test by pointing ESPHome to local path:
   ```yaml
   external_components:
     - source:
         type: local
         path: /path/to/esphome-ssd1680/components
   ```

### Build Process

1. ESPHome parses YAML configuration
2. `display.py` validates against `CONFIG_SCHEMA`
3. `to_code()` generates C++ initialization
4. ESP-IDF compiles C++ with platformio
5. Binary uploaded to ESP32-S3

## Coding Conventions

### C++ Style

- **Namespace:** `esphome::ssd1680_epaper`
- **Method naming:** `snake_case` with trailing underscore for private methods
- **Logging:** Use `ESP_LOGI()`, `ESP_LOGD()`, `ESP_LOGE()` macros
- **Tag:** `static const char *const TAG = "ssd1680_epaper";`

### Python Style

- Follow ESPHome configuration patterns
- Use `cv.*` validators for schema validation
- Async `to_code()` with `cg.*` code generation

### YAML Style

- Standard ESPHome format
- Use `id()` for component references
- Use `!secret` for sensitive values
- Use `gfonts://` for Google Fonts

## Testing

### Minimal Test Configuration (CrowPanel 2.13")

```yaml
display:
  - platform: ssd1680_epaper
    id: epaper
    cs_pin: GPIO14
    dc_pin: GPIO13
    reset_pin: GPIO10
    busy_pin: GPIO9
    lambda: |-
      it.fill(COLOR_OFF);
      it.print(0, 0, id(font), "Hello World");
```

### Debug Logging

The driver includes extensive logging. Enable debug logging in ESPHome:
```yaml
logger:
  level: DEBUG
```

Look for log lines starting with `[ssd1680_epaper]`.

## Common Issues and Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| Display blank | GPIO7 not enabled | Verify GPIO7 HIGH (handled in setup) |
| Inverted colors | Pixel polarity | Already handled via XOR in driver |
| Update timeout warnings | Normal BUSY behavior | Safe to ignore - display still works |
| Ghosting/artifacts | E-paper characteristic | Use full refresh (already implemented) |
| **Diagonal/skewed image** | `WIDTH`/`HEIGHT` and registers 0x01/0x45 set for 296 gate lines on a 250-line panel | Set `WIDTH=128`, `HEIGHT=250`, and update 0x01/0x45 to use `0xF9` (249) as documented above |
| **ESPHome not picking up latest git commit** | External component cache not refreshed | Add `refresh: 0s` to `external_components.source`, or clear `~/.esphome/external_components/` and rebuild. Confirm the commit was actually pushed to the referenced branch/ref |
| Confusing pin numbers in logs (`CS=45, DC=46, RST=47, BUSY=48`) | Leftover hardcoded debug strings from 2.9" bring-up in `update()` | Cosmetic only — actual pins are whatever is configured in YAML (`cs_pin`, `dc_pin`, `reset_pin`, `busy_pin`). Safe to update/remove those log lines |

## Dependencies

### ESPHome Components (Required)
- `spi` - SPI bus communication
- `display` - Display buffer and drawing primitives

### External Libraries
- None required - uses ESPHome framework only

### ESP-IDF APIs Used
- `driver/gpio.h` - For GPIO7 power control

## File Metrics

| File | Lines | Purpose |
|------|-------|---------|
| `ssd1680_epaper.cpp` | ~405 | Core driver implementation |
| `display.py` | 55 | Configuration schema |
| `ssd1680_epaper.h` | 47 | Class interface |
| `crowpanel-clock.yaml` | 142 | Working example |
| `README.md` | 262 | User documentation |

## Making Changes

### Modifying Display Initialization

Edit `init_display_()` in `ssd1680_epaper.cpp`. The sequence follows the SSD1680 datasheet, adapted for the panel's actual gate line count (250 for the 2.13" model).

### Adding Configuration Options

1. Add constant to `display.py` imports or define new one
2. Add to `CONFIG_SCHEMA`
3. Handle in `to_code()` function
4. Add setter in `.h` file
5. Use value in `.cpp` implementation

### Changing Display Dimensions

Modify these locations:
- `ssd1680_epaper.cpp`:
  - `WIDTH` and `HEIGHT` constants (currently `128` and `250` for the 2.13" panel)
  - `ALLSCREEN_BYTES = (WIDTH * HEIGHT) / 8` (currently `4000` bytes)
  - Register `0x01` (Driver Output Control) in both `init_display_()` and `display_frame_()`: low byte must equal `HEIGHT - 1` (`0xF9` = 249 for HEIGHT=250)
  - Register `0x45` (RAM Y Address) in both functions: end value must match `HEIGHT - 1` (`0xF9, 0x00`)
- `ssd1680_epaper.h`: `get_width_internal()` (currently `128`) and `get_height_internal()` (currently `250`)

> **Reference used to validate these values:** the official `GxEPD2_213_B74` driver (GDEM0213B74 panel, SSD1680), which confirms `WIDTH = 128`, `HEIGHT = 250` for this exact panel — different from the 2.9" panel's `128 x 296`. Do **not** assume the visible resolution (122x250) equals the internal RAM/gate geometry; the RAM width stays 128 (16 bytes) even though only 122 columns are visible.

### Centering content on the 2.13" panel

Because the visible area (122px) is smaller than the RAM width (128px), and depending on `rotation:` in YAML, drawing coordinates may need a manual offset to appear centered on the physical glass. In practice this has required an empirically-determined horizontal offset (around 46px in the rotated/lateral rendering case) — calculate your "usable" origin/width explicitly in the lambda rather than relying on `it.get_width()/2`.

## Git Workflow

- Main branch: `main`
- Feature branches: `claude/` prefix for AI-assisted development
- Commit messages: Descriptive, explaining what changed

## Resources

- [SSD1680 Datasheet](https://www.good-display.com/companyfile/101.html)
- [ESPHome Custom Components](https://esphome.io/custom/custom_component.html)
- [ESPHome Display Component](https://esphome.io/components/display/index.html)
- [Elecrow CrowPanel ESP32 2.13" E-Paper HMI Display (122x250)](https://www.elecrow.com/crowpanel-esp32-2-13-e-paper-hmi-display-with-122-250-resolution-black-white-color-driven-by-spi-interface.html)
- [GxEPD2 driver reference for GDEM0213B74 (GxEPD2_213_B74)](https://github.com/ZinggJM/GxEPD2) — used to validate WIDTH/HEIGHT and register values for this panel
