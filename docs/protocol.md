# The serial protocol

Every fpvOS unit has a USB serial port (CDC-ACM) in every mode. A tool on the
host, such as the fpvOS Flasher or `tools/fpvos-update/client.py` in fpvOS,
uses it to read what the unit is, to change its mode and to update it. The
frames are the ones the vendor's PC tool uses (the "OTRA" frames), so one tool
can talk to stock units and fpvOS units alike.

The unit side is fpvos-update's receiver in fpvOS
(`br-external/package/fpvos-update/src/receiver.c`). It calls `fpvos-usb mode`
from this repository for the mode changes.

## Modes

| Mode | On USB, next to the serial port | How to get there |
|---|---|---|
| normal | USB network (CDC-ECM), the unit's address and SSH | at boot; from bridge: a reboot |
| update | USB network; the unit runs from RAM, nothing else running, so it can be flashed | `update` (or `clean`): the unit reboots into it where it has to |
| bridge | its AR8030, as an AR8030 on USB (boot ROM, then the running chip) | `bridge`, from normal; no reboot |

Bridge mode is not kept across a reboot: a unit always boots in normal mode.
Update mode is the updater's clean mode, under its own name in this protocol.

USB identities (vendor:product), with the same serial number in every mode:

| Mode | Identity | Interfaces |
|---|---|---|
| normal, update | the board's own (P1 Air: 1d6b:0104) | ECM (or RNDIS) + ACM |
| bridge, boot ROM | 4152:8030 "AR8030 boot ROM" | ACM + one vendor interface, no endpoints |
| bridge, while the unit's chip loads | 1d6b:0104 "fpvOS serial" | ACM |
| bridge, running chip | 1d6b:8030 "AR8030" | ACM + a vendor interface, bulk in and out |

The serial port leaves the bus for about a second whenever the identity
changes; reopen it. Web Serial keys its permission by identity, so a browser
tool asks for the port again after a change.

## Frames

Little-endian. A 36-byte header, then `length` bytes of payload:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `OTRA` |
| 4 | 2 | version 3292 (`dc 0c`) |
| 6 | 2 | type: 1 request, 2 reply |
| 8 | 2 | message id 0 |
| 10 | 2 | unused, 0xabcd |
| 12 | 4 | command |
| 16 | 2 | format 2 |
| 18 | 2 | user id 0 |
| 20 | 4 | payload length (at most 1 MiB) |
| 24 | 4 | sequence: one more for each request |
| 28 | 4 | retry count |
| 32 | 4 | CRC-32 of the header (this field zero) and the payload |

A reply has the request's header with type 2, its own length and CRC. A
request with a bad CRC gets the reply "CRC mismatch"; any other bad header
closes the port (the unit opens it again).

Replies to commands other than 0x3c and 0x76 carry `status` (s32, 0 = OK)
and a 64-byte text. For 0x74 and 0x75 three u32 come first: the request's
length, the bytes received so far and the file size.

## Commands

| Command | Payload | What it does |
|---|---|---|
| 0x3c find device | none | 300 bytes: max chunk (u32), firmware "development" (32), name (64), installed image hash or a text (64), USB serial (32), hardware (32), then the mode text at 236 (64) |
| 0x76 status | none | progress percent (u32), state (s32), text (64) |
| 3 | 32 bytes: a name, zero padded | one of the names below |
| 0x72 prepare | none | update mode only: ready for a file |
| 0x73 file start | 328 bytes: MD5 hex (32, zero padded to 64), size u32, 1 u32, destination (256) | `/tmp/pc/fpvos.fwu` to install, `/tmp/pc/fpvos.verify` to check only |
| 0x74 file data | the next chunk | a retry with the same sequence is acknowledged once |
| 0x75 file end | none | checks the MD5, then installs or verifies; follow with 0x76 |

Names for command 3:

| Name | What it does |
|---|---|
| `normal` | to normal mode: OK at once if it already is; from bridge, "rebooting", then a reboot |
| `bridge` | to bridge mode: the unit's own radio users stop, "OK", then (1 s later) the USB network leaves and the boot ROM appears; refused in update mode |
| `update` | to update mode (the same as `clean`): "rebooting into clean mode" when the unit has to reboot for it, "OK" when it is already there |
| `reboot` | reboots |
| `stock` | where the board supports it: back to the stock firmware it kept |

The mode text in the 0x3c reply is "normal mode", "bridge mode" or "clean
RAM mode" (update mode); while an update runs it says what the update is
doing.

## Bridge mode, from the host

The host runs ar_libre's `arlink.ko` (USB). It takes the boot ROM, uploads
its firmware and config, and gets `/dev/arlinkN` once the running chip
appears; the unit puts the upload back together, loads its own chip with it
over SDIO and carries the frames both ways unchanged.

The firmware has to be a build the chip can run over SDIO (for Open8030: the
SDIO transport, not `AR_FORCE_USB`). A USB build leaves the chip silent and
the unit has to be power cycled. `arlink.ko` has to match the vendor
interface only (ar_libre 2026-11-04 and later), or it also takes the serial
port's interfaces.

To leave bridge mode, send `normal`: the unit reboots, with its own firmware.
