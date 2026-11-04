// SPDX-License-Identifier: GPL-2.0-or-later
#include "bytes.h"
#include "frame.h"

size_t frame_scan(const uint8_t *buf, size_t have, unsigned long *resyncs,
		  void (*emit)(const uint8_t *frame, size_t len, void *arg), void *arg)
{
	size_t i = 0;
	while (have - i >= FRAME_HDR) {
		const uint8_t *f = buf + i;
		uint8_t x = 0xff;
		for (int k = 0; k < 17; k++)
			x ^= f[k];
		uint32_t plen = get_le32(f + 1);
		if (f[0] != 0xAA || f[17] != x || plen > FRAME_MAX - FRAME_HDR - 1) {
			i++;            /* not a frame start: look further */
			(*resyncs)++;
			continue;
		}
		size_t total = FRAME_HDR + plen + 1;
		if (have - i < total)
			break;
		if (f[total - 1] != 0xBB) {
			i++;
			(*resyncs)++;
			continue;
		}
		emit(f, total, arg);
		i += total;
	}
	return i;
}
