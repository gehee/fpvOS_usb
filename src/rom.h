/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The AR8030 boot ROM's USB upload, put back together as an image file. */
#ifndef FPVOS_USB_ROM_H
#define FPVOS_USB_ROM_H
#include <stdint.h>

#define ROM_REQ        0x0c          /* vendor OUT control request carrying one block */
#define ROM_BLOCK_HDR  12            /* 'U' 'S' | len u16 | addr u32 | 0 u32 */
#define ROM_BLOCK_MAX  (4096 + 64)
#define ROM_HDR_ADDR   0x002f0040u
#define IMG_MAGIC      0x41528030u
#define IMG_HDR_SIZE   64
#define IMG_ALIGN      512

struct rom_section { const char *name; uint32_t addr, len, off, got; };

struct rom {
	uint8_t *img;
	uint32_t size;
	struct rom_section sec[3];      /* spl, config, fw: in image order */
	int have_hdr;
	unsigned blocks, stray;
};

void rom_reset(struct rom *r);
/* One block as the host sent it. 1: the host said run and the image is
 * complete in r->img (r->size bytes); 0: more to come; -1: a bad header. */
int rom_block(struct rom *r, const uint8_t *b, uint32_t n);

#endif
