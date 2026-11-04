# fpvos-usb. CROSS_COMPILE=aarch64-linux- for a unit; `make test` on the host.
CC ?= cc
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Werror -std=gnu11
LDLIBS += -pthread
SRC = src/main.c src/conf.c src/gadget.c src/bridge.c src/rom.c src/frame.c src/usbdesc.c

ifneq ($(CROSS_COMPILE),)
CC := $(CROSS_COMPILE)gcc
endif

all: fpvos-usb

fpvos-usb: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(LDLIBS) -o $@

build/test_%: tests/test_%.c src/rom.c src/frame.c src/usbdesc.c src/conf.c tests/log.c $(wildcard src/*.h)
	@mkdir -p build
	cc -Wall -Wextra -Werror -std=gnu11 -fsanitize=address,undefined -g -Isrc \
		$< src/rom.c src/frame.c src/usbdesc.c src/conf.c tests/log.c -o $@

build/fpvos-usb: $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	cc -Wall -Wextra -Werror -std=gnu11 -g $(SRC) -pthread -o $@

test: build/test_rom build/test_frame build/test_board build/fpvos-usb
	build/test_rom
	build/test_frame
	build/test_board
	sh tests/test_mode.sh build/fpvos-usb

clean:
	rm -rf fpvos-usb build

.PHONY: all test clean
