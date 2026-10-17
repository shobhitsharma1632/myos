#!/usr/bin/env python3
import sys
import os
import re

def find_symbol_in_map(map_path, symbol):
    with open(map_path, 'r') as f:
        for line in f:
            if symbol in line:
                match = re.search(r'0x([0-9a-fA-F]+)', line)
                if match:
                    return int(match.group(1), 16)
    return None

def install_stage1(target_image, stage1_bin, stage1_map, stage2_offset, stage2_size, partition_offset=0):
    SECTOR_SIZE = 512
    entry_offset = find_symbol_in_map(stage1_map, '__entry_start')
    if entry_offset is None:
        raise ValueError(f"Could not find __entry_start in {stage1_map}")
    entry_offset -= 0x7C00

    stage2_start = find_symbol_in_map(stage1_map, 'stage2_location')
    if stage2_start is None:
        raise ValueError(f"Could not find stage2_location in {stage1_map}")
    stage2_start -= 0x7C00

    with open(stage1_bin, 'rb') as fstage1:
        stage1_data = bytearray(fstage1.read(512))

    # Patch stage2_location: 4 bytes LBA, 1 byte sector count
    stage1_data[stage2_start:stage2_start+4] = int(stage2_offset).to_bytes(4, byteorder='little')
    stage1_data[stage2_start+4] = int(stage2_size)

    with open(target_image, 'r+b') as ftarget:
        ftarget.seek(partition_offset * SECTOR_SIZE, os.SEEK_SET)

        # 1. Write the first 3 bytes (jump instruction)
        ftarget.write(stage1_data[:3])

        # 2. Skip the BPB (preserving mkfs.fat formatting), write from entry_offset to 512
        ftarget.seek(partition_offset * SECTOR_SIZE + entry_offset, os.SEEK_SET)
        ftarget.write(stage1_data[entry_offset:])

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("Usage: install_bootloader.py <image> <stage1.bin> <stage1.map> <stage2.bin> [partition_offset]")
        sys.exit(1)

    image = sys.argv[1]
    stage1_bin = sys.argv[2]
    stage1_map = sys.argv[3]
    stage2_bin = sys.argv[4]
    part_offset = int(sys.argv[5]) if len(sys.argv) > 5 else 0

    stage2_size_bytes = os.path.getsize(stage2_bin)
    stage2_sectors = (stage2_size_bytes + 511) // 512
    stage2_offset = 1

    install_stage1(image, stage1_bin, stage1_map, stage2_offset, stage2_sectors, part_offset)
    print(f"Installed Stage 1 to {image} (stage2: LBA={stage2_offset}, sectors={stage2_sectors}, part_offset={part_offset})")
