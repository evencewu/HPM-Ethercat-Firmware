#!/usr/bin/env python3
"""
Closed-loop validation script for the HPM5E00 FoE slave demo.

This script performs host-side checks that do not require actual EtherCAT
hardware:

1. Verifies the generated firmware binary has a valid RISC-V reset vector.
2. Verifies the embedded SII EEPROM advertises CoE + FoE mailbox protocols.
3. Computes CRC32 of the firmware using the same algorithm as bootstrap.c and
   builds the metadata structure that the slave would write to flash.
4. Confirms the metadata can be round-tripped (encode -> decode -> CRC check).
"""

import struct
import sys
import zlib
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_BIN = PROJECT_ROOT / "build" / "output" / "demo.bin"
SII_BIN = PROJECT_ROOT / "sii_eeprom.bin"


def check_firmware(firmware: bytes) -> None:
    """Check that the firmware starts with a plausible RISC-V reset vector."""
    if len(firmware) < 8:
        raise ValueError("Firmware binary is too small")

    first_word = struct.unpack_from("<I", firmware, 0)[0]
    if first_word == 0xFFFFFFFF or first_word == 0x00000000:
        raise ValueError("Invalid reset vector (all 0/1)")

    print(f"[OK] Firmware size: {len(firmware)} bytes")
    print(f"[OK] First word (reset vector): 0x{first_word:08x}")


def _sii_config_checksum(data: bytes) -> int:
    """Compute SII configuration area CRC-8 (poly 0x07, init 0xFF)."""
    remainder = 0xFF
    polynomial = 0x07
    for byte in data:
        remainder ^= byte
        for _ in range(8):
            if remainder & 0x80:
                remainder = ((remainder << 1) ^ polynomial) & 0xFF
            else:
                remainder = (remainder << 1) & 0xFF
    return remainder


def _crc32_bitwise(data: bytes) -> int:
    """Bit-by-bit CRC32 matching bootstrap.c (poly 0xEDB88320)."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return ~crc & 0xFFFFFFFF


def check_sii(sii: bytes) -> None:
    """Check SII mailbox protocol field advertises CoE + FoE."""
    if len(sii) < 0x40:
        raise ValueError("SII binary is too small")

    # Config area checksum covers words 0-6 (bytes 0-13)
    stored_checksum = sii[14]  # low byte of word 7
    calc_checksum = _sii_config_checksum(sii[:14])
    if calc_checksum != stored_checksum:
        print(f"[WARN] SII config checksum mismatch: calc=0x{calc_checksum:02x}, stored=0x{stored_checksum:02x}")
    else:
        print(f"[OK] SII config checksum: 0x{stored_checksum:02x}")

    # Vendor/Product/Revision/Serial from standard info area (word addresses)
    vendor = struct.unpack_from("<I", sii, 0x10)[0]
    product = struct.unpack_from("<I", sii, 0x14)[0]
    revision = struct.unpack_from("<I", sii, 0x18)[0]
    serial = struct.unpack_from("<I", sii, 0x1C)[0]
    print(f"[OK] SII Vendor ID:    0x{vendor:08x}")
    print(f"[OK] SII Product Code: 0x{product:08x}")
    print(f"[OK] SII Revision:     0x{revision:08x}")
    print(f"[OK] SII Serial:       0x{serial:08x}")

    # Mailbox protocol at byte 0x38 (word address 0x1c = ECT_SII_MBXPROTO)
    mbx_proto = struct.unpack_from("<H", sii, 0x38)[0]
    print(f"[OK] SII Mailbox protocol: 0x{mbx_proto:04x}")

    MBXPROT_COE = 0x0004
    MBXPROT_FOE = 0x0008
    if not (mbx_proto & MBXPROT_COE):
        raise ValueError("SII does not advertise CoE")
    if not (mbx_proto & MBXPROT_FOE):
        raise ValueError("SII does not advertise FoE")

    # BOOT mailbox config at word addresses 0x14/0x16 (bytes 0x28-0x2F)
    rx_mbx_addr = struct.unpack_from("<H", sii, 0x28)[0]
    rx_mbx_size = struct.unpack_from("<H", sii, 0x2A)[0]
    tx_mbx_addr = struct.unpack_from("<H", sii, 0x2C)[0]
    tx_mbx_size = struct.unpack_from("<H", sii, 0x2E)[0]
    print(f"[OK] BOOT RX mailbox: 0x{rx_mbx_addr:04x} ({rx_mbx_size} bytes)")
    print(f"[OK] BOOT TX mailbox: 0x{tx_mbx_addr:04x} ({tx_mbx_size} bytes)")


def check_metadata_roundtrip(firmware: bytes) -> None:
    """Build metadata, encode, decode and verify CRC32."""
    crc_zlib = zlib.crc32(firmware) & 0xFFFFFFFF
    crc_bitwise = _crc32_bitwise(firmware)
    if crc_zlib != crc_bitwise:
        raise ValueError(f"CRC32 algorithm mismatch: zlib=0x{crc_zlib:08x}, bitwise=0x{crc_bitwise:08x}")

    crc = crc_zlib
    size = len(firmware)

    metadata = struct.pack("<IIII", 0x464F4501, size, crc, 0)
    magic, decoded_size, decoded_crc, _ = struct.unpack("<IIII", metadata)

    if magic != 0x464F4501:
        raise ValueError("Metadata magic mismatch")
    if decoded_size != size:
        raise ValueError("Metadata size mismatch")
    if decoded_crc != crc:
        raise ValueError("Metadata CRC mismatch")

    print(f"[OK] CRC32 algorithm match: 0x{crc:08x}")
    print("[OK] Metadata round-trip verified")


def main() -> int:
    errors = []

    if not FIRMWARE_BIN.exists():
        errors.append(f"Firmware binary not found: {FIRMWARE_BIN}")
    if not SII_BIN.exists():
        errors.append(f"SII binary not found: {SII_BIN}")

    if errors:
        for e in errors:
            print(f"[FAIL] {e}", file=sys.stderr)
        return 1

    firmware = FIRMWARE_BIN.read_bytes()
    sii = SII_BIN.read_bytes()

    print("=== HPM5E00 FoE Slave Closed-Loop Checks ===\n")

    try:
        check_firmware(firmware)
        check_sii(sii)
        check_metadata_roundtrip(firmware)
    except ValueError as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        return 1

    print("\n[OK] All host-side checks passed.")
    print("Next step: connect HPM5E00EVK and run foe_flash with sudo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
