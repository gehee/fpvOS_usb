/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Changes to the image's composite gadget: the serial function (KEEP) stays
 * in every mode, the other function is swapped. FPVOS_USB_CONFIGFS moves the
 * configfs root (for tests). */
#ifndef FPVOS_USB_GADGET_H
#define FPVOS_USB_GADGET_H
#include <stdint.h>

#include "conf.h"

#define FFS_NAME "arlink"
#define FFS_DIR  "/dev/ffs-arlink"

/* What the bridge changes, to put back if it fails: the identity and the
 * functions it unlinked (the USB network), so the unit can be reached again. */
#define GADGET_SAVED 6
#define GADGET_FUNCTIONS 4
struct gadget_saved {
	char attr[GADGET_SAVED][64];
	char name[GADGET_FUNCTIONS][64];
	int unlinked;
};
void gadget_save(const struct conf *c, struct gadget_saved *s);
/* Only while unbound. */
int gadget_restore(const struct conf *c, const struct gadget_saved *s);

/* Both are no-ops when already done. */
int gadget_unbind(const struct conf *c);
int gadget_bind(const struct conf *c);
/* The device's identity; only while unbound. */
int gadget_ids(const struct conf *c, uint16_t vid, uint16_t pid, const char *product);
/* Unlinks every function from the configuration but KEEP, noting them in s. */
int gadget_only_keep(const struct conf *c, struct gadget_saved *s);
/* The FunctionFS function, linked and mounted at FFS_DIR; and taken away. */
int gadget_ffs_add(const struct conf *c);
void gadget_ffs_remove(const struct conf *c);

#endif
