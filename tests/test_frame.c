// SPDX-License-Identifier: GPL-2.0-or-later
// Frames found in the host's byte stream: whole, split across reads, after
// garbage, and a false start.
#include <string.h>

#include "check.h"
#include "frame.h"

static size_t make(uint8_t *f, uint32_t plen, uint8_t fill)
{
	memset(f, 0, FRAME_HDR);
	f[0] = 0xAA;
	for (int i = 0; i < 4; i++)
		f[1 + i] = plen >> (8 * i);
	f[5] = fill;
	uint8_t x = 0xff;
	for (int k = 0; k < 17; k++)
		x ^= f[k];
	f[17] = x;
	memset(f + FRAME_HDR, fill, plen);
	f[FRAME_HDR + plen] = 0xBB;
	return FRAME_HDR + plen + 1;
}

static int got;
static size_t got_len[8];
static uint8_t got_fill[8];

static void emit(const uint8_t *f, size_t len, void *arg)
{
	(void)arg;
	if (got < 8) {
		got_len[got] = len;
		got_fill[got] = f[5];
	}
	got++;
}

int main(void)
{
	static uint8_t buf[4096];
	unsigned long resyncs = 0;
	size_t n = 0;

	buf[n++] = 0x00;                          /* garbage */
	buf[n++] = 0xAA;                          /* a false start */
	n += make(buf + n, 5, 1);
	n += make(buf + n, 0, 2);
	size_t third = make(buf + n, 100, 3);
	size_t used = frame_scan(buf, n + 40, &resyncs, emit, NULL);   /* the third cut short */
	CHECK(got == 2);
	CHECK(got_len[0] == FRAME_HDR + 6 && got_fill[0] == 1);
	CHECK(got_len[1] == FRAME_HDR + 1 && got_fill[1] == 2);
	CHECK(resyncs == 2);
	CHECK(used == n);                         /* the third frame stays for later */
	used = frame_scan(buf + n, third, &resyncs, emit, NULL);
	CHECK(got == 3 && got_fill[2] == 3 && used == third);

	/* A header whose end byte is wrong is not a frame. */
	n = make(buf, 4, 9);
	buf[n - 1] = 0;
	got = 0;
	resyncs = 0;
	frame_scan(buf, n, &resyncs, emit, NULL);
	CHECK(got == 0 && resyncs > 0);

	/* A length past FRAME_MAX is not a frame either. */
	make(buf, 4, 9);
	buf[3] = 0x10;
	buf[17] ^= 0x10;
	got = 0;
	resyncs = 0;
	frame_scan(buf, FRAME_HDR + 5, &resyncs, emit, NULL);
	CHECK(got == 0 && resyncs > 0);
	DONE();
}
