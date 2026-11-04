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

int gadget_unbind(const struct conf *c);
int gadget_bind(const struct conf *c);
/* The device's identity; only while unbound. */
int gadget_ids(const struct conf *c, uint16_t vid, uint16_t pid, const char *product);
/* Unlinks every function from the configuration but KEEP. */
int gadget_only_keep(const struct conf *c);
/* The FunctionFS function, linked and mounted at FFS_DIR; and taken away. */
int gadget_ffs_add(const struct conf *c);
void gadget_ffs_remove(const struct conf *c);

#endif
