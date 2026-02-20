# SpotPear ESP32-S3 N16R8 1.28" BOX — Hardware Reference

## Product Info

- **Product Name**: DeepSeek XiaoZhi AI Voice Chat Robot BOX ESP32-S3 1.28 inch Round LCD TouchScreen N16R8
- **Manufacturer**: SpotPear
- **AliExpress Link**: https://fr.aliexpress.com/item/1005009016529496.html
- **SpotPear Product Page**: https://spotpear.cn/shop/ESP32-S3-N16R8-AI-DeepSeek-XiaoZhi-XiaGe-Qwen-DouBao-1.28-inch-Round-LCD-BOX-TouchScreen.html
- **SpotPear Wiki**: https://spotpear.com/wiki/ESP32-S3-N16R8-AI-DeepSeek-XiaoZhi-XiaGe-Qwen-DouBao-1.28-inch-Round-LCD-BOX-TouchScreen.html
- **Support Email**: tech-support@spotpear.com / services04@spotpear.cn

## SoC & Memory

| Spec | Value |
|---|---|
| SoC | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Module | ESP32-S3-WROOM-1 N16R8 |
| Flash | 16 MB |
| PSRAM | 8 MB (Octal SPI) |
| Wi-Fi | 2.4 GHz 802.11 b/g/n |
| Bluetooth | BLE 5.0 |

## GPIO Pin Map (from config.h)

### Audio — ES8311 Codec (I2C controlled)

| Function | GPIO | Notes |
|---|---|---|
| I2S MCLK | 16 | Master clock |
| I2S WS (LRCK) | 45 | Word select / left-right clock |
| I2S BCLK (SCLK) | 9 | Bit clock |
| I2S DIN (mic data) | 10 | ESP32 receives from codec (labeled DOUT on codec side) |
| I2S DOUT (spk data) | 8 | ESP32 sends to codec (labeled DIN on codec side) |
| PA Enable | 46 | Speaker power amplifier control |
| Codec I2C SDA | 15 | ES8311 I2C data (I2C_NUM_0) |
| Codec I2C SCL | 14 | ES8311 I2C clock (I2C_NUM_0) |
| ES8311 I2C Address | 0x18 | ES8311_CODEC_DEFAULT_ADDR |

- Input sample rate: 16000 Hz
- Output sample rate: 16000 Hz

### Display — 1.28" Round IPS LCD (GC9A01)

| Function | GPIO | Notes |
|---|---|---|
| SPI SCLK | 4 | SPI clock (40 MHz) |
| SPI MOSI | 2 | SPI data |
| SPI CS | 5 | Chip select |
| DC | 47 | Data/Command |
| RESET | 38 | Hardware reset |
| Backlight | 42 | PWM, active-low (inverted) |

- Resolution: 240 x 240 pixels
- Driver IC: GC9A01
- SPI bus: SPI3_HOST
- Color: 16-bit RGB (BGR endian)

### Touch — CST816D (I2C)

| Function | GPIO | Notes |
|---|---|---|
| SDA | 11 | I2C data (I2C_NUM_1) |
| SCL | 7 | I2C clock (I2C_NUM_1) |
| RST | 6 | Hardware reset |
| INT | 12 | Interrupt (unused, polled at 10ms) |
| I2C Address | 0x15 | CST816D default |

### Power Management

| Function | GPIO | Notes |
|---|---|---|
| Battery ADC | 1 | ADC_CHANNEL_0, voltage divider |
| Charging Status | 41 | Input, detects charging state |
| Power Control (MOS) | 3 | RTC GPIO, controls power latch. HIGH=on, LOW=shutdown. Held during deep sleep. |

### Other

| Function | GPIO | Notes |
|---|---|---|
| Built-in LED | 48 | Single LED |
| BOOT Button | 0 | Also used for Wi-Fi config / toggle chat |
| USB-UART TX | 43 | CH343P USB-to-UART |
| USB-UART RX | 44 | CH343P USB-to-UART |

### SD Card / TF Card Slot

| Function | GPIO | Notes |
|---|---|---|
| MOSI | **UNKNOWN** | Slot present on PCB but pins not documented |
| MISO | **UNKNOWN** | SpotPear says "DIY customer use, generally unavailable" |
| CLK | **UNKNOWN** | Use sd_card_scanner project to detect |
| CS | **UNKNOWN** | |

## GPIO Usage Summary

### Used GPIOs

| GPIO | Function |
|---|---|
| 0 | BOOT button |
| 1 | Battery ADC |
| 2 | Display SPI MOSI |
| 3 | Power control (RTC) |
| 4 | Display SPI SCLK |
| 5 | Display SPI CS |
| 6 | Touch RST |
| 7 | Touch SCL |
| 8 | I2S DOUT (speaker) |
| 9 | I2S BCLK |
| 10 | I2S DIN (mic) |
| 11 | Touch SDA |
| 12 | Touch INT |
| 14 | Codec I2C SCL |
| 15 | Codec I2C SDA |
| 16 | I2S MCLK |
| 38 | Display RESET |
| 41 | Charging status |
| 42 | Display backlight |
| 43 | UART TX (USB) |
| 44 | UART RX (USB) |
| 45 | I2S WS (LRCK) |
| 46 | PA enable |
| 47 | Display DC |
| 48 | Built-in LED |

### Unavailable GPIOs (internal to N16R8 module)

| GPIO Range | Function |
|---|---|
| 26–32 | Connected to SPI flash (do not use) |
| 33–37 | Connected to Octal SPI PSRAM (do not use) |

### Free GPIOs (available for SD card or other peripherals)

| GPIO | Notes |
|---|---|
| 13 | Free |
| 17 | Free |
| 18 | Free |
| 19 | Free |
| 20 | Free |
| 21 | Free |
| 39 | Free |
| 40 | Free |

## Physical Board Description (from photos)

- **Form factor**: Round PCB (~40mm diameter), designed for watch-style enclosure
- **Front side** (component side):
  - ESP32-S3 module (large QFN package, center)
  - ES8311 audio codec IC
  - CH343P USB-to-UART converter
  - FPC connector for 1.28" round LCD display
  - MEMS microphone (bottom left area)
  - USB-C connector (bottom edge)
  - JST battery connector (right side, labeled BAT + -)
  - Two tactile buttons (BOOT and power)
  - Red LED (labeled C3)
  - Test pads labeled: 5V, GND, 3V3, IO48
- **Back side**:
  - Micro SD / TF card slot (push-in type, bottom right)
  - LiPo battery (with kapton tape, glued to back)
  - Speaker module (small rectangular, black)
  - Mounting screw holes (brass standoffs)

## Software Configuration

### XiaoZhi Firmware

```bash
idf.py set-target esp32s3
idf.py menuconfig
# -> Xiaozhi Assistant -> Board Type -> Spotpear ESP32-S3-1.28-BOX
idf.py build
```

### Board Source Files

```
main/boards/sp-esp32-s3-1.28-box/
├── config.h              # Pin definitions and hardware config
├── config.json           # Board metadata
├── power_manager.h       # Battery/charging management
├── README.md             # Build instructions (Chinese)
└── sp-esp32-s3-1.28-box.cc  # Board class implementation
```

### Identical Audio Pin Boards (can share audio config)

These boards in the project have the exact same I2S + ES8311 I2C pin assignments:
- `movecall-moji-esp32s3` — 100% identical audio pins
- `sp-esp32-s3-1.54-muma` — 100% identical audio pins
- `esp32s3-korvo2-v3` — Same I2S pins, different I2C (SDA=17, SCL=18)

### Notes

- Display code is commented out in `sp-esp32-s3-1.28-box.cc` (screen was broken/removed)
- Touch polling still runs if CST816D is detected; gracefully skips if not found
- GPIO 3 is used as RTC GPIO for power latch — must stay HIGH to keep the board powered
