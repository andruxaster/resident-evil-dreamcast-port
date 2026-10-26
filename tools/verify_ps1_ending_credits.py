#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def read_u16(data, offset):
    if offset + 2 > len(data):
        raise ValueError(f"short read at 0x{offset:x}")
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data, offset):
    if offset + 4 > len(data):
        raise ValueError(f"short read at 0x{offset:x}")
    return struct.unpack_from("<I", data, offset)[0]


def verify_stf(path, expected_strips):
    data = path.read_bytes()
    if len(data) < 0x206:
        raise ValueError(f"{path}: file is too small")
    offset = 0x200
    strips = 0
    while offset + 6 <= len(data) and strips < 512:
        next_offset, count_offset, value_offset = struct.unpack_from("<3H", data, offset)
        if next_offset in (0, 0xFFFF):
            break
        if count_offset == 0xFFFF:
            raise ValueError(f"{path}: unsupported count sentinel at 0x{offset:x}")
        if value_offset == 0xFFFF:
            if count_offset > len(data) - offset or len(data) - offset - count_offset < 4097:
                raise ValueError(f"{path}: invalid raw record at 0x{offset:x}")
            output_size = 4097
        else:
            if count_offset > len(data) - offset or value_offset > len(data) - offset:
                raise ValueError(f"{path}: record offset outside file at 0x{offset:x}")
            count_pos = offset + count_offset
            value_pos = offset + value_offset
            output_size = 0
            terminated = False
            while count_pos + 2 <= len(data):
                run = read_u16(data, count_pos)
                count_pos += 2
                if run == 0:
                    terminated = True
                    break
                if value_pos >= len(data) or run > 4097 - output_size:
                    raise ValueError(f"{path}: invalid RLE at 0x{offset:x}")
                output_size += run
                value_pos += 1
            if not terminated or output_size not in (4096, 4097):
                raise ValueError(f"{path}: strip {strips} expands to {output_size} bytes")
        if next_offset > len(data) - offset:
            raise ValueError(f"{path}: next record outside file at 0x{offset:x}")
        next_record = offset + next_offset
        if next_record <= offset:
            raise ValueError(f"{path}: non-increasing record link at 0x{offset:x}")
        offset = next_record
        strips += 1
    if strips != expected_strips:
        raise ValueError(f"{path}: found {strips} strips, expected {expected_strips}")
    print(f"{path}: {strips} strips, next record 0x{offset:x}")


def verify_bio(path):
    data = path.read_bytes()
    if len(data) < 12 or data[0] != 0x10:
        raise ValueError(f"{path}: invalid TIM header")
    offset = 8
    clut_size = read_u32(data, offset)
    if clut_size < 12 or offset + clut_size > len(data):
        raise ValueError(f"{path}: invalid CLUT block")
    image_offset = offset + clut_size
    image_size = read_u32(data, image_offset)
    x, y, width, height = struct.unpack_from("<4H", data, image_offset + 4)
    if (x, y, width, height) != (0, 0, 0x40, 0x44):
        raise ValueError(f"{path}: expected 256x68 image, got {width * 4}x{height}")
    packed_size = (width * height * 4) // 2
    if image_size < 12 + packed_size or image_offset + image_size > len(data):
        raise ValueError(f"{path}: image payload is truncated")
    print(f"{path}: 256x68 4bpp image")


def verify_backdrop(path):
    data = path.read_bytes()
    if len(data) != 153600:
        raise ValueError(f"{path}: expected 153600 bytes, got {len(data)}")
    print(f"{path}: 320x240 result backdrop")


def verify_tim(path, expected_width, expected_height):
    data = path.read_bytes()
    if len(data) < 12 or data[0] != 0x10:
        raise ValueError(f"{path}: invalid TIM header")
    offset = 8
    clut_size = read_u32(data, offset)
    offset += clut_size
    image_size = read_u32(data, offset)
    x, y, width, height = struct.unpack_from("<4H", data, offset + 4)
    bpp = data[4] & 3
    actual_width = width * (4 if bpp == 0 else 2 if bpp == 1 else 1)
    if (x, y, actual_width, height) != (0, 0, expected_width, expected_height):
        raise ValueError(f"{path}: expected {expected_width}x{expected_height}, got {actual_width}x{height}")
    if offset + image_size > len(data):
        raise ValueError(f"{path}: image block is truncated")
    print(f"{path}: {expected_width}x{expected_height} result sheet")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("staff", nargs="?", type=Path, default=Path("assets/DC/Data/staff.stf"))
    parser.add_argument("staff2", nargs="?", type=Path, default=Path("assets/DC/Data/staff2.stf"))
    parser.add_argument("bio", nargs="?", type=Path, default=Path("assets/DC/Data/bio.tim"))
    args = parser.parse_args()
    verify_stf(args.staff, 300)
    verify_stf(args.staff2, 206)
    verify_bio(args.bio)
    verify_backdrop(args.bio.parent / "clis01.pix")
    verify_backdrop(args.bio.parent / "jill01.pix")
    verify_tim(args.bio.parent / "en05.tim", 256, 256)
    verify_tim(args.bio.parent / "en07.tim", 256, 168)


if __name__ == "__main__":
    main()
