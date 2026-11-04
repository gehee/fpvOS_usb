// SPDX-License-Identifier: GPL-2.0-or-later
//
// fpvos-usb: what an fpvOS unit shows on USB. The serial port is there in
// every mode; the updater on it (fpvos-update's receiver) calls `mode` when
// the host asks for another one. See docs/protocol.md.
//
//   fpvos-usb mode             print the mode: normal or bridge
//   fpvos-usb mode bridge      stop the radio's users, then (1 s later, so the
//                              reply gets out) turn the USB network into the
//                              AR8030 bridge; exit 0 once started
//   fpvos-usb mode normal      from bridge: (1 s later) undo the bridge live,
//                              rebooting only if that fails; exit 0 at once
//   fpvos-usb bridge           the bridge itself (what `mode bridge` starts)
//   fpvos-usb unbridge         the way back from it (what `mode normal` starts)
//
// Update mode is the updater's own (its clean mode): not handled here.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "conf.h"
#include "log.h"

int bridge_main(const struct conf *c);
int unbridge_main(const struct conf *c, int (*hook)(const char *name));

static double t0;

double log_elapsed(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9 - t0;
}

static void mode_path(char *out, size_t size)
{
	snprintf(out, size, "%s/mode", run_dir());
}

static void mode_get(char *out, size_t size)
{
	char p[300];
	mode_path(p, sizeof(p));
	snprintf(out, size, "normal");
	FILE *f = fopen(p, "r");
	if (!f)
		return;
	if (fgets(out, (int)size, f))
		out[strcspn(out, "\r\n")] = 0;
	fclose(f);
	if (strcmp(out, "bridge"))
		snprintf(out, size, "normal");
}

static int mode_set(const char *mode)
{
	char p[300], tmp[310];
	mode_path(p, sizeof(p));
	snprintf(tmp, sizeof(tmp), "%s.new", p);
	mkdir(run_dir(), 0700);
	FILE *f = fopen(tmp, "w");
	if (!f || fprintf(f, "%s\n", mode) < 0 || fclose(f) || rename(tmp, p)) {
		perror(p);
		return -1;
	}
	return 0;
}

static int hook(const char *name)
{
	char path[256];
	hook_path(path, sizeof(path), name);
	if (access(path, X_OK))
		return 0;               /* a board with nothing to do there */
	pid_t p = fork();
	if (p < 0)
		return -1;
	if (!p) {
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	int st;
	while (waitpid(p, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

/* `fpvos-usb WHAT`, detached from the caller, 1 s later; its log in the run
 * directory (appended). FPVOS_USB_SELF runs another program instead (tests). */
static int start(const char *what)
{
	char log[300];
	snprintf(log, sizeof(log), "%s/bridge.log", run_dir());
	pid_t p = fork();
	if (p < 0)
		return -1;
	if (p)
		return 0;
	setsid();
	int fd = open(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	int null = open("/dev/null", O_RDONLY);
	if (fd >= 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
	}
	if (null >= 0)
		dup2(null, STDIN_FILENO);
	if (fork())
		_exit(0);               /* not a child of the caller any more */
	sleep(1);                       /* the caller's reply goes out before the gadget changes */
	const char *test = getenv("FPVOS_USB_SELF");
	if (test && *test)
		execl(test, test, what, (char *)NULL);
	else
		execl("/proc/self/exe", "fpvos-usb", what, (char *)NULL);
	_exit(127);
}

static int mode_main(int argc, char **argv)
{
	char now[32];
	mode_get(now, sizeof(now));
	if (argc < 3) {
		printf("%s\n", now);
		return 0;
	}
	const char *want = argv[2];
	if (!strcmp(want, "normal"))
		return strcmp(now, "bridge") ? 0 : (start("unbridge") ? 10 : 0);
	if (strcmp(want, "bridge")) {
		fprintf(stderr, "fpvos-usb: no mode %s here (normal, bridge)\n", want);
		return 2;
	}
	if (!strcmp(now, "bridge"))
		return 0;
	struct conf c;
	if (conf_load(&c))
		return 1;
	/* The radio is the host's from here: nothing on this unit may use it. */
	if (hook("radio-stop")) {
		fprintf(stderr, "fpvos-usb: radio-stop failed; staying in normal mode\n");
		return 1;
	}
	if (mode_set("bridge") || start("bridge"))
		return 1;
	return 0;
}

int main(int argc, char **argv)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	t0 = t.tv_sec + t.tv_nsec / 1e9;
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 2 && !strcmp(argv[1], "mode"))
		return mode_main(argc, argv);
	if (argc == 2 && !strcmp(argv[1], "bridge")) {
		struct conf c;
		if (conf_load(&c))
			return 1;
		return bridge_main(&c);
	}
	if (argc == 2 && !strcmp(argv[1], "unbridge")) {
		struct conf c;
		say("unbridge: back to normal mode\n");
		if (conf_load(&c) || unbridge_main(&c, hook)) {
			say("unbridge: failed; rebooting\n");
			const char *reboot = getenv("FPVOS_USB_REBOOT");    /* tests */
			reboot = reboot && *reboot ? reboot : "/sbin/reboot";
			execl(reboot, "reboot", (char *)NULL);
			return 1;
		}
		return mode_set("normal") ? 1 : 0;
	}
	fprintf(stderr, "usage: fpvos-usb mode [normal|bridge] | fpvos-usb bridge | fpvos-usb unbridge\n");
	return 2;
}
