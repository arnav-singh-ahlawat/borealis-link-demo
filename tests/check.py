"""Compile the portable firmware code, then rebuild the demo captures on the host."""

import argparse
from io import BytesIO
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "desktop"))
from link_data import DecodedFrame, PacketLog, parse_line
from ssdv_codec import decode


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-replay", action="store_true")
    args = parser.parse_args()
    files = sorted((ROOT / "images").glob("slot*.ssdv"))
    assert len(files) == 3
    with TemporaryDirectory(prefix="borealis-test-") as directory:
        executable = Path(directory) / ("check.exe" if os.name == "nt" else "check")
        protocol = ROOT / "protocol"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             "-I", str(protocol / "include"), str(ROOT / "tests" / "protocol_check.c"),
             *(str(protocol / name) for name in ("wire.c", "assemble.c", "ssdv_fec.c", "telemetry.c")),
             "-o", str(executable)],
            check=True,
        )
        capture = subprocess.check_output([str(executable), *(str(path) for path in files)])

    records = list(map(parse_line, capture.splitlines()))
    log = PacketLog()
    for record in records:
        log.add(record)
    assert len(log.transfers) == 3
    for transfer, source in zip(log.transfers.values(), files, strict=True):
        assert transfer.complete and transfer.image_bytes() == source.read_bytes()
        with Image.open(BytesIO(decode(transfer.image_bytes()))) as image:
            image.load()
            assert image.size == (192, 128)
        print(f"{source.stem}: {transfer.ref.total} exact frames, JPEG reconstructed")
    assert sum(item.corrected_symbols for item in log.transfers.values()) >= 1

    # A duplicate cannot inflate progress, and frames can reach the desktop out of order.
    frames = [item for item in records if isinstance(item, DecodedFrame) and item.ref.image == 2]
    shuffled = PacketLog()
    for frame in reversed(frames[1:]):
        shuffled.add(frame)
        shuffled.add(frame)
    transfer = shuffled.transfers[frames[0].ref.session]
    assert not transfer.complete and len(transfer.received) == len(frames) - 1
    with Image.open(BytesIO(decode(transfer.image_bytes()))) as preview:
        preview.load()
        assert preview.size == (192, 128)
    shuffled.add(frames[0])
    assert transfer.complete and transfer.image_bytes() == files[1].read_bytes()

    if args.write_replay:
        (ROOT / "desktop" / "sample.ndjson").write_bytes(capture)
    print("Manchester, radio framing, RS up to 16 errors, duplicates, and partial preview passed.")


if __name__ == "__main__":
    main()
