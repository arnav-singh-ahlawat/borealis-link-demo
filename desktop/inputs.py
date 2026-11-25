"""Serial and file readers post to a queue. Only the desktop thread touches widgets."""

from pathlib import Path
from queue import Full, Queue
from threading import Event, Thread

import serial

from link_data import MAX_LINE


class Reader:
    def __init__(self, messages: Queue):
        self.messages = messages
        self.halt = Event()
        self.thread: Thread | None = None

    def start(self, mode: str, target: str, token: int) -> None:
        self.stop()
        self.halt = Event()
        self.thread = Thread(target=self._read, args=(mode, target, token, self.halt), daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.halt.set()
        if self.thread is not None:
            self.thread.join(timeout=0.35)

    def _read(self, mode: str, target: str, token: int, halt: Event) -> None:
        def emit(kind: str, payload: str | bytes) -> bool:
            while not halt.is_set():
                try:
                    self.messages.put((token, kind, payload), timeout=0.1)
                    return True
                except Full:
                    continue
            return False

        try:
            if mode == "serial":
                with serial.Serial(target, baudrate=115200, timeout=0.2) as port:
                    port.reset_input_buffer()
                    emit("state", f"Connected to {target}")
                    pending = bytearray()
                    discard = False
                    while not halt.is_set():
                        block = port.read(max(1, min(port.in_waiting, 1024)))
                        for byte in block:
                            if byte == 10:
                                if not discard and pending:
                                    emit("line", bytes(pending))
                                pending.clear()
                                discard = False
                            elif not discard:
                                if len(pending) < MAX_LINE:
                                    pending.append(byte)
                                else:
                                    discard = True
            else:
                with Path(target).open("rb") as capture:
                    emit("state", "Replaying sample packets")
                    for line in capture:
                        if not emit("line", line.rstrip(b"\r\n")) or halt.wait(0.025):
                            break
                    emit("finished", "Replay complete")
        except (OSError, serial.SerialException) as error:
            emit("error", str(error))
