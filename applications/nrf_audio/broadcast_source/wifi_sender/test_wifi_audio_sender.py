#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

import unittest
from unittest.mock import Mock

import wifi_audio_sender as sender


class SenderTests(unittest.TestCase):
    def test_packets_contain_session_sequence_and_unchanged_stereo_pcm(self):
        pcm = bytes(range(256)) * 3 + bytes(range(192))
        stream = Mock()
        stream.read.side_effect = [pcm, pcm, EOFError]
        sock = Mock()
        sock.sendto.side_effect = lambda data, address: len(data)

        with self.assertRaises(EOFError):
            sender.send_audio(stream, sock, ("192.0.2.1", sender.PORT), 42)

        for sequence, call in enumerate(sock.sendto.call_args_list):
            packet, address = call.args
            self.assertEqual(address, ("192.0.2.1", sender.PORT))
            self.assertEqual(sender.HEADER.unpack_from(packet), (b"NAW1", 42, sequence))
            self.assertEqual(packet[sender.HEADER.size:], pcm)

    def test_short_capture_is_reported_without_sending(self):
        stream = Mock()
        stream.read.return_value = b"short"
        sock = Mock()
        with self.assertRaisesRegex(RuntimeError, "Expected 960 PCM bytes"):
            sender.send_audio(stream, sock, ("192.0.2.1", sender.PORT), 42)
        sock.sendto.assert_not_called()


if __name__ == "__main__":
    unittest.main()
