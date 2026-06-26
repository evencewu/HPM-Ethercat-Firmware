#!/bin/bash
# Quick RAM load test - sets PC to entry point before resume
set -e
ELF=/home/evence/code_file/dependence/hpm5e_can/build/output/demo.elf
SDK=/home/evence/code_file/dependence/hpm_sdk
OCD=/home/evence/code_file/dependence/riscv-openocd/src/openocd

pkill -9 openocd 2>/dev/null || true
sleep 1

echo "=== Loading firmware to RAM ==="
$OCD -s $SDK/boards/openocd -s $(dirname $OCD)/../tcl \
  -c "set CHIPNAME hpm5e00; set _CPUTAPID 0x1000563D" \
  -f probes/ft2232.cfg -f soc/hpm5e00.cfg -f boards/hpm5e00evk.cfg \
  -c "init; halt; load_image $ELF; reg pc 0; resume; echo {=== running ===}; shutdown" 2>&1 | tail -10

sleep 2
echo "=== SERIAL OUTPUT ==="
timeout 5 cat /dev/ttyUSB1 2>&1 || echo "(no data - may need more time)"
echo "=== END ==="
echo ""
echo "If still no output, run: sudo cat /dev/ttyUSB1"
echo "while the board is powered and the firmware is running."
