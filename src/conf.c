// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"
#include "log.h"

static const char *env_or(const char *name, const char *fallback)
{
	const char *v = getenv(name);
	return v && *v ? v : fallback;
}

const char *run_dir(void)
{
	return env_or("FPVOS_USB_RUN", "/run/fpvos-usb");
}

void hook_path(char *out, unsigned size, const char *name)
{
	snprintf(out, size, "%s/%s", env_or("FPVOS_USB_LIBEXEC", "/usr/libexec/fpvos-usb"), name);
}

static void set(char *dst, const char *v)
{
	size_t n = strlen(v);
	if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
		v++;
		n -= 2;
	}
	if (n > 63)
		n = 63;
	memcpy(dst, v, n);
	dst[n] = 0;
}

int board_id_valid(const char *id)
{
	size_t n = strlen(id);
	if (!n || n > 31 || id[0] == '-')
		return 0;
	for (size_t i = 0; i < n; i++)
		if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-'))
			return 0;
	return 1;
}

int conf_load(struct conf *c)
{
	const char *path = env_or("FPVOS_USB_CONF", "/etc/default/fpvos-usb");
	FILE *f = fopen(path, "r");
	memset(c, 0, sizeof(*c));
	strcpy(c->chip_dev, "/dev/arlink0");
	if (!f) {
		say("%s: cannot read\n", path);
		return -1;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (line[0] == '#' || !eq)
			continue;
		*eq = 0;
		const char *k = line, *v = eq + 1;
		if (!strcmp(k, "GADGET")) set(c->gadget, v);
		else if (!strcmp(k, "UDC")) set(c->udc, v);
		else if (!strcmp(k, "KEEP")) set(c->keep, v);
		else if (!strcmp(k, "CHIP_DEV")) set(c->chip_dev, v);
		else if (!strcmp(k, "USB_SERIAL")) set(c->serial, v);
		else if (!strcmp(k, "PRODUCT")) set(c->product, v);
		else if (!strcmp(k, "SAVED_MODE")) set(c->saved, v);
		else if (!strcmp(k, "BOARD")) {
			char b[64];
			set(b, v);
			if (board_id_valid(b))  /* at most 31 characters */
				memcpy(c->board, b, strlen(b) + 1);
		}
	}
	fclose(f);
	/* Names end up in paths: no way out of the gadget's own directory. */
	if (!c->gadget[0] || !c->udc[0] || !c->keep[0] || strchr(c->gadget, '/') ||
	    strchr(c->keep, '/') || strstr(c->gadget, "..") || strstr(c->keep, "..")) {
		say("%s: GADGET, UDC and KEEP must be set, as plain names\n", path);
		return -1;
	}
	if (!c->board[0]) {
		say("%s: BOARD must be set to a board id (a-z, 0-9, -)\n", path);
		return -1;
	}
	return 0;
}

const char *bridge_product(const struct conf *c, const char *fallback)
{
	static char name[80];
	if (!c->product[0])
		return fallback;
	snprintf(name, sizeof(name), "%s [bridge]", c->product);
	return name;
}
