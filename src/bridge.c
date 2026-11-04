// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bridge mode: this unit's AR8030 (on SDIO, behind arlink.ko) shows up on USB
// as an AR8030 on USB, for a host that runs ar_libre's arlink.ko. The serial
// function stays in the gadget throughout, so the mode can be changed again.
//
//   1. Boot ROM: the gadget takes the ROM's ID 4152:8030 with one vendor
//      interface. The host uploads its firmware as vendor control writes, one
//      'U' 'S' block each (rom.c puts the image back together).
//   2. The vendor interface leaves, and the board's chip-load hook reloads
//      this unit's AR8030 with that image.
//   3. Running: the gadget comes back as 1d6b:8030, a vendor interface with a
//      bulk endpoint each way carrying the host protocol's frames to and from
//      CHIP_DEV unchanged.
//
// RAM only: nothing is written to flash. The uploaded image is the host's
// choice and is used as is: it has to be a build the chip can run over SDIO.
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>

#include "conf.h"
#include "frame.h"
#include "gadget.h"
#include "log.h"
#include "rom.h"
#include "usbdesc.h"

#define FW_NAME "usb-upload.img"

static int ep0 = -1;

/* ---- FunctionFS descriptors ----------------------------------------------- */

struct __attribute__((packed)) descs_rom {
	struct usb_functionfs_descs_head_v2 head;
	uint32_t fs_count, hs_count;
	struct usb_interface_descriptor fs_intf, hs_intf;
};

struct __attribute__((packed)) descs_run {
	struct usb_functionfs_descs_head_v2 head;
	uint32_t fs_count, hs_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio in, out;
	} __attribute__((packed)) fs, hs;
};

static struct usb_interface_descriptor vendor_intf(int neps)
{
	struct usb_interface_descriptor d = {
		.bLength = sizeof(d), .bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 0, .bNumEndpoints = (uint8_t)neps,
		.bInterfaceClass = USB_CLASS_VENDOR_SPEC, .iInterface = 1,
	};
	return d;
}

static struct usb_endpoint_descriptor_no_audio bulk_ep(uint8_t addr, uint16_t maxp)
{
	struct usb_endpoint_descriptor_no_audio d = {
		.bLength = sizeof(d), .bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = addr, .bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = htole16(maxp),
	};
	return d;
}

static int ffs_start(const struct conf *c, int running)
{
	ep0 = open(FFS_DIR "/ep0", O_RDWR | O_CLOEXEC);
	if (ep0 < 0) {
		say("open ep0: %s\n", strerror(errno));
		return -1;
	}
	/* ALL_CTRL_RECIP: the ROM's blocks are requests to the device, not to
	 * our interface, and the composite device has other functions. */
	uint32_t flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_ALL_CTRL_RECIP;
	ssize_t w;
	if (!running) {
		struct descs_rom d = {
			.head = { htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2), htole32(sizeof(d)), htole32(flags) },
			.fs_count = htole32(1), .hs_count = htole32(1),
		};
		d.fs_intf = d.hs_intf = vendor_intf(0);
		w = write(ep0, &d, sizeof(d));
	} else {
		struct descs_run d = {
			.head = { htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2), htole32(sizeof(d)), htole32(flags) },
			.fs_count = htole32(3), .hs_count = htole32(3),
		};
		d.fs.intf = d.hs.intf = vendor_intf(2);
		d.fs.in = bulk_ep(USB_DIR_IN | 1, 64);
		d.fs.out = bulk_ep(USB_DIR_OUT | 1, 64);
		d.hs.in = bulk_ep(USB_DIR_IN | 1, 512);
		d.hs.out = bulk_ep(USB_DIR_OUT | 1, 512);
		w = write(ep0, &d, sizeof(d));
	}
	if (w < 0) {
		say("descriptors: %s\n", strerror(errno));
		return -1;
	}
	uint8_t s[128];
	size_t sn = ffs_strings(s, sizeof(s), c->board);
	if (!sn || write(ep0, s, sn) < 0) {
		say("strings: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static void ffs_stop(const struct conf *c)
{
	if (ep0 >= 0)
		close(ep0);
	ep0 = -1;
	gadget_ffs_remove(c);
}

/* Refuse a control request: the opposite-direction transfer on ep0 halts it. */
static void ep0_stall(const struct usb_ctrlrequest *c)
{
	if (c->bRequestType & USB_DIR_IN) {
		if (read(ep0, NULL, 0) < 0 && errno != EL2HLT)
			say("stall: %s\n", strerror(errno));
	} else {
		if (write(ep0, NULL, 0) < 0 && errno != EL2HLT)
			say("stall: %s\n", strerror(errno));
	}
}

static const char *ev_name(int t)
{
	static const char *n[] = { "BIND", "UNBIND", "ENABLE", "DISABLE", "SETUP", "SUSPEND", "RESUME" };
	return t >= 0 && t < 7 ? n[t] : "?";
}

/* Brings the function up under the given identity. */
static int present(const struct conf *c, int running, uint16_t vid, uint16_t pid, const char *product)
{
	if (gadget_unbind(c) || gadget_ffs_add(c) || gadget_ids(c, vid, pid, product) ||
	    ffs_start(c, running))
		return -1;
	return 0;
}

/* ---- phase 1: the boot ROM ------------------------------------------------- */

static int run_rom(const struct conf *c, const char *image)
{
	static uint8_t buf[ROM_BLOCK_MAX];
	struct rom r = {0};

	if (present(c, 0, 0x4152, 0x8030, "AR8030 boot ROM") || gadget_bind(c))
		return -1;
	say("rom: waiting for the host's upload (4152:8030)\n");
	for (;;) {
		struct usb_functionfs_event ev;
		ssize_t n = read(ep0, &ev, sizeof(ev));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			say("ep0: %s\n", strerror(errno));
			return -1;
		}
		if (ev.type != FUNCTIONFS_SETUP) {
			say("rom: %s\n", ev_name(ev.type));
			continue;
		}
		const struct usb_ctrlrequest *s = &ev.u.setup;
		uint16_t wlen = le16toh(s->wLength);
		if (s->bRequestType != (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE) ||
		    s->bRequest != ROM_REQ || wlen > sizeof(buf)) {
			say("rom: request type 0x%02x req 0x%02x len %u refused\n",
			    s->bRequestType, s->bRequest, wlen);
			ep0_stall(s);
			continue;
		}
		n = read(ep0, buf, wlen);       /* the data stage, and the status stage after */
		if (n < 0) {
			say("rom: block data: %s\n", strerror(errno));
			continue;
		}
		if (rom_block(&r, buf, (uint32_t)n) != 1)
			continue;
		FILE *f = fopen(image, "wb");
		size_t w = f ? fwrite(r.img, 1, r.size, f) : 0;
		if (!f || fclose(f) || w != r.size) {
			say("rom: cannot write %s\n", image);
			rom_reset(&r);
			return -1;
		}
		say("rom: %s (%u B)\n", image, r.size);
		rom_reset(&r);
		return 0;
	}
}

/* ---- phase 2: load this unit's chip ---------------------------------------- */

static int run_hook(const char *name, const char *a1, const char *a2)
{
	char path[256];
	hook_path(path, sizeof(path), name);
	say("hook: %s %s %s\n", path, a1 ? a1 : "", a2 ? a2 : "");
	pid_t p = fork();
	if (p < 0)
		return -1;
	if (!p) {
		execl(path, path, a1, a2, (char *)NULL);
		_exit(127);
	}
	int st;
	while (waitpid(p, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	if (!WIFEXITED(st) || WEXITSTATUS(st)) {
		say("hook: %s failed (%d)\n", name, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		return -1;
	}
	return 0;
}

/* ---- phase 3: the running chip --------------------------------------------- */

static int chip = -1, ep_in = -1, ep_out = -1;
static volatile unsigned long to_chip_frames, to_chip_bytes, to_host_frames, to_host_bytes, resyncs;

static void to_chip(const uint8_t *f, size_t len, void *arg)
{
	(void)arg;
	if (write(chip, f, len) != (ssize_t)len)
		say("chip write: %s\n", strerror(errno));
	to_chip_frames++;
	to_chip_bytes += len;
}

static void *host_to_chip(void *arg)
{
	(void)arg;
	static uint8_t buf[2 * FRAME_MAX];
	size_t have = 0;
	for (;;) {
		ssize_t n = read(ep_out, buf + have, sizeof(buf) - have);
		if (n < 0) {
			if (errno != EINTR && errno != ESHUTDOWN)
				say("usb out: %s\n", strerror(errno));
			have = 0;       /* a frame cut by a disconnect is gone */
			usleep(20000);
			continue;
		}
		have += (size_t)n;
		unsigned long r = resyncs;
		size_t used = frame_scan(buf, have, &r, to_chip, NULL);
		resyncs = r;
		memmove(buf, buf + used, have - used);
		have -= used;
		if (have == sizeof(buf))
			have = 0;       /* garbage filling the buffer */
	}
	return NULL;
}

static void *chip_to_host(void *arg)
{
	(void)arg;
	static uint8_t buf[FRAME_MAX];
	for (;;) {
		ssize_t n = read(chip, buf, sizeof(buf));
		if (n <= 0) {
			if (n < 0 && errno != EINTR)
				say("chip read: %s\n", strerror(errno));
			usleep(10000);
			continue;
		}
		/* Blocks while the host has not configured us; the chip's queue drops meanwhile. */
		while (write(ep_in, buf, (size_t)n) < 0) {
			if (errno != EINTR && errno != ESHUTDOWN)
				say("usb in: %s\n", strerror(errno));
			usleep(20000);
		}
		/* A transfer that is a whole number of packets needs a zero-length one
		 * to end it, or the host's URB waits for the next frame. */
		if ((n & 511) == 0 && write(ep_in, buf, 0) < 0 && errno != ESHUTDOWN)
			say("usb in zlp: %s\n", strerror(errno));
		to_host_frames++;
		to_host_bytes += (unsigned long)n;
	}
	return NULL;
}

static void *stats(void *arg)
{
	(void)arg;
	for (;;) {
		sleep(5);
		say("run: to chip %lu frames %lu B, to host %lu frames %lu B, resyncs %lu\n",
		    to_chip_frames, to_chip_bytes, to_host_frames, to_host_bytes, resyncs);
	}
	return NULL;
}

static int run_chip(const struct conf *c)
{
	for (int i = 0; i < 100 && chip < 0; i++) {
		chip = open(c->chip_dev, O_RDWR | O_CLOEXEC);
		if (chip < 0)
			usleep(100000);
	}
	if (chip < 0) {
		say("open %s: %s\n", c->chip_dev, strerror(errno));
		return -1;
	}
	if (present(c, 1, 0x1d6b, 0x8030, "AR8030"))
		return -1;
	ep_in = open(FFS_DIR "/ep1", O_RDWR | O_CLOEXEC);
	ep_out = open(FFS_DIR "/ep2", O_RDWR | O_CLOEXEC);
	if (ep_in < 0 || ep_out < 0) {
		say("open bulk endpoints: %s\n", strerror(errno));
		return -1;
	}
	if (gadget_bind(c))
		return -1;
	say("run: presenting the running chip (1d6b:8030)\n");

	pthread_t t;
	pthread_create(&t, NULL, host_to_chip, NULL);
	pthread_create(&t, NULL, chip_to_host, NULL);
	pthread_create(&t, NULL, stats, NULL);
	for (;;) {
		struct usb_functionfs_event ev;
		ssize_t r = read(ep0, &ev, sizeof(ev));
		if (r < 0) {
			if (errno == EINTR)
				continue;
			say("ep0: %s\n", strerror(errno));
			return -1;
		}
		if (ev.type == FUNCTIONFS_SETUP) {
			const struct usb_ctrlrequest *s = &ev.u.setup;
			say("run: request type 0x%02x req 0x%02x refused\n", s->bRequestType, s->bRequest);
			ep0_stall(s);
		} else {
			say("run: %s\n", ev_name(ev.type));
		}
	}
}

/* The serial port alone, under a neutral identity: so that no host driver
 * takes the device for an AR8030 while it has none to offer. */
static void serial_present(const struct conf *c)
{
	ffs_stop(c);
	gadget_unbind(c);
	gadget_ids(c, 0x1d6b, 0x0104, "fpvOS serial");
	gadget_bind(c);
}

/* Whatever went wrong, the unit's own USB set (network and serial port) comes
 * back, so its log can be read and the mode changed (normal reboots it). */
static int failed(const struct conf *c, const struct gadget_saved *saved)
{
	ffs_stop(c);
	gadget_unbind(c);
	if (gadget_restore(c, saved))
		say("bridge: the USB network could not be put back\n");
	gadget_bind(c);
	say("bridge: failed; the unit's own USB set is back (normal reboots it)\n");
	return 1;
}

static void run_file(char *out, size_t size, const char *name)
{
	snprintf(out, size, "%s/%s", run_dir(), name);
}

int bridge_main(const struct conf *c)
{
	struct gadget_saved saved = {0};
	char path[300];
	FILE *pid;
	char fw_dir[256], image[300];
	snprintf(fw_dir, sizeof(fw_dir), "%s/fw", run_dir());
	snprintf(image, sizeof(image), "%s/" FW_NAME, fw_dir);
	mkdir(fw_dir, 0700);
	unlink(image);

	run_file(path, sizeof(path), "bridge.pid");
	if ((pid = fopen(path, "w"))) {
		fprintf(pid, "%d\n", (int)getpid());
		fclose(pid);
	}
	gadget_save(c, &saved);
	if (gadget_unbind(c) || gadget_only_keep(c, &saved))
		return failed(c, &saved);
	/* For the way back (fpvos-usb unbridge). */
	run_file(path, sizeof(path), "gadget.saved");
	if (gadget_saved_write(path, &saved) || run_rom(c, image))
		return failed(c, &saved);
	/* The boot ROM leaves the bus while this unit's chip loads. */
	serial_present(c);
	if (run_hook("chip-load", fw_dir, FW_NAME))
		return failed(c, &saved);
	run_chip(c);
	return failed(c, &saved);
}

/* The way back from bridge mode, live: the bridge stops, the unit's own USB
 * set comes back, and the board's radio-start hook puts the unit's own
 * firmware back in its chip and starts what uses it. Anything failing: the
 * unit reboots instead. */
int unbridge_main(const struct conf *c, int (*hook)(const char *name))
{
	struct gadget_saved saved;
	char path[300];
	run_file(path, sizeof(path), "bridge.pid");
	FILE *f = fopen(path, "r");
	int pid = 0;
	if (f) {
		if (fscanf(f, "%d", &pid) != 1)
			pid = 0;
		fclose(f);
	}
	if (pid > 1 && !kill(pid, SIGTERM)) {
		for (int i = 0; i < 50 && !kill(pid, 0); i++)
			usleep(100000);
		if (!kill(pid, 0)) {
			kill(pid, SIGKILL);
			usleep(200000);
		}
		say("unbridge: bridge %d stopped\n", pid);
	}
	unlink(path);
	run_file(path, sizeof(path), "gadget.saved");
	if (gadget_saved_read(path, &saved)) {
		say("unbridge: no saved USB set\n");
		return 1;
	}
	gadget_unbind(c);
	gadget_ffs_remove(c);
	if (gadget_restore(c, &saved) || gadget_bind(c)) {
		say("unbridge: the USB set could not be put back\n");
		return 1;
	}
	say("unbridge: the unit's own USB set is back\n");
	/* Required: without it the chip would keep the host's firmware. */
	hook_path(path, sizeof(path), "radio-start");
	if (access(path, X_OK) || hook("radio-start")) {
		say("unbridge: radio-start failed\n");
		return 1;
	}
	run_file(path, sizeof(path), "gadget.saved");
	unlink(path);
	say("unbridge: normal mode\n");
	return 0;
}
