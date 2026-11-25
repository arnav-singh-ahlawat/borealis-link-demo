"""Wire records and bounded, session-aware image assembly for the packet viewer."""

from collections import OrderedDict
from dataclasses import dataclass, field
import json
import struct
from zlib import crc32

HEADER = struct.Struct(">2sBBIHHB")
CHIP_BYTES = (192, 192, 128)
MAX_LINE = 2048
MAX_FRAMES = 512


@dataclass(frozen=True)
class FrameRef:
    transfer: int
    image: int
    index: int
    total: int

    @property
    def session(self) -> tuple[int, int]:
        return self.transfer, self.image


@dataclass(frozen=True)
class Datagram:
    seq: int
    ms: int
    rssi: int
    snr: int
    wire: bytes
    ref: FrameRef | None
    fragment: int | None

    @property
    def chips(self) -> bytes:
        return self.wire[HEADER.size:] if self.ref else b""


@dataclass(frozen=True)
class DecodedFrame:
    ref: FrameRef
    status: str
    invalid_pairs: int
    corrected_symbols: int
    decoded: bytes
    ssdv: bytes | None


@dataclass(frozen=True)
class ReceiverStatus:
    state: str
    value: int


def _integer(obj: dict, field_name: str, low: int, high: int) -> int:
    value = obj.get(field_name)
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"Invalid {field_name}")
    return value


def _hex(obj: dict, field_name: str, limit: int) -> bytes:
    value = obj.get(field_name)
    if not isinstance(value, str) or len(value) > limit * 2 or len(value) % 2:
        raise ValueError(f"Invalid {field_name}")
    return bytes.fromhex(value)


def _wire_ref(wire: bytes) -> tuple[FrameRef | None, int | None]:
    if len(wire) >= HEADER.size:
        magic, version, image, transfer, index, total, fragment = HEADER.unpack_from(wire)
        if (magic == b"BL" and version == 1 and 1 <= image <= 3 and
                0 <= index < total <= MAX_FRAMES and fragment < len(CHIP_BYTES) and
                len(wire) == HEADER.size + CHIP_BYTES[fragment]):
            return FrameRef(transfer, image, index, total), fragment
    return None, None


def validate_ssdv(ssdv: bytes, ref: FrameRef) -> None:
    if (len(ssdv) != 256 or ssdv[:2] != b"\x55\x66" or ssdv[6] != ref.image or
            int.from_bytes(ssdv[7:9], "big") != ref.index or
            bool(ssdv[11] & 4) != (ref.index + 1 == ref.total) or
            not (0 < ssdv[9] <= 32 and 0 < ssdv[10] <= 32) or
            crc32(ssdv[1:220]) != int.from_bytes(ssdv[220:224], "big")):
        raise ValueError("Invalid SSDV frame")


def parse_line(line: bytes) -> Datagram | DecodedFrame | ReceiverStatus:
    if not 0 < len(line) <= MAX_LINE:
        raise ValueError("Invalid telemetry line length")
    message = json.loads(line)
    if not isinstance(message, dict):
        raise ValueError("Telemetry must be an object")
    kind = message.get("kind")
    if kind == "radio":
        wire = _hex(message, "wire", 255)
        ref, fragment = _wire_ref(wire)
        return Datagram(_integer(message, "seq", 0, 0xffffffff),
                        _integer(message, "ms", 0, 0xffffffff),
                        _integer(message, "rssi", -32768, 32767),
                        _integer(message, "snr", -128, 127), wire, ref, fragment)
    if kind == "status" and message.get("state") in ("listening", "init_error", "queue_overrun"):
        return ReceiverStatus(message["state"], _integer(message, "value", -(2**31), 2**31 - 1))
    if kind != "frame":
        raise ValueError("Unknown telemetry record")

    total = _integer(message, "total", 1, MAX_FRAMES)
    ref = FrameRef(_integer(message, "transfer", 0, 0xffffffff),
                   _integer(message, "image", 1, 3),
                   _integer(message, "frame", 0, total - 1), total)
    status = message.get("status")
    if status not in ("ok", "uncorrectable", "metadata_mismatch"):
        raise ValueError("Unknown frame status")
    decoded = _hex(message, "decoded", 256)
    if len(decoded) != 256:
        raise ValueError("Manchester output must be 256 bytes")
    corrected = _integer(message, "corrected_symbols", -1, 16)
    if (corrected < 0) != (status == "uncorrectable"):
        raise ValueError("Invalid correction count")
    ssdv = _hex(message, "ssdv", 256) if status == "ok" else None
    if ssdv is not None:
        validate_ssdv(ssdv, ref)
    return DecodedFrame(ref, status, _integer(message, "invalid_pairs", 0, 2048),
                        corrected, decoded, ssdv)


@dataclass
class Transfer:
    ref: FrameRef
    frames: dict[int, DecodedFrame] = field(default_factory=dict)
    received: dict[int, bytes] = field(default_factory=dict)
    corrected_symbols: int = 0

    @property
    def complete(self) -> bool:
        return len(self.received) == self.ref.total

    def image_bytes(self) -> bytes:
        return b"".join(self.received[index] for index in sorted(self.received))


class PacketLog:
    def __init__(self):
        self.transfers: OrderedDict[tuple[int, int], Transfer] = OrderedDict()
        self.radio_count = 0
        self.overruns = 0

    def add(self, record: Datagram | DecodedFrame | ReceiverStatus) -> Transfer | None:
        if isinstance(record, ReceiverStatus):
            if record.state == "queue_overrun":
                self.overruns += record.value
            return None
        if isinstance(record, Datagram):
            self.radio_count += 1
        ref = record.ref
        if ref is None:
            return None
        transfer = self.transfers.get(ref.session)
        if transfer is None:
            transfer = self.transfers[ref.session] = Transfer(ref)
            if len(self.transfers) > 6:
                self.transfers.popitem(last=False)
        if ref.total != transfer.ref.total:
            return None
        if isinstance(record, DecodedFrame):
            if ref.index not in transfer.received:
                transfer.frames[ref.index] = record
                if record.ssdv is not None:
                    transfer.received[ref.index] = record.ssdv
                    transfer.corrected_symbols += record.corrected_symbols
        return transfer
