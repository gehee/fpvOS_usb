# fpvOS_usb

What an fpvOS unit shows on USB: a serial port in every mode, and next to it
the USB network (normal and update modes) or the unit's own AR8030 radio chip
(bridge mode). A tool on the host changes the mode over the serial port.
[docs/protocol.md](docs/protocol.md) has the protocol and the modes.

- `src/`: `fpvos-usb`, the mode changes (`fpvos-usb mode ...`) and the
  bridge (`fpvos-usb bridge`).
- `boards/<board>/`: what differs per unit: `fpvos-usb.conf` (installed as
  `/etc/default/fpvos-usb`, with the board id the host picks firmware by) and
  the hooks in `/usr/libexec/fpvos-usb/`: `radio-stop` (stops whatever uses
  the radio), `chip-load DIR IMAGE [CONFIG]` (loads the unit's AR8030 with an
  image: the host's in bridge mode) and `radio-start` (the way back: the
  unit's own firmware in its chip, then what uses it).
  An image runs `fpvos-usb shutdown` before its own USB gadget goes down at
  shutdown: unbinding the gadget under the bridge's endpoints hangs the
  reboot.
- `tests/`: host tests.

The serial port's updater is fpvos-update's receiver, in fpvOS: it answers
the protocol and calls `fpvos-usb mode` for mode changes. fpvOS builds this
repository as the `fpvos-usb` package.

## Boards

| Board | State |
|---|---|
| BETAFPV P1 Air (AR9311, fpvOS's own 6.18 kernel) | built; not yet run on a unit |
| Ascent VRX (AR9311) | to do: same as the P1 Air, its own hooks |
| Ascent Lite/Lite+ air (Hi3516CV610, stock kernel) | `ascent-lite`, `ascent-lite-plus`: stock's ACM gadget, FunctionFS as a module fpvOS builds; normal, bridge (host upload, frames both ways) and back without a reboot verified on a Lite+; the chip reset by its GPIO7_3; bridge mode kept across reboots (SAVED_MODE in the app volume) |
| VRX Pro goggle (RK3568, chip on USB) | built; normal, bridge and back verified on a unit, the chip loaded by its power GPIOs; no host upload tried yet |

The stock P1 Air kernel (4.9.38) has no FunctionFS: bridge mode needs the
fpvOS kernel (`./build.sh p1-air-source`).

## Build

```
make CROSS_COMPILE=aarch64-buildroot-linux-musl-
make test
```

The tests run on the host: the boot ROM upload put back together, the frame
scanner, and the mode changes and gadget edits against a fake configfs tree.

## License

GPL-2.0-or-later. The bridge started as `arlink-usbchip`, written for the
Skyzone SteadyView ground unit.
