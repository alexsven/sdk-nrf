#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

"""Send a Windows WASAPI loopback device to an nRF Audio Wi-Fi broadcaster."""

import argparse
import ipaddress
import secrets
import socket
import struct
import sys

RATE = 48000
CHANNELS = 2
FRAMES = 240
PAYLOAD_SIZE = FRAMES * CHANNELS * 2
PORT = 5004
HEADER = struct.Struct("!4sII")


def send_audio(stream, sock, address, session):
    sequence = 0
    while True:
        pcm = stream.read(FRAMES, exception_on_overflow=True)
        if len(pcm) != PAYLOAD_SIZE:
            raise RuntimeError(f"Expected {PAYLOAD_SIZE} PCM bytes, got {len(pcm)}")
        packet = HEADER.pack(b"NAW1", session, sequence) + pcm
        if sock.sendto(packet, address) != len(packet):
            raise OSError("Incomplete UDP audio packet")
        sequence = (sequence + 1) & 0xFFFFFFFF


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-devices", action="store_true", help="List WASAPI loopback devices")
    parser.add_argument("--device", type=int, help="Loopback device index from --list-devices")
    parser.add_argument("--host", type=ipaddress.IPv4Address, help="Broadcaster IPv4 address")
    args = parser.parse_args(argv)

    if not args.list_devices and (args.device is None or args.host is None):
        parser.error("--device and --host are required when sending audio")
    if sys.platform != "win32":
        parser.error("WASAPI loopback capture requires Windows")

    import pyaudiowpatch as pyaudio

    with pyaudio.PyAudio() as audio:
        devices = list(audio.get_loopback_device_info_generator())
        if args.list_devices:
            for device in devices:
                print(f"{device['index']}: {device['name']} "
                      f"({device['maxInputChannels']} channels, "
                      f"default {int(device['defaultSampleRate'])} Hz)")
            if not devices:
                print("No WASAPI loopback devices found.", file=sys.stderr)
                return 1
            return 0

        device = next((item for item in devices if item["index"] == args.device), None)
        if device is None:
            raise ValueError("Selected loopback device is unavailable; list devices again")
        if device["maxInputChannels"] < CHANNELS:
            raise ValueError("Selected loopback device does not support stereo")
        audio.is_format_supported(RATE, input_device=args.device, input_channels=CHANNELS,
                                  input_format=pyaudio.paInt16)
        with audio.open(format=pyaudio.paInt16, channels=CHANNELS, rate=RATE, input=True,
                        input_device_index=args.device, frames_per_buffer=FRAMES) as stream:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                print(f"Sending {device['name']} to {args.host}:{PORT}; press Ctrl+C to stop.",
                      flush=True)
                print("UDP does not confirm that the broadcaster is receiving audio.", flush=True)
                send_audio(stream, sock, (str(args.host), PORT), secrets.randbits(32))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nStopped.")
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Audio sender failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
