"""BOREALIS link viewer. Run with Python 3.11 or 3.12."""

import argparse
from array import array
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from pathlib import Path
from queue import Empty, Queue

import dearpygui.dearpygui as dpg
from PIL import Image, ImageOps
from serial.tools.list_ports import comports

from inputs import Reader
from link_data import Datagram, DecodedFrame, PacketLog, Transfer, parse_line
from ssdv_codec import decode

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / "desktop" / "sample.ndjson"
WIDTH, HEIGHT = 384, 256
TEXT = (228, 239, 249)
MUTED = (152, 174, 198)
ACCENT = (102, 223, 213)
AMBER = (251, 193, 118)


def hex_dump(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        glyphs = "".join(chr(value) if 32 <= value <= 126 else "." for value in chunk)
        lines.append(f"{offset:04X}   {chunk.hex(' ').upper():47}   {glyphs}")
    return "\n".join(lines)


def image_preview(packets: bytes) -> tuple[array, tuple[int, int]]:
    with Image.open(BytesIO(decode(packets))) as source:
        source.load()
        size = source.size
        image = ImageOps.contain(source.convert("RGBA"), (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    surface = Image.new("RGBA", (WIDTH, HEIGHT), (12, 21, 35, 255))
    surface.paste(image, ((WIDTH - image.width) // 2, (HEIGHT - image.height) // 2))
    return array("f", (byte / 255 for byte in surface.tobytes())), size


def transfer_label(transfer: Transfer) -> str:
    return f"Image {transfer.ref.image}  ·  {transfer.ref.transfer:08X}"


def configure_style() -> None:
    with dpg.theme() as theme:
        with dpg.theme_component(dpg.mvAll):
            for item, color in (
                (dpg.mvThemeCol_WindowBg, (11, 18, 30)),
                (dpg.mvThemeCol_ChildBg, (16, 27, 44)),
                (dpg.mvThemeCol_PopupBg, (18, 30, 48)),
                (dpg.mvThemeCol_Border, (44, 63, 87)),
                (dpg.mvThemeCol_Text, TEXT),
                (dpg.mvThemeCol_TextDisabled, MUTED),
                (dpg.mvThemeCol_FrameBg, (11, 20, 34)),
                (dpg.mvThemeCol_FrameBgHovered, (31, 50, 72)),
                (dpg.mvThemeCol_Button, (32, 55, 76)),
                (dpg.mvThemeCol_ButtonHovered, (46, 76, 100)),
                (dpg.mvThemeCol_Header, (35, 68, 88)),
                (dpg.mvThemeCol_HeaderHovered, (42, 79, 99)),
                (dpg.mvThemeCol_Tab, (23, 40, 60)),
                (dpg.mvThemeCol_TabHovered, (44, 75, 94)),
                (dpg.mvThemeCol_TabActive, (35, 69, 86)),
                (dpg.mvThemeCol_TableHeaderBg, (20, 39, 60)),
                (dpg.mvThemeCol_TableRowBgAlt, (19, 34, 53)),
                (dpg.mvThemeCol_PlotHistogram, (60, 153, 166)),
                (dpg.mvThemeCol_CheckMark, ACCENT),
            ):
                dpg.add_theme_color(item, color)
            dpg.add_theme_style(dpg.mvStyleVar_WindowPadding, 15, 13)
            dpg.add_theme_style(dpg.mvStyleVar_ItemSpacing, 9, 8)
            dpg.add_theme_style(dpg.mvStyleVar_FramePadding, 8, 5)
            dpg.add_theme_style(dpg.mvStyleVar_FrameRounding, 4)
            dpg.add_theme_style(dpg.mvStyleVar_ChildRounding, 7)
    dpg.bind_theme(theme)


class Viewer:
    def __init__(self):
        self.messages = Queue(maxsize=1024)
        self.reader = Reader(self.messages)
        self.preview_pool = ThreadPoolExecutor(max_workers=1)
        self.preview_job = None
        self.previews = {}
        self.preview_errors = {}
        self.log = PacketLog()
        self.rows = deque()
        self.token = 0
        self.running = False
        self.mode = ""
        self.bad_lines = 0
        self.last_width = 0
        self.selected = None
        self.showed_image = None
        self.active = None

    def layout(self) -> None:
        configure_style()
        dpg.configure_app(manual_callback_management=True)
        with dpg.font_registry():
            inter = str(ROOT / "third_party" / "inter" / "Inter.ttf")
            body = dpg.add_font(inter, 16)
            title = dpg.add_font(inter, 29)
            mono = dpg.add_font(
                str(ROOT / "third_party" / "ibm-plex-mono" / "IBMPlexMono-Regular.ttf"), 14
            )
        dpg.bind_font(body)

        with dpg.texture_registry():
            blank = array("f", (12 / 255, 21 / 255, 35 / 255, 1)) * (WIDTH * HEIGHT)
            dpg.add_dynamic_texture(WIDTH, HEIGHT, blank, tag="preview_texture")

        with dpg.window(tag="main", no_title_bar=True, no_collapse=True):
            dpg.add_text("BOREALIS", tag="title", color=ACCENT)
            dpg.bind_item_font("title", title)
            dpg.add_text("Free-space optical link demo", color=MUTED)
            with dpg.group(horizontal=True):
                dpg.add_combo([], tag="port", width=260, default_value="")
                dpg.add_button(label="Refresh ports", callback=self.refresh_ports)
                dpg.add_button(label="Connect receiver", tag="connect", callback=self.connect)
                dpg.add_button(label="Replay demo", tag="replay",
                               callback=lambda: self.start("replay", str(SAMPLE)))
            dpg.add_text("Choose the receiver's USB serial port or replay the demo.", tag="state", color=MUTED)
            dpg.add_separator()
            with dpg.group(horizontal=True):
                with dpg.child_window(tag="packet_panel", width=730, height=-1):
                    dpg.add_text("Received packets", color=ACCENT)
                    with dpg.group(horizontal=True):
                        dpg.add_checkbox(label="Follow latest", tag="follow", default_value=True,
                                         callback=self.follow_latest)
                        dpg.add_text("0 LoRa datagrams", tag="count", color=MUTED)
                    with dpg.table(tag="packets", height=195, scrollY=True, freeze_rows=1,
                                   row_background=True, policy=dpg.mvTable_SizingStretchProp):
                        for column in ("Seq", "Image", "SSDV frame", "Fragment", "RSSI", "SNR"):
                            dpg.add_table_column(label=column)
                    dpg.add_text("Select a received datagram to inspect it.", tag="detail", color=MUTED)
                    with dpg.tab_bar():
                        with dpg.tab(label="Received chips"):
                            dpg.add_text("LoRa payload · Manchester chips start at byte 13", color=MUTED)
                            with dpg.plot(label="First 48 Manchester chips", height=100, width=-1,
                                          no_menus=True, no_box_select=True):
                                dpg.add_plot_axis(dpg.mvXAxis, label="Chip", tag="chip_x")
                                dpg.add_plot_axis(dpg.mvYAxis, tag="chip_y")
                                dpg.add_stair_series([], [], parent="chip_y", tag="chips")
                                dpg.set_axis_ticks("chip_y", (("OFF", 0), ("ON", 1)))
                                dpg.set_axis_limits("chip_x", 0, 48)
                                dpg.set_axis_limits("chip_y", -0.22, 1.22)
                            dpg.add_input_text(tag="wire", multiline=True, readonly=True,
                                               width=-1, height=173)
                        with dpg.tab(label="Manchester decoded"):
                            dpg.add_text("Reassembled SSDV bytes before Reed–Solomon correction", color=MUTED)
                            dpg.add_input_text(tag="decoded", multiline=True, readonly=True,
                                               width=-1, height=286)
                        with dpg.tab(label="After RS and CRC"):
                            dpg.add_text("CRC-validated SSDV used to rebuild the JPEG", color=MUTED)
                            dpg.add_input_text(tag="ssdv", multiline=True, readonly=True,
                                               width=-1, height=286)
                    for tag in ("wire", "decoded", "ssdv"):
                        dpg.bind_item_font(tag, mono)

                with dpg.child_window(tag="image_panel", width=-1, height=-1):
                    dpg.add_text("Image reconstruction", color=ACCENT)
                    dpg.add_combo([], tag="transfer", width=-1, callback=self.choose_transfer)
                    dpg.add_progress_bar(tag="progress", width=-1, default_value=0, overlay="0 frames")
                    dpg.add_text("Waiting for the first SSDV frame.", tag="image_state", color=MUTED, wrap=350)
                    dpg.add_image("preview_texture", tag="preview", width=WIDTH, height=HEIGHT, show=False)
                    dpg.add_button(label="Preview partial image", tag="partial", enabled=False,
                                   callback=lambda: self.show_image(partial=True))
                    dpg.add_separator()
                    dpg.add_text("Transmitter button", color=MUTED)
                    for text in ("1 press  ·  EV Building", "2 presses  ·  Hall Building", "3 presses  ·  Loyola Campus"):
                        dpg.add_text(text)
                    dpg.add_separator()
                    dpg.add_text("0 RS symbols corrected", tag="corrections", color=MUTED)
                    dpg.add_text("0 serial errors", tag="errors", color=MUTED)
        self.refresh_ports()
        dpg.set_primary_window("main", True)

    def refresh_ports(self) -> None:
        ports = [port.device for port in sorted(comports())]
        selected = dpg.get_value("port")
        dpg.configure_item("port", items=ports)
        dpg.set_value("port", selected if selected in ports else (ports[0] if ports else ""))

    def set_state(self, text: str, color=MUTED) -> None:
        dpg.set_value("state", text)
        dpg.configure_item("state", color=color)

    def connect(self) -> None:
        if self.running:
            self.reader.stop()
            self.token += 1
            self.set_running(False)
            self.set_state("Disconnected")
        elif port := dpg.get_value("port"):
            self.start("serial", port)
        else:
            self.set_state("No serial port selected. Connect the receiver and refresh ports.", AMBER)

    def set_running(self, value: bool) -> None:
        self.running = value
        dpg.configure_item("connect", label="Disconnect" if value else "Connect receiver")
        dpg.configure_item("replay", enabled=not value)

    def start(self, mode: str, target: str) -> None:
        self.token += 1
        self.mode = mode
        while self.rows:
            dpg.delete_item(self.rows.popleft()[0])
        self.log = PacketLog()
        self.bad_lines = 0
        self.active = self.selected = self.showed_image = None
        self.previews.clear()
        self.preview_errors.clear()
        dpg.set_value("follow", True)
        dpg.configure_item("transfer", items=[])
        dpg.set_value("transfer", "")
        dpg.set_value("count", "0 LoRa datagrams")
        dpg.set_value("errors", "0 serial errors")
        dpg.set_value("detail", "Select a received datagram to inspect it.")
        dpg.set_value("chips", [[], []])
        for tag in ("wire", "decoded", "ssdv"):
            dpg.set_value(tag, "")
        self.show_image()
        self.set_running(True)
        self.set_state("Opening receiver…" if mode == "serial" else "Opening demo capture…")
        self.reader.start(mode, target, self.token)

    def add_row(self, packet: Datagram) -> None:
        with dpg.table_row(parent="packets") as row:
            button = dpg.add_selectable(label=str(packet.seq), span_columns=True,
                                        callback=self.click_packet, user_data=packet)
            values = (
                str(packet.ref.image) if packet.ref else "—",
                str(packet.ref.index + 1) if packet.ref else "—",
                f"{packet.fragment + 1}/3" if packet.fragment is not None else "—",
                f"{packet.rssi} dBm", f"{packet.snr} dB",
            )
            for value in values:
                dpg.add_text(value)
        self.rows.append((row, button, packet))
        if len(self.rows) > 140:
            dpg.delete_item(self.rows.popleft()[0])
        if dpg.get_value("follow"):
            self.select_packet(packet)

    def click_packet(self, _sender, _selected, packet: Datagram) -> None:
        dpg.set_value("follow", False)
        self.select_packet(packet)

    def follow_latest(self) -> None:
        if dpg.get_value("follow") and self.rows:
            self.select_packet(self.rows[-1][2])

    def select_packet(self, packet: Datagram) -> None:
        if packet is not self.selected:
            for _, button, item in self.rows:
                if item is self.selected or item is packet:
                    dpg.set_value(button, item is packet)
            self.selected = packet
            dpg.set_value("wire", hex_dump(packet.wire))
            bits = [(byte >> shift) & 1 for byte in packet.chips[:6] for shift in range(7, -1, -1)]
            dpg.set_value("chips", [list(range(len(bits) + 1)), bits + bits[-1:]] if bits else [[], []])
        self.update_detail()

    def update_detail(self) -> None:
        packet = self.selected
        if packet is None:
            return
        transfer = self.log.transfers.get(packet.ref.session) if packet.ref else None
        frame = transfer.frames.get(packet.ref.index) if transfer else None
        note = f"Datagram {packet.seq}  ·  {len(packet.wire)} bytes"
        if packet.ref is None:
            note += "  ·  Unknown demo header"
        elif frame is None:
            note += "  ·  Waiting for all 3 fragments"
        elif frame.ssdv:
            note += f"  ·  CRC passed  ·  {frame.corrected_symbols} RS fixes  ·  {frame.invalid_pairs} bad pairs"
        else:
            note += f"  ·  {frame.status.replace('_', ' ')}"
        dpg.set_value("detail", note)
        dpg.set_value("decoded", hex_dump(frame.decoded) if frame else "")
        dpg.set_value("ssdv", hex_dump(frame.ssdv) if frame and frame.ssdv else "")

    def add_record(self, record) -> None:
        known = set(self.log.transfers)
        transfer = self.log.add(record)
        if transfer and transfer.ref.session not in known:
            self.active = transfer.ref.session
            items = [transfer_label(item) for item in reversed(self.log.transfers.values())]
            dpg.configure_item("transfer", items=items)
            dpg.set_value("transfer", transfer_label(transfer))
            self.previews = {key: preview for key, preview in self.previews.items() if key in self.log.transfers}
            self.preview_errors = {key: error for key, error in self.preview_errors.items()
                                   if key in self.log.transfers}

        if isinstance(record, Datagram):
            self.add_row(record)
            dpg.set_value("count", f"{self.log.radio_count} LoRa datagrams")
        elif isinstance(record, DecodedFrame):
            self.update_detail()
        elif record.state == "listening" and record.value > 0:
            if self.mode == "serial":
                self.set_state(f"Receiver listening   ·   {record.value / 1e6:g} MHz", ACCENT)
        elif record.state == "init_error":
            self.set_state(f"Receiver radio initialization failed ({record.value})", AMBER)

    def choose_transfer(self, _sender, label) -> None:
        for key, transfer in self.log.transfers.items():
            if transfer_label(transfer) == label:
                self.active = key
                self.show_image()
                return

    def show_image(self, partial=False) -> None:
        transfer = self.log.transfers.get(self.active)
        count = len(transfer.received) if transfer else 0
        total = transfer.ref.total if transfer else 0
        dpg.set_value("progress", count / total if total else 0)
        dpg.configure_item("progress", overlay=f"{count} / {total} SSDV frames" if total else "0 frames")
        dpg.configure_item("partial", enabled=0 < count < total and self.preview_job is None)
        fixes = transfer.corrected_symbols if transfer else 0
        dpg.set_value("corrections", f"{fixes} RS symbols corrected")
        if transfer is None:
            dpg.configure_item("preview", show=False)
            dpg.set_value("image_state", "Waiting for the first SSDV frame.")
            return

        preview = self.previews.get(self.active)
        if preview and self.showed_image != (self.active, preview[0]):
            dpg.set_value("preview_texture", preview[1])
            self.showed_image = self.active, preview[0]
        dpg.configure_item("preview", show=bool(preview))

        failed_count, error = self.preview_errors.get(self.active, (0, ""))
        needs_preview = (not preview or preview[0] != count) and (count != failed_count or partial)
        if self.preview_job is None and (transfer.complete or partial) and needs_preview:
            task = self.preview_pool.submit(image_preview, transfer.image_bytes())
            self.preview_job = self.token, self.active, count, task
        if self.preview_job and self.preview_job[1] == self.active:
            dpg.set_value("image_state", "Rebuilding the JPEG…")
        elif count == failed_count and error:
            dpg.set_value("image_state", error)
        elif preview:
            width, height = preview[2]
            label = "Complete image" if preview[0] == total else f"Partial image from {preview[0]} frames"
            dpg.set_value("image_state", f"{label}  ·  {width} × {height} px")
        else:
            dpg.set_value("image_state", "Receiving image…" if count else "Waiting for a complete SSDV frame.")

    def tick(self) -> None:
        resized = dpg.get_viewport_client_width()
        if resized != self.last_width and resized > 0:
            self.last_width = resized
            left = int(resized * 0.57)
            picture = max(180, min(560, resized - left - 72))
            dpg.configure_item("packet_panel", width=left)
            dpg.configure_item("preview", width=picture, height=round(picture * HEIGHT / WIDTH))
            dpg.configure_item("image_state", wrap=picture)

        refresh = False
        for _ in range(50):
            try:
                token, kind, payload = self.messages.get_nowait()
            except Empty:
                break
            if token != self.token:
                continue
            if kind == "line":
                try:
                    self.add_record(parse_line(payload))
                except ValueError:
                    self.bad_lines += 1
                refresh = True
            else:
                self.set_state(payload, AMBER if kind == "error" else ACCENT)
                if kind in ("finished", "error"):
                    self.set_running(False)

        if self.preview_job and self.preview_job[-1].done():
            token, key, count, task = self.preview_job
            self.preview_job = None
            refresh = True
            if token == self.token and key in self.log.transfers:
                try:
                    texture, dimensions = task.result()
                    self.previews[key] = count, texture, dimensions
                    self.preview_errors.pop(key, None)
                except (ValueError, OSError) as error:
                    self.preview_errors[key] = count, str(error)
        if refresh:
            dpg.set_value("errors", f"{self.bad_lines} invalid serial lines  ·  {self.log.overruns} RX queue drops")
            self.show_image()
        if dpg.get_value("follow") and self.rows:
            dpg.set_y_scroll("packets", dpg.get_y_scroll_max("packets"))

    def close(self) -> None:
        self.reader.stop()
        self.preview_pool.shutdown(wait=True, cancel_futures=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="BOREALIS received-packet viewer")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--port", help="Receiver port (such as COM7 or /dev/ttyACM0)")
    source.add_argument("--replay", nargs="?", const=str(SAMPLE), help="Replay an NDJSON capture")
    args = parser.parse_args()

    dpg.create_context()
    viewer = Viewer()
    try:
        viewer.layout()
        dpg.create_viewport(title="BOREALIS · Optical Link Demo", width=1270, height=830,
                            min_width=940, min_height=680)
        dpg.setup_dearpygui()
        dpg.show_viewport()
        if args.port:
            viewer.start("serial", args.port)
        elif args.replay:
            viewer.start("replay", args.replay)
        while dpg.is_dearpygui_running():
            dpg.run_callbacks(dpg.get_callback_queue())
            viewer.tick()
            dpg.render_dearpygui_frame()
    finally:
        viewer.close()
        dpg.destroy_context()


if __name__ == "__main__":
    main()
