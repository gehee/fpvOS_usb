// SPDX-License-Identifier: GPL-2.0-or-later
//
// The host uploads an image to the boot ROM as 'US' blocks: the image header
// first (to ROM_HDR_ADDR), then the spl, config and firmware sections to their
// chip addresses, then the separate config over the config section, then an
// empty block: run. The blocks are put back into an image file the way the
// chip's memory ends up: header, then each section at its 512-aligned offset.
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "log.h"
#include "rom.h"

#define ALIGN_UP(x) (((x) + IMG_ALIGN - 1) & ~(uint32_t)(IMG_ALIGN - 1))
#define SECTION_MAX (8u << 20)

void rom_reset(struct rom *r)
{
	free(r->img);
	memset(r, 0, sizeof(*r));
}

static int rom_header(struct rom *r, const uint8_t *h, uint32_t len)
{
	if (len != IMG_HDR_SIZE || get_le32(h) != IMG_MAGIC || get_le16(h + 6) != IMG_HDR_SIZE) {
		say("rom: the block at the header address is not an AR8030 image header\n");
		return -1;
	}
	r->sec[0] = (struct rom_section){ "spl",    get_le32(h + 0x20), get_le32(h + 0x24), 0, 0 };
	r->sec[1] = (struct rom_section){ "config", get_le32(h + 0x28), get_le32(h + 0x2c), 0, 0 };
	r->sec[2] = (struct rom_section){ "fw",     get_le32(h + 0x18), get_le32(h + 0x1c), 0, 0 };
	uint32_t off = ALIGN_UP(IMG_HDR_SIZE);
	for (int i = 0; i < 3; i++) {
		if (r->sec[i].len > SECTION_MAX) {
			say("rom: %s section of %u bytes is too big\n", r->sec[i].name, r->sec[i].len);
			return -1;
		}
		r->sec[i].off = off;
		off = ALIGN_UP(off + r->sec[i].len);
	}
	r->size = r->sec[2].off + r->sec[2].len;
	r->img = calloc(1, r->size);
	if (!r->img)
		return -1;
	memcpy(r->img, h, IMG_HDR_SIZE);
	r->have_hdr = 1;
	say("rom: image header: spl %u B @0x%08x, config %u B @0x%08x, fw %u B @0x%08x\n",
	    r->sec[0].len, r->sec[0].addr, r->sec[1].len, r->sec[1].addr, r->sec[2].len, r->sec[2].addr);
	return 0;
}

static void rom_data(struct rom *r, uint32_t addr, const uint8_t *p, uint32_t len)
{
	for (int i = 0; i < 3; i++) {
		struct rom_section *s = &r->sec[i];
		/* Written as two compares so that addr + len cannot wrap. */
		if (addr >= s->addr && len <= s->len && addr - s->addr <= s->len - len) {
			memcpy(r->img + s->off + (addr - s->addr), p, len);
			s->got += len;
			return;
		}
	}
	if (++r->stray <= 5)
		say("rom: %u bytes at 0x%08x are in no section of the image, dropped\n", len, addr);
}

int rom_block(struct rom *r, const uint8_t *b, uint32_t n)
{
	if (n < ROM_BLOCK_HDR || b[0] != 'U' || b[1] != 'S') {
		say("rom: a %u-byte write that is not a 'US' block, ignored\n", n);
		return 0;
	}
	uint32_t len = get_le16(b + 2), addr = get_le32(b + 4);
	if (len != n - ROM_BLOCK_HDR) {
		say("rom: block says %u bytes, carries %u\n", len, n - ROM_BLOCK_HDR);
		return 0;
	}
	if (!len) {
		if (!r->have_hdr) {
			say("rom: run with no image uploaded, ignored\n");
			return 0;
		}
		say("rom: %u blocks; spl %u/%u, config %u/%u (incl. the separate config), fw %u/%u bytes\n",
		    r->blocks, r->sec[0].got, r->sec[0].len, r->sec[1].got, r->sec[1].len,
		    r->sec[2].got, r->sec[2].len);
		return 1;
	}
	if (addr == ROM_HDR_ADDR) {
		/* A new upload: whatever came before is dropped. */
		rom_reset(r);
		r->blocks = 1;
		return rom_header(r, b + ROM_BLOCK_HDR, len);
	}
	r->blocks++;
	if (!r->have_hdr) {
		if (++r->stray <= 5)
			say("rom: data before the image header, dropped\n");
		return 0;
	}
	rom_data(r, addr, b + ROM_BLOCK_HDR, len);
	return 0;
}
