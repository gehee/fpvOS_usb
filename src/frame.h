/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The AR8030 host protocol's frames, found in a byte stream from the host. */
#ifndef FPVOS_USB_FRAME_H
#define FPVOS_USB_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* 0xAA | len u32 | ... | xor of bytes 0-16 with 0xff at byte 17 | payload | 0xBB */
#define FRAME_HDR 18
#define FRAME_MAX (64 * 1024)

/* Calls emit for each whole frame in buf[0..have), skipping bytes that start
 * none (counted in *resyncs). Returns how many bytes were used up: the rest
 * is the start of a frame still to come. */
size_t frame_scan(const uint8_t *buf, size_t have, unsigned long *resyncs,
		  void (*emit)(const uint8_t *frame, size_t len, void *arg), void *arg);

#endif
