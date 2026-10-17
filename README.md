# cardputer-homelab

A pocket homelab panel for the [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) (original, ESP32-S3, no PSRAM).
It shows whether each machine is up, with CPU, RAM, fullest disk, GPU, VRAM and container counts, refreshed every 15 s.

```
Cardputer --Wi-Fi--> fleet-panel (server/fleet-panel.py) --ssh--> every machine
                     runs `fleet --json`, serves a slim summary
```

Data comes from [fleet](https://github.com/cobanov/fleet). The small server keeps only the fields the panel shows; raw fleet output holds process command lines, which can contain tokens, so it never leaves the server.

## Firmware

```sh
cp include/secrets.example.h include/secrets.h   # add your Wi-Fi networks
# set PANEL_URL in src/main.cpp to the machine running fleet-panel
pio run -t upload
```

The Cardputer joins the strongest known network from `secrets.h` in the background.

Keys: `j`/`k` (or `;`/`.`) select, `Enter` or `1`-`9` details, `q` back, `r` refresh, `t` NTP sync, `z` time zone, `i` help, `x` restart.

First flash over UiFlow: switch the Cardputer off, hold `G0`, plug in USB. The battery keeps it running otherwise, so replugging alone does not reset it.

## Server

Needs `fleet` configured with your hosts (`fleet setup`) and Python 3.

```sh
python3 server/fleet-panel.py            # serves GET /panel on :8791 (PORT to change)
```

`server/dev.cobanov.fleet-panel.plist` is a macOS LaunchAgent that keeps it running. The service has no auth, so keep it on a trusted LAN.
