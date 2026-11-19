# Metalio E-Ink4-Plus port — working notes

Status: **stage 1 passed on hardware** (2026-11-19). Smoke test built in CI booted from
app1 under the device bootloader, TPS65185 rails up (`tps_ok=1`), full GC16 pattern drawn,
stable for minutes.

## Device facts (from a full flash backup of the user's unit)

- Chip ESP32-S31 rev v0.0, MAC 1c:29:04:d0:46:d6, 16 MB flash, octal PSRAM.
- Bootloader at 0x2000, built with ESP-IDF release/v6.1 `9a97f6c5`.
- Partition table (RickyOS-Plus layout): nvs 0x9000, otadata 0xe000, app0 0x10000 (6.25 MiB),
  app1 0x650000 (6.25 MiB), spiffs 0xc90000, coredump 0xff0000.
- app0 = RickyOS-Plus 1.2.26 (user's fallback). app1 = our test builds.
- otadata CRC = zlib.crc32(seq_le32, 0xFFFFFFFF); slot = (seq-1) % 2. `tools/make_otadata.py`.
- Stock Metalio firmware is no longer on the device (only first 1 MB of the stock layout saved).

## Panel orientation (measured on hardware, stage 1)

EPDiy runs in `EPD_ROT_LANDSCAPE`, native 1216×684. Holding the reader portrait with the
cover keys at the bottom: native (0,0) is the **top-right** corner, native +x runs **down**,
native +y runs **left**. Portrait (px, py) with px∈[0,684), py∈[0,1216) →
native (x = py, y = 683 − px).

A first frame after boot must clear from an unknown optical state (RickyOS uses
GC16 from-white / cleanup), otherwise the previous image ghosts through.

## Toolchain decision for CrossNext

CrossNext's build depends on PlatformIO extra_scripts (HTML, i18n, word lists, library
patches), so stage 2 stays on PlatformIO:

- pioarduino platform-espressif32 `develop` (pinned `23ebbebac3f515c481396c69217d0eca4f0dfa37`)
  has board `esp32-s31-coreboard-1`, Arduino-ESP32 **4.0.0**, framework-espidf **v6.1.1**.
- v6.1.1 contains `9a97f6c5` (250 commits newer). RickyOS's lock-up warning concerns the older
  plain `v6.1` tag. If a v6.1.1 app does not boot, pin framework-espidf to `9a97f6c5` via
  `platform_packages` and use hybrid compile.
- Plus board needs DIO flash + octal PSRAM (`memory_type = dio_opi`), partitions from
  `ports/metalio-plus/partitions.csv`, app ≤ 6.25 MiB.

## Source of the hardware layer

RickyOS `codex/plus-reading-stats-recovery`, `ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/`:

- `components/metalio_board` — EPDiy + TPS65185 + TCA9555 board, FT6336U touch, SD, waveforms.
  I2C via Arduino `Wire1` (`bus/plus_i2c_wire.cc`, SDA0/SCL1, 100 kHz); stage 1 uses
  `bus/plus_i2c_idf.c` instead.
- `components/rickyos_display/src/driver/MetalioPlusDriver.cpp` — FreeInkDisplay panel driver
  (1-bpp/2-bit gray → EPDiy 4-bpp, GC16/GL16/DU, reader turn waveforms).
- `components/rickyos_platform/include/BoardConfig.h` — `makeMetalioPlusProfile()`:
  FT6336U 0x38 IRQ5 (swapXY, flipY, cover keys at raw Y≈1600, X 100/200/300 = home/prev/next),
  BOOT GPIO61, power key TCA P1.7, volume TCA P1.0/P1.1, frontlight cool GPIO6 / warm GPIO7
  (20 kHz, 10-bit), SDMMC 4-bit CLK24 CMD25 D0-3 = 20-23, BQ27220 0x55, PCF8563 0x51,
  SC7A20H 0x19 (INT wired-AND with TCA INT on GPIO2 → must idle released), insets 8 px,
  motor TCA P0.3, BT audio module + amp P0.4/P1.4.
- `components/rickyos_application/port/PlusBoard.cpp` — board bring-up order, power key,
  sleep/wake.

## CrossNext changes needed (stage 2+)

1. `freeink-sdk` BoardConfig: `FREEINK_DEVICE_METALIO_PLUS`, `FREEINK_MCU_S31`,
   `Board::MetalioPlus`, `DisplayController::MetalioParallel`, Plus profile (CrossNext struct
   field names differ from RickyOS's in places — build it with a constexpr function).
2. FreeInkDisplay: `MetalioPlusDriver` adapted to CrossNext's `PanelDriver` interface.
3. Board lib from `metalio_board` (PlatformIO `library.json`), keeping `plus_i2c_wire.cc`.
4. InputManager: FT6336U already exists; add cover keys, TCA9555 power/volume keys, BOOT.
5. SDCardManager: `FREEINK_MCU_S3 || FREEINK_MCU_ESP32` guards at lines ~166/183 must
   include S31 for SDMMC.
6. platformio env `metalio_plus` + CI job building it.
