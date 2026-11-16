#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Four-channel USB audio player for the multichannel_auracast firmware."""

import sys
from dataclasses import dataclass
from math import gcd, isfinite
from pathlib import Path
from queue import Empty, SimpleQueue
from threading import Lock

import numpy as np
import sounddevice as sd
import soundfile as sf
from numpy.typing import NDArray
from PySide6 import QtCore, QtWidgets
from scipy.signal import resample_poly

SAMPLE_RATE = 48000
DEVICE_NAME = "Multichannel Auracast"
AUDIO_FILTER = "Audio files (*.wav *.flac *.ogg *.aif *.aiff *.mp3);;All files (*)"


@dataclass(frozen=True)
class Clip:
    path: Path
    samples: NDArray[np.float32]
    original_rate: int


def load_clip(path: Path, channels: int) -> Clip:
    samples, rate = sf.read(path, dtype="float32", always_2d=True)
    if samples.shape[1] != channels:
        raise ValueError(f"{path.name}: expected {channels} channel(s), got {samples.shape[1]}.")
    if not len(samples) or rate <= 0 or not np.isfinite(samples).all():
        raise ValueError(f"{path.name}: empty audio or invalid samples/sample rate.")
    if rate != SAMPLE_RATE:
        divisor = gcd(rate, SAMPLE_RATE)
        samples = resample_poly(samples, SAMPLE_RATE // divisor, rate // divisor, axis=0)
    samples = np.ascontiguousarray(samples, dtype=np.float32)
    if not np.isfinite(samples).all():
        raise ValueError(f"{path.name}: resampling produced invalid samples.")
    samples.setflags(write=False)
    return Clip(path, samples, rate)


@dataclass
class Track:
    clip: Clip | None = None
    position: int = 0
    playing: bool = False
    gain: float = 1.0


@dataclass(frozen=True)
class TrackView:
    clip: Clip | None
    position: int
    playing: bool
    gain: float


class Mixer:
    """Own playback positions; GUI updates and audio callbacks share only this lock."""

    def __init__(self):
        self._lock = Lock()
        self._tracks = [Track(), Track()]
        self._track_channels = [2, 2]
        self._master_gain = 1.0

    def snapshot(self) -> tuple[TrackView, ...]:
        with self._lock:
            return tuple(TrackView(t.clip, t.position, t.playing, t.gain) for t in self._tracks)

    def set_layout(self, channels: list[int]):
        if sum(channels) != 4 or any(width not in (1, 2) for width in channels):
            raise ValueError("Track layout must map mono or stereo files to four output channels.")
        with self._lock:
            self._track_channels = channels.copy()
            self._tracks = [Track() for _ in channels]

    @staticmethod
    def _validate_clip(clip: Clip, channels: int):
        if clip.samples.ndim != 2 or clip.samples.shape[1] != channels or not len(clip.samples):
            raise ValueError(f"Expected nonempty {channels}-channel audio.")

    def replace_all(self, clips: list[Clip]):
        with self._lock:
            if len(clips) == 2:
                channels = [2, 2]
            elif len(clips) == 4:
                channels = [1, 1, 1, 1]
            elif len(clips) == 3 and len(self._track_channels) == 3:
                channels = self._track_channels.copy()
            else:
                raise ValueError("Choose two stereo files, four mono files, or a mixed layout.")
            for clip, width in zip(clips, channels, strict=True):
                self._validate_clip(clip, width)
            self._track_channels = channels
            self._tracks = [Track(clip=clip) for clip in clips]

    def replace_track(self, index: int, clip: Clip):
        with self._lock:
            self._validate_clip(clip, self._track_channels[index])
            self._tracks[index] = Track(clip=clip, gain=self._tracks[index].gain)

    def set_playing(self, index: int, playing: bool):
        with self._lock:
            track = self._tracks[index]
            if track.clip is None:
                raise ValueError("Load a file in this row first.")
            track.playing = playing

    def play_all(self):
        with self._lock:
            if not any(t.clip is not None for t in self._tracks):
                raise ValueError("Load audio files first.")
            for track in self._tracks:
                track.playing = track.clip is not None

    def pause_all(self):
        with self._lock:
            for track in self._tracks:
                track.playing = False

    def set_gain(self, gain: float, index: int | None = None):
        if not isfinite(gain) or not 0 <= gain <= 1:
            raise ValueError("Volume must be between 0 and 100 percent.")
        with self._lock:
            if index is None:
                self._master_gain = gain
            else:
                self._tracks[index].gain = gain

    def render(self, frames: int) -> NDArray[np.int16]:
        output = np.zeros((frames, 4), dtype=np.float32)
        offsets = np.arange(frames)
        with self._lock:
            channel = 0
            for track, width in zip(self._tracks, self._track_channels, strict=True):
                if track.clip is None or not track.playing:
                    channel += width
                    continue
                samples = track.clip.samples
                positions = (offsets + track.position) % len(samples)
                output[:, channel : channel + width] = (
                    samples[positions] * (track.gain * self._master_gain)
                )
                track.position = (track.position + frames) % len(samples)
                channel += width
        output *= 32768
        np.rint(output, out=output)
        np.clip(output, -32768, 32767, out=output)
        return output.astype(np.int16)


@dataclass(frozen=True)
class UsbDevice:
    index: int
    name: str
    host_api: str


def find_devices() -> list[UsbDevice]:
    host_apis = sd.query_hostapis()
    devices = []
    for index, device in enumerate(sd.query_devices()):
        host_api = host_apis[device["hostapi"]]["name"]
        if (
            DEVICE_NAME.casefold() in device["name"].casefold()
            and device["max_output_channels"] >= 4
            and (sys.platform != "win32" or host_api == "Windows WASAPI")
        ):
            devices.append(UsbDevice(index, device["name"], host_api))
    return devices


class UsbOutput:
    def __init__(self, mixer: Mixer):
        self.mixer = mixer
        self.stream = None
        self.messages: SimpleQueue[str] = SimpleQueue()

    def open(self, device: UsbDevice):
        self.close()
        if device not in find_devices():
            raise ValueError(
                "The selected USB device changed or disappeared. Refresh the device list."
            )
        settings = (
            sd.WasapiSettings(exclusive=False) if device.host_api == "Windows WASAPI" else None
        )
        parameters = dict(
            device=device.index,
            channels=4,
            samplerate=SAMPLE_RATE,
            dtype="int16",
            extra_settings=settings,
        )
        sd.check_output_settings(**parameters)
        stream = sd.OutputStream(
            **parameters,
            blocksize=0,
            latency="high",
            callback=self._callback,
        )
        try:
            stream.start()
        except sd.PortAudioError:
            stream.close(ignore_errors=False)
            raise
        self.stream = stream

    def _callback(self, outdata, frames, time_info, status):
        if status:
            self.messages.put(str(status))
        outdata[:] = self.mixer.render(frames)

    def close(self):
        stream, self.stream = self.stream, None
        if stream is not None:
            try:
                if not stream.stopped:
                    stream.stop(ignore_errors=False)
            finally:
                stream.close(ignore_errors=False)


class FileDrop(QtWidgets.QLabel):
    files_dropped = QtCore.Signal(object)

    def __init__(self, text: str, counts: tuple[int, ...]):
        super().__init__(text)
        self.counts = counts
        self.setAcceptDrops(True)
        self.setWordWrap(True)
        self.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.setFrameStyle(QtWidgets.QFrame.Shape.StyledPanel)
        self.setMinimumHeight(65)
        self.setMargin(10)

    def _paths(self, event) -> list[Path]:
        urls = event.mimeData().urls()
        if len(urls) not in self.counts or not all(url.isLocalFile() for url in urls):
            return []
        return [Path(url.toLocalFile()) for url in urls]

    def dragEnterEvent(self, event):
        if self._paths(event):
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event):
        paths = self._paths(event)
        if paths:
            self.files_dropped.emit(paths)
            event.acceptProposedAction()
        else:
            event.ignore()


class LoadThread(QtCore.QThread):
    loaded = QtCore.Signal(object)
    failed = QtCore.Signal(str)

    def __init__(self, paths: list[Path], channels: list[int], parent):
        super().__init__(parent)
        self.paths = paths
        self.channels = channels

    def run(self):
        try:
            clips = []
            for path, channels in zip(self.paths, self.channels, strict=True):
                if self.isInterruptionRequested():
                    return
                clips.append(load_clip(path, channels))
            if not self.isInterruptionRequested():
                self.loaded.emit(clips)
        except (OSError, RuntimeError, ValueError, MemoryError) as error:
            self.failed.emit(f"Could not load audio: {error}")


def time_text(frames: int) -> str:
    seconds = frames // SAMPLE_RATE
    return f"{seconds // 60:02}:{seconds % 60:02}"


class TrackRow(QtWidgets.QGroupBox):
    load_requested = QtCore.Signal(int)
    file_dropped = QtCore.Signal(int, object)
    play_requested = QtCore.Signal(int)
    gain_changed = QtCore.Signal(int, int)

    def __init__(self, index: int, channels: int, title: str):
        super().__init__(title)
        self.channels = channels
        layout = QtWidgets.QVBoxLayout(self)
        self.drop = FileDrop(f"Drop one {'stereo' if channels == 2 else 'mono'} file here", (1,))
        self.drop.files_dropped.connect(lambda paths: self.file_dropped.emit(index, paths))
        layout.addWidget(self.drop)
        controls = QtWidgets.QHBoxLayout()
        load = QtWidgets.QPushButton("Choose file...")
        load.clicked.connect(lambda: self.load_requested.emit(index))
        self.play = QtWidgets.QPushButton("Play")
        self.play.setEnabled(False)
        self.play.clicked.connect(lambda: self.play_requested.emit(index))
        self.position = QtWidgets.QLabel("--:-- / --:--")
        self.volume = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.volume.setRange(0, 100)
        self.volume.setValue(100)
        self.volume_label = QtWidgets.QLabel("100%")
        self.volume.valueChanged.connect(lambda value: self.gain_changed.emit(index, value))
        for widget in (load, self.play, self.position, self.volume, self.volume_label):
            controls.addWidget(widget)
        layout.addLayout(controls)

    def update_track(self, track: TrackView):
        self.play.setEnabled(track.clip is not None)
        self.play.setText("Pause" if track.playing else "Play")
        if track.clip is not None:
            self.drop.setText(track.clip.path.name)
            self.drop.setToolTip(
                f"{track.clip.path}\nOriginal rate: {track.clip.original_rate} Hz; output: 48000 Hz"
            )
            self.position.setText(
                f"{time_text(track.position)} / {time_text(len(track.clip.samples))}"
            )
        with QtCore.QSignalBlocker(self.volume):
            self.volume.setValue(round(track.gain * 100))
        self.volume_label.setText(f"{self.volume.value()}%")


class SourceWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("nRF7120 - Multichannel Auracast USB source")
        self.resize(820, 600)
        self.mixer = Mixer()
        self.output = UsbOutput(self.mixer)
        self.rows: list[TrackRow] = []
        self._pair_stereo = (True, True)
        self._loader: LoadThread | None = None
        self._load_index: int | None = None
        self._load_layout: list[int] = [2, 2]
        self._load_pair_stereo = self._pair_stereo
        self._closing = False

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        layout = QtWidgets.QVBoxLayout(central)
        devices = QtWidgets.QHBoxLayout()
        self.devices = QtWidgets.QComboBox()
        self.devices.setPlaceholderText("Connect the nRF7120 audio USB port, then find the device")
        self.refresh = QtWidgets.QPushButton("Find USB device")
        self.refresh.clicked.connect(self.refresh_devices)
        self.connect_button = QtWidgets.QPushButton("Connect")
        self.connect_button.clicked.connect(self.toggle_connection)
        devices.addWidget(self.devices, 1)
        devices.addWidget(self.refresh)
        devices.addWidget(self.connect_button)
        layout.addLayout(devices)

        self.panel = QtWidgets.QWidget()
        panel_layout = QtWidgets.QVBoxLayout(self.panel)
        pair_modes = QtWidgets.QHBoxLayout()
        self.pair_modes: list[QtWidgets.QComboBox] = []
        for pair, name in enumerate(("Programme A (USB 1/2)", "Programme B (USB 3/4)")):
            pair_modes.addWidget(QtWidgets.QLabel(name))
            mode = QtWidgets.QComboBox()
            mode.addItems(["Stereo file", "Two mono files"])
            mode.currentIndexChanged.connect(
                lambda index, pair=pair: self.change_pair_mode(pair, index)
            )
            pair_modes.addWidget(mode)
            self.pair_modes.append(mode)
        panel_layout.addLayout(pair_modes)
        self.bulk_drop = FileDrop(
            "Drop files for the selected pair layout, or drop two stereo/four mono files "
            "to set both pairs automatically.\nFiles map in drop order; check each channel row.",
            (2, 3, 4),
        )
        self.bulk_drop.files_dropped.connect(lambda paths: self.load_files(paths))
        panel_layout.addWidget(self.bulk_drop)
        self.track_layout = QtWidgets.QVBoxLayout()
        panel_layout.addLayout(self.track_layout)

        master = QtWidgets.QHBoxLayout()
        self.play_all = QtWidgets.QPushButton("Play all")
        self.play_all.clicked.connect(self.start_all)
        self.pause_all = QtWidgets.QPushButton("Pause all")
        self.pause_all.clicked.connect(self.mixer.pause_all)
        self.master_volume = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.master_volume.setRange(0, 100)
        self.master_volume.setValue(100)
        self.master_label = QtWidgets.QLabel("Master: 100%")
        self.master_volume.valueChanged.connect(self.change_master_gain)
        for widget in (self.play_all, self.pause_all, self.master_label, self.master_volume):
            master.addWidget(widget)
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.panel)
        layout.addWidget(scroll)
        layout.addLayout(master)
        self.status = QtWidgets.QLabel()
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self._rebuild_rows(self._track_layout())
        self.refresh_devices()
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.update_status)
        self.timer.start(100)

    def _error(self, text: str):
        self.status.setText(text)
        QtWidgets.QMessageBox.critical(self, "Audio source", text)

    def _track_layout(self, pair_stereo: tuple[bool, bool] | None = None) -> list[int]:
        modes = self._pair_stereo if pair_stereo is None else pair_stereo
        channels = []
        for stereo in modes:
            channels.extend((2,) if stereo else (1, 1))
        return channels

    def _set_pair_modes(self, pair_stereo: tuple[bool, bool]):
        self._pair_stereo = pair_stereo
        for control, stereo in zip(self.pair_modes, pair_stereo, strict=True):
            with QtCore.QSignalBlocker(control):
                control.setCurrentIndex(0 if stereo else 1)

    def _rebuild_rows(self, channels: list[int]):
        for row in self.rows:
            self.track_layout.removeWidget(row)
            row.hide()
            row.deleteLater()
        self.rows = []
        index = 0
        first_channel = 1
        for pair, stereo in enumerate(self._pair_stereo):
            count = 1 if stereo else 2
            for within_pair in range(count):
                width = channels[index]
                if stereo:
                    title = (
                        f"Programme {'AB'[pair]} - USB {first_channel}/{first_channel + 1} (L/R)"
                    )
                else:
                    title = (
                        f"USB {first_channel} - Programme {'AB'[pair]} "
                        f"{'L' if within_pair == 0 else 'R'}"
                    )
                row = TrackRow(index, width, title)
                row.load_requested.connect(self.choose_file)
                row.file_dropped.connect(lambda index, paths: self.load_files(paths, index))
                row.play_requested.connect(self.toggle_track)
                row.gain_changed.connect(
                    lambda index, value: self.mixer.set_gain(value / 100, index)
                )
                self.track_layout.addWidget(row)
                self.rows.append(row)
                first_channel += width
                index += 1

    def refresh_devices(self):
        if self.output.stream is not None:
            return
        previous = self.devices.currentData()
        self.devices.clear()
        try:
            for device in find_devices():
                self.devices.addItem(f"{device.name} [{device.host_api}]", device)
        except (sd.PortAudioError, OSError) as error:
            self._error(f"Could not enumerate USB audio devices: {error}")
            return
        if previous is not None:
            for index in range(self.devices.count()):
                if self.devices.itemData(index) == previous:
                    self.devices.setCurrentIndex(index)
                    break
        if self.devices.count():
            self.status.setText(
                "Found four-channel USB audio. Connect, load files, then press Play."
            )
        else:
            self.status.setText(
                "No four-channel 'Multichannel Auracast' output found. Connect the DK's audio "
                "USB port with the new firmware, then retry. After hot-plugging, a restart of "
                "this player may be needed. PC speakers will never be selected."
            )

    def _ensure_connected(self) -> bool:
        if self.output.stream is not None:
            return True
        device = self.devices.currentData()
        if device is None:
            self._error("No matching four-channel USB device. Use Find USB device first.")
            return False
        try:
            self.output.open(device)
        except (sd.PortAudioError, ValueError, OSError) as error:
            self._error(
                f"Could not open {device.name} at 48 kHz, four channels, 16-bit: {error}\n"
                "Configure the Windows endpoint for four-channel/quadraphonic playback "
                "and close any application holding exclusive access."
            )
            return False
        self.devices.setEnabled(False)
        self.refresh.setEnabled(False)
        self.connect_button.setText("Disconnect")
        self.status.setText("Connected: four channels, 48000 Hz, 16-bit. Playback loops each file.")
        return True

    def disconnect(self):
        self.mixer.pause_all()
        closed_cleanly = True
        try:
            self.output.close()
        except sd.PortAudioError as error:
            self._error(f"Could not cleanly close USB audio: {error}")
            closed_cleanly = False
        self.devices.setEnabled(True)
        self.refresh.setEnabled(True)
        self.connect_button.setText("Connect")
        return closed_cleanly

    def toggle_connection(self):
        if self.output.stream is None:
            self._ensure_connected()
        else:
            if self.disconnect():
                self.status.setText("Disconnected. File positions are paused.")

    def start_all(self):
        if not any(t.clip is not None for t in self.mixer.snapshot()):
            self._error("Load audio files first.")
        elif self._ensure_connected():
            self.mixer.play_all()

    def toggle_track(self, index: int):
        track = self.mixer.snapshot()[index]
        if track.playing:
            self.mixer.set_playing(index, False)
        elif self._ensure_connected():
            self.mixer.set_playing(index, True)

    def change_master_gain(self, value: int):
        self.mixer.set_gain(value / 100)
        self.master_label.setText(f"Master: {value}%")

    def change_pair_mode(self, pair: int, index: int):
        desired = list(self._pair_stereo)
        desired[pair] = index == 0
        desired_modes = tuple(desired)
        if desired_modes == self._pair_stereo:
            return
        if any(t.clip is not None for t in self.mixer.snapshot()):
            answer = QtWidgets.QMessageBox.question(
                self,
                "Change channel mode",
                "Changing a programme's mode clears the loaded files. Continue?",
            )
            if answer != QtWidgets.QMessageBox.StandardButton.Yes:
                self._set_pair_modes(self._pair_stereo)
                return
        self._pair_stereo = desired_modes
        layout = self._track_layout()
        self.mixer.set_layout(layout)
        self._rebuild_rows(layout)

    def choose_file(self, index: int):
        filename, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Choose audio file", "", AUDIO_FILTER
        )
        if filename:
            self.load_files([Path(filename)], index)

    def load_files(self, paths: list[Path], index: int | None = None):
        if self._loader is not None:
            self._error("File loading is already in progress.")
            return
        if index is None:
            if len(paths) == 2:
                layout = [2, 2]
                pair_stereo = (True, True)
            elif len(paths) == 4:
                layout = [1, 1, 1, 1]
                pair_stereo = (False, False)
            else:
                layout = self._track_layout()
                pair_stereo = self._pair_stereo
            if len(paths) != len(layout):
                self._error(
                    "Choose a mono/stereo mode for each pair, then drop the matching number "
                    "of files (2, 3, or 4)."
                )
                return
            channels = layout
        else:
            if len(paths) != 1:
                self._error("Drop one file onto a channel row.")
                return
            layout = self._track_layout()
            pair_stereo = self._pair_stereo
            channels = [layout[index]]
        self.panel.setEnabled(False)
        self.status.setText("Loading audio and resampling to 48000 Hz...")
        self._load_index = index
        self._load_layout = layout
        self._load_pair_stereo = pair_stereo
        worker = LoadThread(paths, channels, self)
        worker.loaded.connect(self._files_loaded)
        worker.failed.connect(self._load_error)
        worker.finished.connect(self._load_finished)
        self._loader = worker
        worker.start()

    @QtCore.Slot(object)
    def _files_loaded(self, clips: list[Clip]):
        if self._closing:
            return
        index = self._load_index
        if index is None:
            self._set_pair_modes(self._load_pair_stereo)
            self.mixer.set_layout(self._load_layout)
            self.mixer.replace_all(clips)
            self._rebuild_rows(self._load_layout)
        else:
            self.mixer.replace_track(index, clips[0])
        self.status.setText("Files loaded, paused at the start. Use each row's Play or Play all.")
        self.update_status()

    @QtCore.Slot(str)
    def _load_error(self, message: str):
        if not self._closing:
            self._error(message)

    @QtCore.Slot()
    def _load_finished(self):
        worker, self._loader = self._loader, None
        worker.deleteLater()
        self.panel.setEnabled(True)
        if self._closing:
            self.close()

    def update_status(self):
        for row, track in zip(self.rows, self.mixer.snapshot(), strict=True):
            row.update_track(track)
        try:
            while True:
                self.status.setText(f"USB audio warning: {self.output.messages.get_nowait()}")
        except Empty:
            pass
        if self.output.stream is not None:
            try:
                active = self.output.stream.active
            except sd.PortAudioError as error:
                self.disconnect()
                self._error(f"USB audio device became unavailable: {error}")
                return
            if not active:
                self.disconnect()
                self._error(
                    "USB audio stopped unexpectedly. Check the USB connection and console "
                    "errors, then reconnect. Playback has been paused."
                )

    def closeEvent(self, event):
        self.timer.stop()
        self.disconnect()
        if self._loader is not None:
            self._closing = True
            self._loader.requestInterruption()
            self.status.setText("Waiting for the file loader to finish before closing...")
            event.ignore()
        else:
            event.accept()


def main() -> int:
    app = QtWidgets.QApplication(sys.argv)
    window = SourceWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
