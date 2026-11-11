// SPDX-License-Identifier: GPL-2.0-or-later
// Board ids: which are accepted, how they reach the host in the interface
// string, and the board settings file.
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/usb/functionfs.h>

#include "check.h"
#include "conf.h"
#include "usbdesc.h"

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int load(const char *text, struct conf *c)
{
	char path[] = "/tmp/fpvos-usb-conf-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text))
		return -2;
	close(fd);
	setenv("FPVOS_USB_CONF", path, 1);
	int r = conf_load(c);
	unlink(path);
	return r;
}

int main(void)
{
	CHECK(board_id_valid("p1-air"));
	CHECK(board_id_valid("ascent-lite-plus"));
	CHECK(board_id_valid("a"));
	CHECK(!board_id_valid(""));
	CHECK(!board_id_valid("-p1"));
	CHECK(!board_id_valid("P1"));
	CHECK(!board_id_valid("p1/air"));
	CHECK(!board_id_valid("p1 air"));
	CHECK(!board_id_valid("0123456789012345678901234567890123"));

	uint8_t b[128];
	size_t n = ffs_strings(b, sizeof(b), "p1-air");
	const char *text = BOARD_PREFIX "p1-air";
	CHECK(n == 16 + 2 + strlen(text) + 1);
	CHECK(le32(b) == FUNCTIONFS_STRINGS_MAGIC && le32(b + 4) == n);
	CHECK(le32(b + 8) == 1 && le32(b + 12) == 1);
	CHECK(b[16] == 0x09 && b[17] == 0x04);
	CHECK(!strcmp((const char *)b + 18, text));
	CHECK(ffs_strings(b, 20, "p1-air") == 0);

	struct conf c;
	CHECK(load("GADGET=fpvos\nUDC=u\nKEEP=acm.usb0\nBOARD=\"p1-air\"\n", &c) == 0);
	CHECK(!strcmp(c.board, "p1-air") && !strcmp(c.chip_dev, "/dev/arlink0"));
	CHECK(!strcmp(bridge_product(&c, "AR8030"), "AR8030"));
	CHECK(load("GADGET=acm\nUDC=u\nKEEP=acm.0\nBOARD=ascent-lite-plus\nPRODUCT=\"Ascent Lite+ Air Unit - fpvOS\"\n", &c) == 0);
	CHECK(!strcmp(bridge_product(&c, "AR8030"), "Ascent Lite+ Air Unit - fpvOS [bridge]"));
	CHECK(load("GADGET=fpvos\nUDC=u\nKEEP=acm.usb0\n", &c) == -1);
	CHECK(load("GADGET=fpvos\nUDC=u\nKEEP=acm.usb0\nBOARD=P1 Air\n", &c) == -1);
	DONE();
}
