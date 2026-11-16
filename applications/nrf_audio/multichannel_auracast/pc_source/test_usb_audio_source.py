#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Run with python -m unittest -v in this directory; no audio device is opened."""

import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import numpy as np
import soundfile as sf
import usb_audio_source as source
from PySide6 import QtCore, QtGui, QtTest, QtWidgets

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def clip(values, name="test.wav"):
    return source.Clip(Path(name), np.asarray(values, dtype=np.float32), 48000)


class MixerTests(unittest.TestCase):
    def test_stereo_routes_loops_and_pauses_independently(self):
        mixer = source.Mixer()
        mixer.replace_all(
            [
                clip([[0.125, 0.25], [0.375, 0.5]]),
                clip([[-0.125, -0.25], [-0.375, -0.5], [-0.625, -0.75]]),
            ]
        )
        mixer.play_all()
        output = mixer.render(5)
        expected = (
            np.array(
                [
                    [0.125, 0.25, -0.125, -0.25],
                    [0.375, 0.5, -0.375, -0.5],
                    [0.125, 0.25, -0.625, -0.75],
                    [0.375, 0.5, -0.125, -0.25],
                    [0.125, 0.25, -0.375, -0.5],
                ]
            )
            * 32768
        )
        np.testing.assert_array_equal(output, expected.astype(np.int16))
        mixer.set_playing(0, False)
        output = mixer.render(2)
        np.testing.assert_array_equal(output[:, :2], 0)
        self.assertEqual(mixer.snapshot()[0].position, 1)
        np.testing.assert_array_equal(
            output[:, 2:],
            np.array(
                [
                    [-0.625, -0.75],
                    [-0.125, -0.25],
                ]
            )
            * 32768,
        )
        mixer.set_playing(0, True)
        np.testing.assert_array_equal(mixer.render(1)[0, :2], [12288, 16384])

    def test_four_mono_volumes_master_pause_and_clipping(self):
        mixer = source.Mixer()
        mixer.replace_all([clip([[value]]) for value in (0.25, 0.5, -0.5, 2.0)])
        mixer.play_all()
        mixer.set_gain(0.5, 1)
        mixer.set_gain(0, 2)
        mixer.set_gain(0.5)
        np.testing.assert_array_equal(mixer.render(3), [[4096, 4096, 0, 32767]] * 3)
        mixer.pause_all()
        before = mixer.snapshot()
        np.testing.assert_array_equal(mixer.render(480), 0)
        self.assertEqual(mixer.snapshot(), before)
        mixer.set_gain(1)
        mixer.play_all()
        self.assertEqual(mixer.render(1)[0, 3], 32767)

    def test_mixed_layout_routes_mono_and_stereo_tracks(self):
        mixer = source.Mixer()
        mixer.set_layout([1, 1, 2])
        mixer.replace_all(
            [
                clip([[0.125]]),
                clip([[0.25]]),
                clip([[0.375, 0.5]]),
            ]
        )
        mixer.play_all()
        np.testing.assert_array_equal(mixer.render(1), [[4096, 8192, 12288, 16384]])

    def test_missing_tracks_are_silent_and_zero_volume_keeps_time(self):
        mixer = source.Mixer()
        mixer.replace_track(1, clip([[0.25, -0.25], [0.5, -0.5], [0.75, -0.75]]))
        mixer.set_playing(1, True)
        mixer.set_gain(0, 1)
        np.testing.assert_array_equal(mixer.render(2), 0)
        self.assertEqual(mixer.snapshot()[1].position, 2)
        mixer.set_gain(1, 1)
        np.testing.assert_array_equal(mixer.render(1), [[0, 0, 24576, -24576]])

    def test_invalid_load_preserves_existing_tracks(self):
        mixer = source.Mixer()
        previous = clip([[0.1, 0.2]])
        mixer.replace_track(0, previous)
        with self.assertRaises(ValueError):
            mixer.replace_all([previous, clip([[0.3]])])
        self.assertIs(mixer.snapshot()[0].clip, previous)
        for gain in (-0.1, 1.1, float("nan")):
            with self.assertRaises(ValueError):
                mixer.set_gain(gain)


class FileTests(unittest.TestCase):
    def test_resamples_stereo_and_validates_file_channels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stereo.wav"
            samples = np.zeros((441, 2), dtype=np.float32)
            samples[:, 0], samples[:, 1] = 0.25, -0.5
            sf.write(path, samples, 44100, subtype="FLOAT")
            loaded = source.load_clip(path, 2)
            self.assertEqual(loaded.samples.shape, (480, 2))
            self.assertEqual(loaded.original_rate, 44100)
            self.assertFalse(loaded.samples.flags.writeable)
            np.testing.assert_allclose(loaded.samples[20:-20, 1], -0.5, atol=0.001)
            with self.assertRaisesRegex(ValueError, "expected 1"):
                source.load_clip(path, 1)
            empty = Path(directory) / "empty.wav"
            sf.write(empty, np.empty((0, 2)), 48000)
            with self.assertRaisesRegex(ValueError, "empty"):
                source.load_clip(empty, 2)


class DeviceTests(unittest.TestCase):
    def test_windows_only_offers_matching_four_channel_wasapi_device(self):
        devices = [
            {"name": "PC speakers", "max_output_channels": 8, "hostapi": 1},
            {"name": "Multichannel Auracast", "max_output_channels": 2, "hostapi": 1},
            {"name": "Multichannel Auracast", "max_output_channels": 4, "hostapi": 0},
            {"name": "Speakers (Multichannel Auracast)", "max_output_channels": 4, "hostapi": 1},
        ]
        with (
            patch.object(source.sys, "platform", "win32"),
            patch.object(source.sd, "query_devices", return_value=devices),
            patch.object(
                source.sd,
                "query_hostapis",
                return_value=[
                    {"name": "MME"},
                    {"name": "Windows WASAPI"},
                ],
            ),
        ):
            found = source.find_devices()
        self.assertEqual([device.index for device in found], [3])

    def test_output_format_shared_mode_and_failed_start_cleanup(self):
        mixer = source.Mixer()
        output = source.UsbOutput(mixer)
        stream = Mock()
        stream.stopped = False
        device = source.UsbDevice(3, source.DEVICE_NAME, "Windows WASAPI")
        with (
            patch.object(source, "find_devices", return_value=[device]),
            patch.object(source.sd, "check_output_settings") as check,
            patch.object(source.sd, "OutputStream", return_value=stream) as factory,
            patch.object(source.sd, "WasapiSettings") as settings,
        ):
            output.open(device)
            settings.assert_called_once_with(exclusive=False)
            self.assertEqual(check.call_args.kwargs["channels"], 4)
            self.assertEqual(check.call_args.kwargs["samplerate"], 48000)
            self.assertEqual(check.call_args.kwargs["dtype"], "int16")
            factory.assert_called_once()
            self.assertEqual(factory.call_args.kwargs["channels"], 4)
            self.assertEqual(factory.call_args.kwargs["samplerate"], 48000)
            self.assertEqual(factory.call_args.kwargs["dtype"], "int16")
            self.assertIs(factory.call_args.kwargs["extra_settings"], settings.return_value)
            output.close()
            stream.stop.assert_called_once_with(ignore_errors=False)
            stream.close.assert_called_once_with(ignore_errors=False)
            stream.reset_mock()
            stream.start.side_effect = source.sd.PortAudioError("USB disconnected")
            with self.assertRaises(source.sd.PortAudioError):
                output.open(device)
            self.assertIsNone(output.stream)
            stream.close.assert_called_once_with(ignore_errors=False)

    def test_device_disappearing_before_connect_does_not_open_another_output(self):
        output = source.UsbOutput(source.Mixer())
        device = source.UsbDevice(3, source.DEVICE_NAME, "Windows WASAPI")
        with (
            patch.object(source, "find_devices", return_value=[]),
            patch.object(source.sd, "OutputStream") as stream,
        ):
            with self.assertRaisesRegex(ValueError, "changed or disappeared"):
                output.open(device)
            stream.assert_not_called()


class GuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def setUp(self):
        self.device_patch = patch.object(source, "find_devices", return_value=[])
        self.device_patch.start()
        self.dialog_patch = patch.object(QtWidgets.QMessageBox, "critical")
        self.dialog = self.dialog_patch.start()
        self.window = source.SourceWindow()
        self.window.timer.stop()
        self.window.show()
        self.app.processEvents()

    def tearDown(self):
        self.window.close()
        self.app.processEvents()
        self.dialog_patch.stop()
        self.device_patch.stop()

    def wait_for_load(self):
        deadline = time.monotonic() + 10
        while self.window._loader is not None and time.monotonic() < deadline:
            QtTest.QTest.qWait(10)
        self.assertIsNone(self.window._loader)

    def test_drag_drop_four_mono_files_and_master_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            mime = QtCore.QMimeData()
            paths = [Path(directory) / f"channel-{i}.wav" for i in range(4)]
            for index, path in enumerate(paths):
                sf.write(path, np.full(480, (index + 1) / 8), 48000, subtype="FLOAT")
            mime.setUrls([QtCore.QUrl.fromLocalFile(str(path)) for path in paths])
            enter = QtGui.QDragEnterEvent(
                QtCore.QPoint(5, 5),
                QtCore.Qt.DropAction.CopyAction,
                mime,
                QtCore.Qt.MouseButton.LeftButton,
                QtCore.Qt.KeyboardModifier.NoModifier,
            )
            self.app.sendEvent(self.window.bulk_drop, enter)
            self.assertTrue(enter.isAccepted())
            drop = QtGui.QDropEvent(
                QtCore.QPointF(5, 5),
                QtCore.Qt.DropAction.CopyAction,
                mime,
                QtCore.Qt.MouseButton.LeftButton,
                QtCore.Qt.KeyboardModifier.NoModifier,
            )
            self.app.sendEvent(self.window.bulk_drop, drop)
            self.assertTrue(drop.isAccepted())
            self.wait_for_load()
            self.assertEqual(len(self.window.rows), 4)
            self.assertEqual([mode.currentIndex() for mode in self.window.pair_modes], [1, 1])
            self.assertEqual([t.clip.path for t in self.window.mixer.snapshot()], paths)
            with patch.object(self.window, "_ensure_connected", return_value=True):
                self.window.play_all.click()
            self.window.rows[1].volume.setValue(50)
            self.window.master_volume.setValue(50)
            np.testing.assert_array_equal(
                self.window.mixer.render(1),
                [[2048, 2048, 6144, 8192]],
            )
            self.window.pause_all.click()
            np.testing.assert_array_equal(self.window.mixer.render(1), 0)
            self.dialog.assert_not_called()

    def test_mixed_pair_layout_loads_and_routes_three_files(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / "stereo.wav"]
            paths.extend(Path(directory) / f"mono-{i}.wav" for i in range(2))
            sf.write(paths[0], np.array([[0.125, 0.25]]), 48000, subtype="FLOAT")
            sf.write(paths[1], np.array([[0.375]]), 48000, subtype="FLOAT")
            sf.write(paths[2], np.array([[0.5]]), 48000, subtype="FLOAT")

            self.window.pair_modes[1].setCurrentIndex(1)
            self.assertEqual(len(self.window.rows), 3)
            self.window.load_files(paths)
            self.wait_for_load()

            self.assertEqual([row.channels for row in self.window.rows], [2, 1, 1])
            self.window.mixer.play_all()
            np.testing.assert_array_equal(
                self.window.mixer.render(1),
                [[4096, 8192, 12288, 16384]],
            )
            self.dialog.assert_not_called()

    def test_bad_file_is_reported_without_replacing_audio(self):
        previous = clip([[0.25, 0.5]])
        self.window.mixer.replace_track(0, previous)
        self.window.load_files([Path("does-not-exist.wav")], 0)
        self.wait_for_load()
        self.assertIs(self.window.mixer.snapshot()[0].clip, previous)
        self.dialog.assert_called_once()
        self.assertTrue(self.window.panel.isEnabled())

    def test_two_stereo_files_have_independent_gui_transport(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / f"stereo-{i}.wav" for i in range(2)]
            for index, path in enumerate(paths):
                samples = np.array([[0.125, 0.25], [0.375, 0.5]]) * (index + 1)
                sf.write(path, samples, 48000, subtype="FLOAT")
            self.window.load_files(paths)
            self.wait_for_load()
            self.assertEqual(len(self.window.rows), 2)
            with patch.object(self.window, "_ensure_connected", return_value=True):
                self.window.rows[0].play.click()
                np.testing.assert_array_equal(
                    self.window.mixer.render(1),
                    [[4096, 8192, 0, 0]],
                )
                self.window.rows[1].play.click()
                np.testing.assert_array_equal(
                    self.window.mixer.render(1),
                    [[12288, 16384, 8192, 16384]],
                )
                self.window.rows[0].play.click()
                np.testing.assert_array_equal(
                    self.window.mixer.render(1),
                    [[0, 0, 24576, 32767]],
                )
            self.dialog.assert_not_called()

    def test_closing_during_load_waits_for_worker(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stereo.wav"
            sf.write(path, np.zeros((480, 2)), 48000)
            self.window.load_files([path], 0)
            self.window.close()
            self.wait_for_load()
            self.assertFalse(self.window.isVisible())

    def test_mode_change_hides_old_rows_before_deferred_deletion(self):
        previous_rows = self.window.rows.copy()
        self.window.pair_modes[0].setCurrentIndex(1)
        self.assertEqual(len(self.window.rows), 3)
        self.assertTrue(all(row.isHidden() for row in previous_rows))

    def test_play_without_device_does_not_use_default_output(self):
        self.window.mixer.replace_track(0, clip([[0.25, 0.5]]))
        with patch.object(source.sd, "OutputStream") as stream:
            self.window.start_all()
            stream.assert_not_called()
        self.assertFalse(self.window.mixer.snapshot()[0].playing)
        self.dialog.assert_called_once()


if __name__ == "__main__":
    unittest.main()
