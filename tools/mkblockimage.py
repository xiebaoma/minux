"""Generate the deterministic 32-sector raw disk used by QEMU and make test."""
from pathlib import Path
import sys


def main():
    image = bytearray()
    for sector in range(32):
        image.extend(sector.to_bytes(8, "little"))
        image.extend((sector * 37 + i * 7 + i // 256) & 255 for i in range(8, 512))
    Path(sys.argv[1]).write_bytes(image)


if __name__ == "__main__":
    main()
