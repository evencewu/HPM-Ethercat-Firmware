#!/usr/bin/env python3
"""
Closed-loop test runner for the HPM5E00 FoE slave demo.

This script performs all host-side build and validation steps that do not
require actual EtherCAT hardware. It also prints the exact commands needed
for the hardware-in-the-loop phase.

Usage:
    python3 tools/run_closed_loop_test.py
"""

import os
import subprocess
import sys
from pathlib import Path

# Project paths
HPM5E_CAN = Path(__file__).resolve().parent.parent
SOEM_CPP = HPM5E_CAN.parent.parent / "test" / "soem_cpp"
SOEM = HPM5E_CAN.parent.parent / "dependence" / "SOEM"
SOES = HPM5E_CAN.parent.parent / "dependence" / "SOES"

# Environment
HPM_SDK_BASE = HPM5E_CAN.parent.parent / "dependence" / "hpm_sdk"
TOOLCHAIN = HPM5E_CAN.parent.parent / "dependence" / "rv32imac_zicsr_zifencei_multilib_b_ext-linux"


def log_step(step: str) -> None:
    print(f"\n{'=' * 60}", flush=True)
    print(f"STEP: {step}", flush=True)
    print(f"{'=' * 60}", flush=True)


def run(cmd: list[str], cwd: Path, env: dict | None = None) -> int:
    """Run a command and stream output. Return exit code."""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)

    print(f"$ cd {cwd} && {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, cwd=cwd, env=merged_env)
    return result.returncode


def setup_env() -> dict[str, str]:
    """Return environment variables needed for the RISC-V build."""
    env = {}
    if "HPM_SDK_BASE" not in os.environ:
        env["HPM_SDK_BASE"] = str(HPM_SDK_BASE)
    if "GNURISCV_TOOLCHAIN_PATH" not in os.environ:
        env["GNURISCV_TOOLCHAIN_PATH"] = str(TOOLCHAIN)
    return env


def build_hpm5e_can(env: dict[str, str]) -> bool:
    log_step("Building hpm5e_can FoE slave firmware")

    build_dir = HPM5E_CAN / "build"
    if build_dir.exists():
        # Reuse existing build to keep it fast; ninja will detect changes.
        pass

    cmake_cmd = [
        "cmake", "-B", "build", "-S", ".", "-GNinja",
        "-DBOARD=hpm5e00evk",
        "-DCUSTOM_TARGET_TRIPLET=riscv32-unknown-elf",
        "-DRV_ARCH=rv32imac_zicsr_zifencei",
        "-DHPM_BUILD_TYPE=flash_xip",
    ]
    if run(cmake_cmd, HPM5E_CAN, env) != 0:
        return False

    if run(["ninja", "-C", "build"], HPM5E_CAN, env) != 0:
        return False

    expected_elf = HPM5E_CAN / "build" / "output" / "demo.elf"
    expected_bin = HPM5E_CAN / "build" / "output" / "demo.bin"
    if not expected_elf.exists() or not expected_bin.exists():
        print("[FAIL] Expected firmware outputs not found.", file=sys.stderr, flush=True)
        return False

    print(f"[OK] Firmware: {expected_bin} ({expected_bin.stat().st_size} bytes)", flush=True)
    return True


def build_soem_cpp() -> bool:
    log_step("Building soem_cpp master tools")

    build_dir = SOEM_CPP / "build"
    if run(["cmake", "-B", "build", "-S", "."], SOEM_CPP) != 0:
        return False
    if run(["cmake", "--build", "build"], SOEM_CPP) != 0:
        return False

    expected = SOEM_CPP / "build" / "foe_flash"
    if not expected.exists():
        print("[FAIL] foe_flash not found.", file=sys.stderr, flush=True)
        return False

    print(f"[OK] foe_flash: {expected}", flush=True)
    return True


def build_slaveinfo() -> bool:
    log_step("Building SOEM slaveinfo")

    build_dir = SOEM / "build"
    if not build_dir.exists():
        if run(["cmake", "-B", "build", "-S", "."], SOEM) != 0:
            return False

    if run(["make", "slaveinfo"], SOEM / "build") != 0:
        return False

    expected = SOEM / "build" / "samples" / "slaveinfo" / "slaveinfo"
    if not expected.exists():
        print("[FAIL] slaveinfo not found.", file=sys.stderr, flush=True)
        return False

    print(f"[OK] slaveinfo: {expected}", flush=True)
    return True


def run_host_validation() -> bool:
    log_step("Running host-side validation")

    script = HPM5E_CAN / "tools" / "foe_loopback_check.py"
    return run([sys.executable, str(script)], HPM5E_CAN) == 0


def print_next_steps() -> None:
    log_step("Hardware-in-the-loop next steps")
    print("""
1. Connect HPM5E00EVK EtherCAT port to PC Ethernet port (e.g. enp5s0).
2. Power on the board.
3. Flash the firmware:

   cd ~/code_file/dependence/hpm5e_can
   ./openocd -s $HPM_SDK_BASE/boards/openocd \\
       -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \\
       -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \\
       -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg \\
       -c "program build/output/demo.elf verify reset exit"

4. Discover the slave:

   cd ~/code_file/dependence/SOEM/build
   sudo ./samples/slaveinfo/slaveinfo enp5s0

5. Flash a new firmware via FoE:

   cd ~/code_file/test/soem_cpp
   sudo ./build/foe_flash enp5s0 1 ~/code_file/dependence/hpm5e_can/build/output/demo.bin

6. Watch the serial console for:

   BOOT: flash complete, total 84416 bytes
   BOOT: staged CRC32 = 0x7bd2df9d
   BOOT: copying firmware to application area and reset

For full details see TESTING.md.
""")


def main() -> int:
    print("HPM5E00 FoE Slave Closed-Loop Test Runner", flush=True)
    print("This script runs all host-side checks.", flush=True)
    print("Hardware-in-the-loop steps will be printed at the end.", flush=True)

    env = setup_env()

    steps = [
        ("hpm5e_can firmware", build_hpm5e_can, (env,)),
        ("soem_cpp tools", build_soem_cpp, ()),
        ("SOEM slaveinfo", build_slaveinfo, ()),
        ("host validation", run_host_validation, ()),
    ]

    all_passed = True
    for name, func, args in steps:
        try:
            if not func(*args):
                all_passed = False
                print(f"[FAIL] Step '{name}' failed.", file=sys.stderr, flush=True)
                break
        except Exception as e:
            all_passed = False
            print(f"[FAIL] Step '{name}' raised exception: {e}", file=sys.stderr, flush=True)
            break

    print_next_steps()

    if all_passed:
        print("\n[OK] All host-side checks passed.", flush=True)
        return 0
    else:
        print("\n[FAIL] Some host-side checks failed.", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
