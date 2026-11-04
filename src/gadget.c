// SPDX-License-Identifier: GPL-2.0-or-later
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gadget.h"
#include "log.h"

static void path_of(char *out, size_t size, const struct conf *c, const char *rel)
{
	const char *root = getenv("FPVOS_USB_CONFIGFS");
	snprintf(out, size, "%s/usb_gadget/%s%s%s", root && *root ? root : "/sys/kernel/config",
		 c->gadget, *rel ? "/" : "", rel);
}

static int put(const struct conf *c, const char *rel, const char *val)
{
	char p[512];
	path_of(p, sizeof(p), c, rel);
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) {
		say("open %s: %s\n", p, strerror(errno));
		return -1;
	}
	ssize_t w = write(fd, val, strlen(val));
	close(fd);
	if (w < 0) {
		say("write %s=%s: %s\n", p, val, strerror(errno));
		return -1;
	}
	return 0;
}

int gadget_unbind(const struct conf *c)
{
	return put(c, "UDC", "\n");
}

int gadget_bind(const struct conf *c)
{
	say("gadget: binding to %s\n", c->udc);
	return put(c, "UDC", c->udc);
}

int gadget_ids(const struct conf *c, uint16_t vid, uint16_t pid, const char *product)
{
	char v[16];
	int r = 0;
	snprintf(v, sizeof(v), "0x%04x", vid);
	r |= put(c, "idVendor", v);
	snprintf(v, sizeof(v), "0x%04x", pid);
	r |= put(c, "idProduct", v);
	/* Interface association descriptors (the serial port has one): the
	 * class Windows wants to see on a composite device. */
	r |= put(c, "bDeviceClass", "0xef");
	r |= put(c, "bDeviceSubClass", "0x02");
	r |= put(c, "bDeviceProtocol", "0x01");
	r |= put(c, "strings/0x409/product", product);
	return r;
}

int gadget_only_keep(const struct conf *c)
{
	char p[512], link[800];
	path_of(p, sizeof(p), c, "configs/c.1");
	DIR *d = opendir(p);
	if (!d) {
		say("opendir %s: %s\n", p, strerror(errno));
		return -1;
	}
	struct dirent *e;
	int r = 0;
	while ((e = readdir(d))) {
		if (e->d_type != DT_LNK || !strcmp(e->d_name, c->keep))
			continue;
		snprintf(link, sizeof(link), "%s/%s", p, e->d_name);
		if (unlink(link)) {
			say("unlink %s: %s\n", link, strerror(errno));
			r = -1;
		} else {
			say("gadget: %s unlinked\n", e->d_name);
		}
	}
	closedir(d);
	return r;
}

int gadget_ffs_add(const struct conf *c)
{
	char fn[512], link[512];
	path_of(fn, sizeof(fn), c, "functions/ffs." FFS_NAME);
	path_of(link, sizeof(link), c, "configs/c.1/ffs." FFS_NAME);
	if (mkdir(fn, 0755) && errno != EEXIST) {
		say("mkdir %s: %s\n", fn, strerror(errno));
		return -1;
	}
	if (symlink(fn, link) && errno != EEXIST) {
		say("link %s: %s\n", link, strerror(errno));
		return -1;
	}
	if (getenv("FPVOS_USB_CONFIGFS"))
		return 0;                   /* a test tree: nothing to mount */
	if (mkdir(FFS_DIR, 0755) && errno != EEXIST) {
		say("mkdir " FFS_DIR ": %s\n", strerror(errno));
		return -1;
	}
	if (mount(FFS_NAME, FFS_DIR, "functionfs", 0, NULL) && errno != EBUSY) {
		say("mount functionfs: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

void gadget_ffs_remove(const struct conf *c)
{
	char fn[512], link[512];
	path_of(fn, sizeof(fn), c, "functions/ffs." FFS_NAME);
	path_of(link, sizeof(link), c, "configs/c.1/ffs." FFS_NAME);
	if (!getenv("FPVOS_USB_CONFIGFS"))
		umount(FFS_DIR);
	unlink(link);
	rmdir(fn);
}
