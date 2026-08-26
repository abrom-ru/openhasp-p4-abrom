# openhasp-mvp
Minimal Viable Project for openHASP testing

## Tooling and Framework

- ESP IDF 6.0.2
- Board Manager 0.6.4
- LVGL 9.5

## Services and testing

- LVGL display and touch
- WiFi / networking
- HTTP(S) and WS(s)
- MQTT(S)
- LittleFS flash
- SD Card (FAT)
- FTP (optional)

## Tested hardware

- Wireless-Tag WT32-SC01 Plus (@fvanroie)

## Contributing

If you want to add/test a new device, please open an issue with the board title. This will serve as a tasl, follow-up, collaboration and support hub.
When a hardware board is working using the [Espressif Board Manager](https://board-manager.espressif.com/) ([Guide](https://docs.espressif.com/projects/esp-board-manager/en/latest/index.html)), it can be merged via PR.
