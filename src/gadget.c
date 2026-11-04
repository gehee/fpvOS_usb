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
	/* configfs attributes exist already and are written whole; a test tree
	 * (FPVOS_USB_CONFIGFS) has plain files, made here. */
	int flags = getenv("FPVOS_USB_CONFIGFS") ? O_CREAT | O_TRUNC : 0;
	int fd = open(p, O_WRONLY | O_CLOEXEC | flags, 0644);
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

/* An attribute's value, without the newline, in out ("" if unreadable). */
static void get(const struct conf *c, const char *rel, char *out, size_t size)
{
	char p[512];
	path_of(p, sizeof(p), c, rel);
	out[0] = 0;
	int fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	ssize_t n = read(fd, out, size - 1);
	close(fd);
	out[n > 0 ? n : 0] = 0;
	out[strcspn(out, "\n")] = 0;
}

int gadget_unbind(const struct conf *c)
{
	char udc[64];
	/* configfs refuses to unbind a gadget that is not bound (-ENODEV). */
	get(c, "UDC", udc, sizeof(udc));
	if (!udc[0])
		return 0;
	return put(c, "UDC", "\n");
}

static const char *const saved_attrs[GADGET_SAVED] = {
	"idVendor", "idProduct", "bDeviceClass", "bDeviceSubClass", "bDeviceProtocol",
	"strings/0x409/product",
};

void gadget_save(const struct conf *c, struct gadget_saved *s)
{
	for (int i = 0; i < GADGET_SAVED; i++)
		get(c, saved_attrs[i], s->attr[i], sizeof(s->attr[i]));
}

int gadget_restore(const struct conf *c, const struct gadget_saved *s)
{
	char fn[512], link[512];
	int r = 0;
	for (int i = 0; i < GADGET_SAVED; i++)
		if (s->attr[i][0])
			r |= put(c, saved_attrs[i], s->attr[i]);
	for (int i = 0; i < s->unlinked; i++) {
		char rel[160];
		snprintf(rel, sizeof(rel), "functions/%s", s->name[i]);
		path_of(fn, sizeof(fn), c, rel);
		snprintf(rel, sizeof(rel), "configs/c.1/%s", s->name[i]);
		path_of(link, sizeof(link), c, rel);
		if (symlink(fn, link) && errno != EEXIST) {
			say("relink %s: %s\n", s->name[i], strerror(errno));
			r = -1;
		}
	}
	return r;
}

int gadget_bind(const struct conf *c)
{
	char udc[64];
	get(c, "UDC", udc, sizeof(udc));
	if (!strcmp(udc, c->udc))
		return 0;
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

int gadget_saved_write(const char *path, const struct gadget_saved *s)
{
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return -1;
	for (int i = 0; i < GADGET_SAVED; i++)
		fprintf(f, "a%d %s\n", i, s->attr[i]);
	for (int i = 0; i < s->unlinked; i++)
		fprintf(f, "f %s\n", s->name[i]);
	if (fclose(f) || rename(tmp, path))
		return -1;
	return 0;
}

int gadget_saved_read(const char *path, struct gadget_saved *s)
{
	FILE *f = fopen(path, "r");
	char line[160];
	memset(s, 0, sizeof(*s));
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		size_t n;
		if (line[0] == 'a' && line[1] >= '0' && line[1] < '0' + GADGET_SAVED && line[2] == ' ' &&
		    (n = strlen(line + 3)) < sizeof(s->attr[0]))
			memcpy(s->attr[line[1] - '0'], line + 3, n + 1);
		else if (line[0] == 'f' && line[1] == ' ' && s->unlinked < GADGET_FUNCTIONS &&
			 (n = strlen(line + 2)) && n < sizeof(s->name[0]) && !strchr(line + 2, '/'))
			memcpy(s->name[s->unlinked++], line + 2, n + 1);
	}
	fclose(f);
	return 0;
}

int gadget_only_keep(const struct conf *c, struct gadget_saved *s)
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
			size_t n = strlen(e->d_name);
			if (s->unlinked < GADGET_FUNCTIONS && n < sizeof(s->name[0]))
				memcpy(s->name[s->unlinked++], e->d_name, n + 1);
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
