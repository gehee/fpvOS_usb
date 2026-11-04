/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The bridge's interface string, which carries the board id to the host. */
#ifndef FPVOS_USB_USBDESC_H
#define FPVOS_USB_USBDESC_H
#include <stddef.h>
#include <stdint.h>

/* "arlink board=<id>": what the host reads as the vendor interface's string
 * (sysfs .../interface), in both the boot ROM and the running chip. */
#define BOARD_PREFIX "arlink board="

/* The FunctionFS strings blob (one string, US English) for that interface
 * string, in out. Returns its length, 0 if out is too small. */
size_t ffs_strings(uint8_t *out, size_t size, const char *board);

#endif
