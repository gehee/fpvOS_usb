// SPDX-License-Identifier: GPL-2.0-or-later
// The boot ROM upload put back together: section placement, the separate
// config over the image's own, a new header starting over, bad blocks.
#include <string.h>

#include "check.h"
#include "rom.h"

static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }

static uint32_t block(uint8_t *b, uint32_t addr, const void *data, uint16_t len)
{
	b[0] = 'U';
	b[1] = 'S';
	le16(b + 2, len);
	le32(b + 4, addr);
	le32(b + 8, 0);
	if (len)
		memcpy(b + ROM_BLOCK_HDR, data, len);
	return ROM_BLOCK_HDR + len;
}

enum { SPL = 0x100000, CFG = 0x200000, FW = 0x300000 };

static void header(uint8_t *h, uint32_t spl, uint32_t cfg, uint32_t fw)
{
	memset(h, 0, IMG_HDR_SIZE);
	le32(h, IMG_MAGIC);
	le16(h + 6, IMG_HDR_SIZE);
	le32(h + 0x18, FW);  le32(h + 0x1c, fw);
	le32(h + 0x20, SPL); le32(h + 0x24, spl);
	le32(h + 0x28, CFG); le32(h + 0x2c, cfg);
}

int main(void)
{
	static uint8_t b[ROM_BLOCK_MAX], h[IMG_HDR_SIZE], data[2048];
	struct rom r = {0};

	header(h, 700, 100, 1500);
	CHECK(rom_block(&r, b, block(b, ROM_HDR_ADDR, h, IMG_HDR_SIZE)) == 0);
	CHECK(r.have_hdr);
	/* header 64 -> 512; spl 700 at 512 -> 1536; config 100 at 1536 -> 2048; fw at 2048 */
	CHECK(r.sec[0].off == 512 && r.sec[1].off == 1536 && r.sec[2].off == 2048);
	CHECK(r.size == 2048 + 1500);

	memset(data, 0x11, sizeof(data));
	CHECK(rom_block(&r, b, block(b, SPL, data, 700)) == 0);
	memset(data, 0x22, sizeof(data));
	CHECK(rom_block(&r, b, block(b, CFG, data, 100)) == 0);
	memset(data, 0x33, sizeof(data));
	CHECK(rom_block(&r, b, block(b, FW, data, 1000)) == 0);
	CHECK(rom_block(&r, b, block(b, FW + 1000, data, 500)) == 0);
	/* The separate config lands over the image's own. */
	memset(data, 0x44, sizeof(data));
	CHECK(rom_block(&r, b, block(b, CFG + 10, data, 20)) == 0);
	/* Past the end of a section, and wrapping around: dropped. */
	CHECK(rom_block(&r, b, block(b, FW + 1400, data, 200)) == 0);
	CHECK(rom_block(&r, b, block(b, 0xffffff00u, data, 0x200)) == 0);
	CHECK(r.stray == 2);
	/* Not a block, a length that lies: ignored. */
	CHECK(rom_block(&r, (const uint8_t *)"XX", 2) == 0);
	block(b, FW, data, 10);
	CHECK(rom_block(&r, b, ROM_BLOCK_HDR + 9) == 0);

	CHECK(rom_block(&r, b, block(b, 0, NULL, 0)) == 1);
	CHECK(!memcmp(r.img, h, IMG_HDR_SIZE));
	CHECK(r.img[512] == 0x11 && r.img[512 + 699] == 0x11 && r.img[1536 - 1] == 0);
	CHECK(r.img[1536] == 0x22 && r.img[1536 + 10] == 0x44 && r.img[1536 + 29] == 0x44 &&
	      r.img[1536 + 30] == 0x22);
	CHECK(r.img[2048] == 0x33 && r.img[2048 + 1499] == 0x33);
	CHECK(r.sec[0].got == 700 && r.sec[2].got == 1500);

	/* A second upload starts over. */
	header(h, 10, 10, 10);
	CHECK(rom_block(&r, b, block(b, ROM_HDR_ADDR, h, IMG_HDR_SIZE)) == 0);
	CHECK(r.blocks == 1 && r.sec[2].got == 0 && r.size == 1536 + 10);

	/* Not an image header; a section too big; run before any header. */
	h[0] ^= 1;
	CHECK(rom_block(&r, b, block(b, ROM_HDR_ADDR, h, IMG_HDR_SIZE)) == -1);
	CHECK(!r.have_hdr);
	CHECK(rom_block(&r, b, block(b, 0, NULL, 0)) == 0);
	header(h, 9u << 20, 10, 10);
	CHECK(rom_block(&r, b, block(b, ROM_HDR_ADDR, h, IMG_HDR_SIZE)) == -1);
	rom_reset(&r);
	DONE();
}
