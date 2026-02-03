#!/usr/bin/env python3
"""
Generate bootloader-compatible update file (.upd) from application binary (.bin)

Usage: mkupd.py <input.bin> <output.upd> [options]

Options:
  --major <N>      Firmware major version (default: 1)
  --minor <N>      Firmware minor version (default: 0)
  --hw-type <N>    Hardware type (default: 0 for generic)
  --bl-ver-min <N> Minimum bootloader version required (default: 1)
  --entry <ADDR>   Entry point address (default: 0x0C80)
"""

import sys
import struct
import argparse

# App header constants
APP_MAGIC = 0x454D4F57  # "WOME" in little-endian
HEADER_SIZE = 64
DEFAULT_ENTRY_POINT = 0x0C80

def crc32(data):
    """Calculate CRC32 (IEEE 802.3 polynomial)"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF

def create_header(app_data, fw_major, fw_minor, hw_type, bl_ver_min, entry_point):
    """Create 64-byte app header"""
    app_size = len(app_data)
    app_crc = crc32(app_data)

    # Pack first 24 bytes (for header CRC calculation)
    header_data = struct.pack('<I', APP_MAGIC)           # magic (4 bytes)
    header_data += struct.pack('<B', fw_major)           # fw_ver_major (1 byte)
    header_data += struct.pack('<B', fw_minor)           # fw_ver_minor (1 byte)
    header_data += struct.pack('<B', bl_ver_min)         # bl_ver_min (1 byte)
    header_data += struct.pack('<B', hw_type)            # hw_type (1 byte)
    header_data += struct.pack('<I', app_size)           # app_size (4 bytes)
    header_data += struct.pack('<I', app_crc)            # app_crc32 (4 bytes)
    header_data += struct.pack('<I', entry_point)        # entry_point (4 bytes)
    # Total: 24 bytes

    # Calculate header CRC (of first 24 bytes)
    header_crc = crc32(header_data)

    # Add header CRC
    header_data += struct.pack('<I', header_crc)         # header_crc32 (4 bytes)

    # Pad with 0xFF to 64 bytes
    header_data += b'\xFF' * (HEADER_SIZE - len(header_data))

    assert len(header_data) == HEADER_SIZE, f"Header size mismatch: {len(header_data)}"

    return header_data, app_crc

def main():
    parser = argparse.ArgumentParser(
        description='Generate bootloader-compatible update file from application binary'
    )
    parser.add_argument('input', help='Input binary file (.bin)')
    parser.add_argument('output', help='Output update file (.upd)')
    parser.add_argument('--major', type=int, default=1, help='Firmware major version')
    parser.add_argument('--minor', type=int, default=0, help='Firmware minor version')
    parser.add_argument('--hw-type', type=int, default=0, help='Hardware type (0=generic)')
    parser.add_argument('--bl-ver-min', type=int, default=1, help='Minimum bootloader version')
    parser.add_argument('--entry', type=lambda x: int(x, 0), default=DEFAULT_ENTRY_POINT,
                        help='Entry point address (default: 0x0C80)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')

    args = parser.parse_args()

    # Read input binary
    try:
        with open(args.input, 'rb') as f:
            app_data = f.read()
    except FileNotFoundError:
        print(f"Error: Input file '{args.input}' not found", file=sys.stderr)
        return 1
    except IOError as e:
        print(f"Error reading '{args.input}': {e}", file=sys.stderr)
        return 1

    if len(app_data) == 0:
        print("Error: Input file is empty", file=sys.stderr)
        return 1

    # Create header
    header, app_crc = create_header(
        app_data,
        args.major,
        args.minor,
        args.hw_type,
        args.bl_ver_min,
        args.entry
    )

    # Write output file
    try:
        with open(args.output, 'wb') as f:
            f.write(header)
            f.write(app_data)
    except IOError as e:
        print(f"Error writing '{args.output}': {e}", file=sys.stderr)
        return 1

    total_size = len(header) + len(app_data)

    if args.verbose:
        print(f"Input:      {args.input} ({len(app_data)} bytes)")
        print(f"Output:     {args.output} ({total_size} bytes)")
        print(f"FW Version: {args.major}.{args.minor}")
        print(f"HW Type:    {args.hw_type}")
        print(f"BL Ver Min: {args.bl_ver_min}")
        print(f"Entry:      0x{args.entry:04X}")
        print(f"App CRC32:  0x{app_crc:08X}")
    else:
        print(f"Created {args.output}: {total_size} bytes (header={HEADER_SIZE}, app={len(app_data)}, crc=0x{app_crc:08X})")

    return 0

if __name__ == '__main__':
    sys.exit(main())
