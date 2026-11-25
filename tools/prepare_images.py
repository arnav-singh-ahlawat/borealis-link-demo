"""Normalize and pre-packetize replacements for the three transmitter flash slots."""

import argparse
from io import BytesIO
from pathlib import Path
import sys

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "desktop"))
from link_data import FrameRef, MAX_FRAMES, validate_ssdv
from ssdv_codec import decode, encode


def prepare(source: Path, slot: int) -> tuple[bytes, bytes]:
    with Image.open(source) as original:
        fitted = ImageOps.pad(ImageOps.exif_transpose(original).convert("RGB"), (192, 128),
                              Image.Resampling.LANCZOS, color=(12, 21, 35))
    buffer = BytesIO()
    fitted.save(buffer, "JPEG", quality=82, subsampling=2, progressive=False)
    jpeg = buffer.getvalue()
    packets = encode(jpeg, slot)
    total, excess = divmod(len(packets), 256)
    if excess or not 1 <= total <= MAX_FRAMES:
        raise ValueError("Unexpected SSDV packet size or format")
    for index in range(total):
        validate_ssdv(packets[index * 256:(index + 1) * 256], FrameRef(0, slot, index, total))
    with Image.open(BytesIO(decode(packets))) as preview:
        preview.load()
        if preview.size != (192, 128):
            raise ValueError("SSDV preview did not preserve the 192 × 128 resolution")
    return jpeg, packets


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", metavar="IMAGE", nargs=3, type=Path)
    args = parser.parse_args()

    products = [prepare(path, slot) for slot, path in enumerate(args.images, start=1)]
    for slot, (jpeg, packets) in enumerate(products, start=1):
        prefix = ROOT / "images" / f"slot{slot}"
        prefix.with_suffix(".jpg").write_bytes(jpeg)
        prefix.with_suffix(".ssdv").write_bytes(packets)
        print(f"Slot {slot} · {len(packets) // 256} SSDV packets")


if __name__ == "__main__":
    main()
