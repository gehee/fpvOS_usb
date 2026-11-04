/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FPVOS_USB_BYTES_H
#define FPVOS_USB_BYTES_H
#include <stdint.h>

static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t get_le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

#endif
