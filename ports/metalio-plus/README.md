# CrossNext on the Metalio E-Ink4-Plus (work in progress)

Target: **Metalio E-Ink4-Plus V1.5** — ESP32-S31, 4.7" ED047TC2-class parallel panel
(E0470A14-AF, native 1216×684, UI portrait 684×1216), TPS65185 + TCA9555, FT6336U
touch, cool/warm frontlight. Not for the ordinary Metalio E-Ink 4 (ESP32-S3, SPI panel).

The board layer comes from RickyOS's Plus port
(`chinoryunqin/RickyOS`, branch `codex/plus-reading-stats-recovery`,
`ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/components/metalio_board`),
MIT licensed — see `smoke/components/plus_board/LICENSE.vendor` and `NOTICE.md`.

## Toolchain

ESP-IDF **release/v6.1 at `9a97f6c54ec638111ce55cd36581b3c192f15207`**, target `esp32s31`.
The device bootloader is from that IDF; apps built with another IDF lock up at boot.
`ci/check_idf.py` refuses any other checkout. Builds run in GitHub Actions
(`.github/workflows/metalio-plus.yml`).

## Stages

1. `smoke/` — plain IDF app: panel power-up, full GC16 test pattern, USB log. *(current)*
2. CrossNext boots on the Plus at 684×1216.
3. Touch, cover keys, side keys, SD card.
4. Frontlight, battery, sleep/wake, Wi-Fi.
5. Layout polish for CrossNext screens and games.

## Flashing (app1 only)

The partition table is RickyOS-Plus's: `app0 @0x10000`, `app1 @0x650000`, 6.25 MiB each.
Test builds go to **app1** so RickyOS in app0 stays as the fallback. Never run
`idf.py flash`: it rewrites the bootloader, partition table and app0.

```
esptool --port COM4 write-flash 0x650000 CrossNext-Plus-Smoke-app1.bin
esptool --port COM4 write-flash 0xe000 otadata-app1.bin
```

Back to RickyOS: `esptool --port COM4 write-flash 0xe000 otadata-app0.bin`
(or restore the full flash backup).
