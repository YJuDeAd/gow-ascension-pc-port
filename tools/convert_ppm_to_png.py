#!/usr/bin/env python3
"""tools/convert_ppm_to_png.py — Convert PPM (P6) images to PNG using Python standard library only."""

from __future__ import annotations

import glob
import os
import struct
import sys
import zlib


def write_png(width: int, height: int, rgb_data: bytes, out_path: str) -> None:
    """Write an RGB PNG using only standard library (struct + zlib)."""

    def make_chunk(chunk_type: bytes, data: bytes) -> bytes:
        length = struct.pack(">I", len(data))
        crc = struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
        return length + chunk_type + data + crc

    # PNG signature
    header = b"\x89PNG\r\n\x1a\n"

    # IHDR chunk: width(4), height(4), bit_depth(1), color_type(1=RGB:2), comp(1), filter(1), interlace(1)
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr_chunk = make_chunk(b"IHDR", ihdr_data)

    # Raw scanlines with filter byte 0 (None) at the start of each row
    row_bytes = width * 3
    raw_scanlines = bytearray()
    for y in range(height):
        raw_scanlines.append(0)  # Filter byte 0
        raw_scanlines.extend(rgb_data[y * row_bytes : (y + 1) * row_bytes])

    # IDAT chunk
    compressed = zlib.compress(bytes(raw_scanlines), level=9)
    idat_chunk = make_chunk(b"IDAT", compressed)

    # IEND chunk
    iend_chunk = make_chunk(b"IEND", b"")

    with open(out_path, "wb") as f:
        f.write(header + ihdr_chunk + idat_chunk + iend_chunk)


def parse_ppm(ppm_path: str) -> tuple[int, int, bytes, int]:
    """Parse binary PPM (P6) file, returns (width, height, rgb_data, non_black_count)."""
    with open(ppm_path, "rb") as f:
        header = b""
        while len(header.split()) < 4:
            line = f.readline()
            if not line:
                break
            # Ignore comments
            line = line.split(b"#")[0].strip()
            if line:
                header += b" " + line

    tokens = header.split()
    if len(tokens) < 4 or tokens[0] != b"P6":
        raise ValueError(f"Invalid P6 PPM: {ppm_path}")

    width = int(tokens[1])
    height = int(tokens[2])
    maxval = int(tokens[3])

    # The rest of the file is raw RGB bytes
    with open(ppm_path, "rb") as f:
        # Seek past the header text
        content = f.read()
        # Find header end
        idx = 0
        tokens_found = 0
        in_token = False
        in_comment = False
        while tokens_found < 4 and idx < len(content):
            b = content[idx : idx + 1]
            if in_comment:
                if b == b"\n":
                    in_comment = False
            elif b == b"#":
                in_comment = True
            elif b in b" \t\r\n":
                if in_token:
                    in_token = False
            else:
                if not in_token:
                    in_token = True
                    tokens_found += 1
            idx += 1
        raw_rgb = content[idx : idx + width * height * 3]

    # Count non-black pixels
    non_black = 0
    for i in range(0, len(raw_rgb), 3):
        r = raw_rgb[i]
        g = raw_rgb[i + 1] if i + 1 < len(raw_rgb) else 0
        b = raw_rgb[i + 2] if i + 2 < len(raw_rgb) else 0
        if r > 0 or g > 0 or b > 0:
            non_black += 1

    return width, height, raw_rgb, non_black


def process_frames_dir(frames_dir: str, max_pngs: int = 20) -> list[str]:
    """Find all PPMs in frames_dir, convert up to max_pngs non-black frames to PNG, delete PPMs."""
    ppm_files = sorted(glob.glob(os.path.join(frames_dir, "*.ppm")))
    converted = []

    for ppm in ppm_files:
        try:
            w, h, data, non_black = parse_ppm(ppm)
            print(f"[ppm-scan] {os.path.basename(ppm)}: {w}x{h}, non_black={non_black}")
            if non_black > 0 and len(converted) < max_pngs:
                png_path = os.path.splitext(ppm)[0] + ".png"
                write_png(w, h, data, png_path)
                converted.append(png_path)
                print(f"[ppm-to-png] Converted {os.path.basename(ppm)} -> {os.path.basename(png_path)} ({os.path.getsize(png_path)} bytes)")
            # Delete PPM to preserve disk space
            os.remove(ppm)
        except Exception as ex:
            print(f"[ppm-error] Failed processing {ppm}: {ex}")
            if os.path.isfile(ppm):
                os.remove(ppm)

    return converted


if __name__ == "__main__":
    dir_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "frames")
    max_count = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    pngs = process_frames_dir(dir_path, max_pngs=max_count)
    print(f"Total non-black PNGs created: {len(pngs)}")
