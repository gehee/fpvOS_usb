#!/bin/sh
# Mode changes and the gadget edits, against a fake configfs tree and fake
# board hooks. $1: a host build of fpvos-usb.
set -eu
bin=$(realpath "$1")
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
export FPVOS_USB_RUN=$t/run FPVOS_USB_LIBEXEC=$t/libexec FPVOS_USB_CONF=$t/conf \
	FPVOS_USB_CONFIGFS=$t/configfs FPVOS_USB_BRIDGE=$t/bridge
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$t/libexec"
cat > "$t/conf" <<E
# a comment
GADGET="fpvos"
UDC=8000000.usb
KEEP=acm.usb0
BOARD=p1-air
E
printf '#!/bin/sh\necho stopped >> %s/calls\n' "$t" > "$t/libexec/radio-stop"
printf '#!/bin/sh\necho bridge >> %s/calls\n' "$t" > "$t/bridge"
chmod +x "$t/libexec/radio-stop" "$t/bridge"

[ "$("$bin" mode)" = normal ] || fail "starts in normal mode"
"$bin" mode normal || fail "normal in normal mode is nothing to do"
st=0; "$bin" mode update 2>/dev/null || st=$?
[ "$st" = 2 ] || fail "update is the updater's, not a mode here ($st)"

"$bin" mode bridge || fail "bridge starts"
[ "$("$bin" mode)" = bridge ] || fail "mode reads bridge"
i=0; while ! grep -q bridge "$t/calls" 2>/dev/null && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
[ "$(cat "$t/calls")" = "stopped
bridge" ] || fail "radio stopped, then the bridge started: $(cat "$t/calls")"
"$bin" mode bridge || fail "bridge again is nothing to do"
[ "$(wc -l < "$t/calls")" = 2 ] || fail "no second bridge"
st=0; "$bin" mode normal || st=$?
[ "$st" = 10 ] || fail "back to normal means a reboot ($st)"

# A radio-stop that fails keeps the unit in normal mode.
rm -rf "$t/run" "$t/calls"
printf '#!/bin/sh\nexit 1\n' > "$t/libexec/radio-stop"
st=0; "$bin" mode bridge 2>/dev/null || st=$?
[ "$st" = 1 ] && [ "$("$bin" mode)" = normal ] || fail "radio-stop failure stays normal"

# Bad names never become paths.
printf 'GADGET=../x\nUDC=u\nKEEP=acm.usb0\nBOARD=p1-air\n' > "$t/conf"
printf '#!/bin/sh\n' > "$t/libexec/radio-stop"
"$bin" mode bridge >/dev/null 2>&1 && fail "GADGET with a path refused"
# No board id, or one that is not one: no bridge (the host could not pick firmware).
for b in '' 'P1' '../x' '-p1'; do
	printf 'GADGET=fpvos\nUDC=u\nKEEP=acm.usb0\nBOARD=%s\n' "$b" > "$t/conf"
	"$bin" mode bridge >/dev/null 2>&1 && fail "BOARD '$b' refused"
done
[ ! -e "$t/calls" ] || fail "no radio stopped for a refused bridge"

# The bridge on a tree without FunctionFS: it fails, and the unit's own USB
# set (network and serial port, its identity) comes back.
printf 'GADGET=fpvos\nUDC=8000000.usb\nKEEP=acm.usb0\nBOARD=p1-air\n' > "$t/conf"
g=$t/configfs/usb_gadget/fpvos
mkdir -p "$g/configs/c.1" "$g/functions/ecm.usb0" "$g/functions/acm.usb0" "$g/strings/0x409"
ln -s "$g/functions/ecm.usb0" "$g/configs/c.1/ecm.usb0"
ln -s "$g/functions/acm.usb0" "$g/configs/c.1/acm.usb0"
printf '0x1d6b\n' > "$g/idVendor"; printf '0x0104\n' > "$g/idProduct"
printf 'P1 Air development network\n' > "$g/strings/0x409/product"
: > "$g/UDC"
"$bin" bridge > "$t/bridge.log" 2>&1 && fail "bridge without FunctionFS fails"
[ "$(ls "$g/configs/c.1" | tr '\n' ' ')" = "acm.usb0 ecm.usb0 " ] || fail "network and serial port linked: $(ls "$g/configs/c.1")"
grep -q 'ecm.usb0 unlinked' "$t/bridge.log" || fail "the network was taken away first"
[ ! -e "$g/functions/ffs.arlink" ] || fail "FunctionFS function removed"
[ "$(cat "$g/UDC")" = 8000000.usb ] || fail "bound again"
[ "$(cat "$g/idVendor"):$(cat "$g/idProduct")" = 0x1d6b:0x0104 ] || fail "own identity back"
[ "$(cat "$g/strings/0x409/product")" = "P1 Air development network" ] || fail "own product back"
echo ok
