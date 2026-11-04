/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); failures++; } } while (0)
#define DONE() do { if (failures) { fprintf(stderr, "%d failed\n", failures); return 1; } puts("ok"); return 0; } while (0)
