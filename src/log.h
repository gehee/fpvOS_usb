/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FPVOS_USB_LOG_H
#define FPVOS_USB_LOG_H
#include <stdio.h>

/* Seconds since the program started, then the message, on stdout. */
double log_elapsed(void);
#define say(...) do { printf("%8.3f ", log_elapsed()); printf(__VA_ARGS__); fflush(stdout); } while (0)

#endif
