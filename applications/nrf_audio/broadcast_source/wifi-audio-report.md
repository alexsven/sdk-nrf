# Wi-Fi to Bluetooth LE Audio: prototype report

**Status:** Experimental proof of concept, tested on hardware  
**Date:** 27 November 2026

## Summary

The standard stereo nRF Audio broadcast source was extended with an optional
Wi-Fi input. A Windows PC captures its playback mix and sends uncompressed
stereo audio over UDP to an nRF7120 DK. The DK encodes the audio as LC3 and
broadcasts two Bluetooth LE Audio streams. An nRF5340 Audio DK synchronized to
the broadcast and played the left channel; audible end-to-end playback was
confirmed.

The listener initially heard occasional glitches, but none were heard during
the later measurement period. Their cause has not been established. This is a
prototype, not a reliability or latency qualification.

## Transport chain

```text
Windows application(s)
        |
        v
Windows playback endpoint's shared audio mix
        |
        v
WASAPI loopback capture + Python sender
48 kHz, stereo, 16-bit PCM
        |
        | IPv4 UDP via Wi-Fi access point
        | 5 ms audio per datagram, destination port 5004
        v
nRF7120 DK: UDP receiver -> jitter buffer -> 10 ms PCM frames
        |
        v
Stereo LC3 encoder -> Bluetooth LE Audio broadcast source
        |                 |
        | left BIS        | right BIS
        v                 v
nRF5340 Audio DK      available on air, not validated by this sink
left BIS -> LC3 decode -> analog audio output
```

The sender captures the selected Windows endpoint's shared mix, not a
particular application; system sounds and other applications using that
endpoint are included. Live application audio was heard end to end. Spotify
specifically was not verified, and platform-protected audio may not be
available to loopback capture.

The broadcaster uses one broadcast group with one subgroup and separate left
and right BISes. In this test, each BIS used LC3 at 48 kHz, 10 ms per frame and
96 kbit/s per channel. The broadcast name was `NRF5340_BROADCASTER`. The
existing sink had been customized to select the first BIS; its filter was set
to that broadcast name. Consequently, this test validates the left BIS only,
not stereo output or right-channel selection.

## Wi-Fi transport

The Python sender requests 48 kHz, stereo, signed 16-bit PCM from the chosen
WASAPI loopback endpoint. Each blocking read obtains 240 samples per channel
(5 ms), which are sent immediately as one UDP datagram. The sender does not
implement retransmissions or explicit packet pacing; it relies on the capture
stream for timing. UDP send success does not confirm delivery.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 bytes | ASCII magic/version `NAW1` |
| 4 | 4 bytes | Random per-sender session identifier, big-endian |
| 8 | 4 bytes | Sequence number, big-endian, incremented modulo 2³² |
| 12 | 960 bytes | 240 interleaved left/right stereo samples, signed 16-bit little-endian |

Each UDP payload is 972 bytes, at a nominal 200 packets/s. This represents
1.536 Mbit/s of PCM and approximately 1.6 Mbit/s including the application,
UDP and IPv4 headers, before Wi-Fi link overhead.

### Receiver buffering and timing

- The receiver validates the packet length and magic, tracks one active session,
  and uses sequence numbers to detect gaps and reject late or duplicate packets.
  Another session is ignored until the active session has been idle for more
  than two seconds.
- The ring buffer holds up to 4,800 stereo samples, or 100 ms. Playback is
  primed after 1,440 samples, or 30 ms, and then produces a 480-sample PCM
  frame every 10 ms.
- A gap of up to four packets is filled with silence; a larger gap, buffer
  overrun or underrun causes rebuffering. Playback produces silence when
  insufficient buffered audio is available.
- To compensate for clock differences, the receiver normally consumes 480
  samples per 10 ms frame, but consumes 481 above 35 ms occupancy or 479 below
  25 ms. It interpolates back to 480 output samples. This is a small
  correction, not a guarantee against longer stalls or bursty delivery.

## Hardware observations

The nRF7120 used a 5 GHz Wi-Fi connection; the PC was on the same local
network. Wi-Fi/Bluetooth coexistence and the MPSL coexistence interface were
enabled. An initial build omitted the Wi-Fi ROM patch and the coexistence
manager timed out. Including the patch in the merged flash image resolved
initialization; the DK subsequently logged that coexistence was configured.

With Wi-Fi power saving enabled, the DK became intermittently unreachable and
received audio in bursts, causing buffer overruns. Disabling power saving
restored sustained reachability in the test. The Wi-Fi build now disables
low-power mode. These observations identify a setup issue; they do not prove
the cause of all subsequently heard glitches.

## Measurements

| Measurement | Observed result | Interpretation |
|---|---|---|
| nRF7120 application-core CPU | Approximately 35% busy, 65% idle | Estimated from differences between two Zephyr thread-runtime snapshots during playback. Encoder accounted for approximately 29% of the total; Wi-Fi receive and playback threads approximately 0.6% and 1.4%. |
| Sink Bluetooth receive counters | 19,144 good frames, 0 additional bad/empty frames | Difference between two reads of existing sink counters, about 191 seconds at 100 frames/s. These are reported callback counts, not a complete proof that no frames were missed. |
| Wi-Fi buffer occupancy | Approximately 15–35 ms | 32 sampled reads over approximately 44 seconds; one subsequent debug read failed. Samples do not capture every minimum or maximum. |
| Controlled UDP sender | 12,000 silent packets over 60 seconds | Sender scheduling was at most 8 ms late, with two sends over 5 ms late. No sequence-gap warning appeared during the run, but rate-limited logs do not establish an exact loss rate. |
| Listening | Audio audible end to end; no glitches heard during the later measurement | Earlier occasional glitches were reported, but were not reproduced during that interval. |

CPU figures are approximate scheduler-accounting estimates from one run, not
peak load or a full system power measurement. They do not separately
characterize radio firmware, interrupt time, or worst-case scheduling. The
15–35 ms buffer samples and a clean listening interval do not prove that clock
drift or packet loss cannot occur over longer runs. End-to-end latency was not
measured.

## Limitations and next steps

- The current sink tests only the left BIS. Test right-channel selection and a
  stereo-capable receiver separately.
- Add interval counters for UDP packets, missing/late packets, rebuffering,
  buffer occupancy and clock corrections; collect sender scheduling and
  capture-overflow statistics alongside sink Bluetooth frame counters.
- Compare controlled runs with coexistence measurements and different Wi-Fi
  conditions before attributing glitches to coexistence, network jitter,
  sender scheduling or clock drift. The 5 GHz test does not by itself rule out
  shared-radio or antenna effects.
- Run longer listening and stress tests, including 2.4 GHz Wi-Fi, varied
  access-point conditions and end-to-end latency measurement.
- The application UDP stream and the test Bluetooth broadcast are unencrypted
  and unauthenticated. Use a trusted network; the prototype is not a secure
  production transport.

## Implementation and reproduction

The build, Wi-Fi connection and Windows sender instructions are in the
[broadcast source README](README.rst). Relevant implementation files are the
[Wi-Fi receiver](../src/modules/audio_wifi.c), [Windows sender](wifi_sender/wifi_audio_sender.py),
[Wi-Fi configuration](overlay-wifi.conf), and
[Wi-Fi ROM patch overlay](wifi.overlay). The native receiver tests are in
[wifi_input](../../../tests/nrf_audio/wifi_input/).

The Wi-Fi and normal USB broadcaster builds passed, along with five focused
native receiver tests and two Python sender tests. These tests and the hardware
observations above do not establish production readiness.
