"""Run the upstream SSDV command as a WASI program, with one temporary directory."""

from functools import lru_cache
from pathlib import Path
from tempfile import TemporaryDirectory

import wasmtime

MODULE_PATH = Path(__file__).with_name("ssdv.wasm")


@lru_cache(maxsize=1)
def _module():
    config = wasmtime.Config()
    config.consume_fuel = True
    engine = wasmtime.Engine(config)
    return engine, wasmtime.Module.from_file(engine, str(MODULE_PATH))


def _run(arguments: list[str], payload: bytes) -> bytes:
    engine, module = _module()
    with TemporaryDirectory(prefix="borealis-ssdv-") as directory:
        work = Path(directory)
        (work / "input").write_bytes(payload)
        wasi = wasmtime.WasiConfig()
        wasi.argv = ["ssdv", *arguments, "/job/input", "/job/output"]
        wasi.preopen_dir(str(work), "/job")
        wasi.stderr_file = str(work / "diagnostics")
        try:
            with wasmtime.Store(engine) as store, wasmtime.Linker(engine) as linker:
                store.set_limits(memory_size=32 * 1024 * 1024)
                store.set_fuel(300_000_000)
                store.set_wasi(wasi)
                linker.define_wasi()
                instance = linker.instantiate(store, module)
                instance.exports(store)["_start"](store)
        except wasmtime.ExitTrap as error:
            if error.code != 0:
                details = (work / "diagnostics").read_text(errors="replace").splitlines()
                reason = details[-1] if details else f"exit code {error.code}"
                raise ValueError(f"SSDV failed ({reason})") from error
        except (wasmtime.Trap, wasmtime.WasmtimeError) as error:
            raise ValueError("SSDV could not process this image") from error
        output = work / "output"
        if not output.exists() or not 0 < output.stat().st_size <= 8 * 1024 * 1024:
            raise ValueError("SSDV produced no usable image data")
        return output.read_bytes()


def encode(jpeg: bytes, image_id: int, quality: int = 4) -> bytes:
    return _run(["-e", "-c", "BOREAL", "-i", str(image_id), "-q", str(quality)], jpeg)


def decode(packets: bytes) -> bytes:
    if not packets or len(packets) % 256:
        raise ValueError("Expected one or more 256-byte SSDV packets")
    jpeg = _run(["-d"], packets)
    if jpeg[:2] != b"\xff\xd8" or jpeg[-2:] != b"\xff\xd9":
        raise ValueError("SSDV did not return a complete JPEG container")
    return jpeg
