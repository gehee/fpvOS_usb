/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The board's settings: /etc/default/fpvos-usb, KEY=VALUE lines a shell can
 * also source. FPVOS_USB_CONF, FPVOS_USB_RUN and FPVOS_USB_LIBEXEC move the
 * file, the RAM state directory and the board hooks (for tests). */
#ifndef FPVOS_USB_CONF_H
#define FPVOS_USB_CONF_H

struct conf {
	char gadget[64];        /* GADGET: the configfs gadget the image set up */
	char udc[64];           /* UDC: the controller it binds to */
	char keep[64];          /* KEEP: the function every mode keeps (the serial port) */
	char chip_dev[64];      /* CHIP_DEV: this unit's AR8030 (arlink.ko) */
	char serial[64];        /* USB_SERIAL: the same in every mode */
	char board[32];         /* BOARD: the board id the bridge shows the host (docs/protocol.md) */
	char product[64];       /* PRODUCT: the unit's name; in bridge mode "<PRODUCT> (bridge)" */
};

/* A board id: 1-31 of a-z, 0-9 and '-', starting with a letter or digit. */
int board_id_valid(const char *id);

int conf_load(struct conf *c);
/* The USB product string for a bridge stage: "<PRODUCT> (bridge)", or
 * fallback when the board names no product. */
const char *bridge_product(const struct conf *c, const char *fallback);
const char *run_dir(void);
/* The board hook NAME's path (radio-stop, chip-load) in out. */
void hook_path(char *out, unsigned size, const char *name);

#endif
