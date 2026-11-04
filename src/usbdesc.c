// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <string.h>
#include <linux/usb/functionfs.h>

#include "usbdesc.h"

static void put_le32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

size_t ffs_strings(uint8_t *out, size_t size, const char *board)
{
	char text[64];
	int n = snprintf(text, sizeof(text), BOARD_PREFIX "%s", board);
	/* head (magic, length, strings, languages), the language, the string */
	size_t len = 16 + 2 + (size_t)n + 1;
	if (n < 0 || (size_t)n >= sizeof(text) || len > size)
		return 0;
	put_le32(out, FUNCTIONFS_STRINGS_MAGIC);
	put_le32(out + 4, (uint32_t)len);
	put_le32(out + 8, 1);
	put_le32(out + 12, 1);
	out[16] = 0x09;
	out[17] = 0x04;
	memcpy(out + 18, text, (size_t)n + 1);
	return len;
}
