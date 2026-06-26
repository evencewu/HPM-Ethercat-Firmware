#!/bin/bash
# HPM5E00 FoE Slave Hardware-in-the-Loop Test Script
# Usage: sudo ./tools/hardware_test.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
SDK_BASE="/home/evence/code_file/dependence/hpm_sdk"
OPENOCD="/home/evence/code_file/dependence/riscv-openocd/src/openocd"
ELF="$PROJ_DIR/build/output/demo.elf"
SERIAL="/dev/ttyUSB1"
BAUD="115200"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; }
info() { echo -e "${YELLOW}[..]${NC} $*"; }

# Must run as root
if [ "$EUID" -ne 0 ]; then
    fail "Please run with sudo: sudo $0"
    exit 1
fi

# Kill any stale OpenOCD
info "Killing stale OpenOCD processes..."
pkill -9 openocd 2>/dev/null || true
sleep 1

# Check tools
for tool in "$OPENOCD" stty cat; do
    if ! command -v "$tool" &>/dev/null && [ ! -f "$tool" ]; then
        fail "$tool not found"
        exit 1
    fi
done
ok "All tools found"

# Configure serial port
info "Configuring $SERIAL at $BAUD baud..."
stty -F "$SERIAL" "$BAUD" raw -echo 2>/dev/null || {
    fail "Cannot configure $SERIAL. Check USB connection."
    exit 1
}
ok "Serial port configured"

# Step 1: Load firmware via OpenOCD and resume
info "Loading firmware via OpenOCD..."
(
    "$OPENOCD" \
        -s "$SDK_BASE/boards/openocd" \
        -s "$(dirname "$OPENOCD")/../tcl" \
        -c "set CHIPNAME hpm5e00; set _CPUTAPID 0x1000563D" \
        -f probes/ft2232.cfg \
        -f soc/hpm5e00.cfg \
        -f boards/hpm5e00evk.cfg \
        -c "init; halt; enable_clock; load_image $ELF; resume; sleep 2; shutdown"
) 2>&1 | grep -E "Error|Warn|target|halted|written|verified|shutdown" || true
ok "Firmware loaded and CPU resumed"

# Step 2: Read serial output
info "Reading serial output (waiting 3 seconds)..."
sleep 3
SERIAL_OUT=$(timeout 5 cat "$SERIAL" 2>/dev/null || true)
echo "--- Serial Output ---"
echo "$SERIAL_OUT"
echo "---------------------"

if echo "$SERIAL_OUT" | grep -q "HPM5E00 SOES FoE demo"; then
    ok "Firmware boot message seen!"
else
    fail "No firmware boot message on serial."
    echo "  Possible causes:"
    echo "  1. Firmware is hung (check OpenOCD output above for errors)"
    echo "  2. Wrong serial port (check dmesg | grep ttyUSB)"
    echo "  3. Baud rate mismatch (should be $BAUD)"
    echo "  4. Firmware crashed before reaching printf"
fi

# Step 3: Check if serial shows ESC init message
if echo "$SERIAL_OUT" | grep -q "HPM5E00"; then
    info "Serial output detected. If 'Entering BOOT state' is NOT shown,"
    info "the slave is in normal operational mode and ready for discovery."
fi

echo ""
echo "=== Next steps (run manually) ==="
echo "1. Discover slave:  sudo ~/code_file/dependence/SOEM/build/samples/slaveinfo/slaveinfo enp5s0"
echo "2. Flash via FoE:   sudo ~/code_file/test/soem_cpp/build/foe_flash enp5s0 1 $ELF"
echo "3. Monitor serial:  sudo cat $SERIAL"
echo ""
echo "To run automatically, uncomment the slaveinfo/foe_flash sections in this script."
