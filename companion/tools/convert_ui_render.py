"""Convert unmodified WARP RGB readbacks to PNG using the standard library."""
from pathlib import Path
import argparse
import struct
import zlib


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def convert(path):
    magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"Unexpected RGB readback: {path}")
    stride = width * 3
    scanlines = b"".join(b"\0" + pixels[row:row + stride] for row in range(0, len(pixels), stride))
    output = (b"\x89PNG\r\n\x1a\n"
              + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
              + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))
    path.with_suffix(".png").write_bytes(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    directory = parser.parse_args().directory
    for readback in directory.glob("praxis-ui-*.ppm"):
        convert(readback)
