Multichannel Auracast source
###########################

This opt-in nRF7120 DK application accepts one four-channel USB playback stream
and broadcasts four independent mono language programmes. It reuses the nRF Audio
modules without replacing the existing ``broadcast_source`` application.

Audio routing
*************

The USB interface is UAC2, full-speed, 48 kHz, signed 16-bit PCM, with channels
ordered FL, FR, BL, BR and product name ``Multichannel Auracast``.
There is no USB capture interface.

.. list-table::
   :header-rows: 1

   * - USB channel
     - Programme (language code)
     - Subgroup
     - Global BIS
   * - 1 (FL)
     - English (``eng``)
     - 0
     - 1
   * - 2 (FR)
     - Spanish (``spa``)
     - 1
     - 2
   * - 3 (BL)
     - French (``fra``)
     - 2
     - 3
   * - 4 (BR)
     - Mandarin (``cmn``)
     - 3
     - 4

Each subgroup advertises its own Program_Info and ISO 639-3 language metadata.
Each BIS is advertised as mono with no left/right allocation; FL/FR/BL/BR
are USB-side channel labels only. The source creates one unencrypted BIG
with sequential packing, four subgroups and four mono BISes, using the LC3
``48_4_1`` preset for all programmes:
48 kHz, 10 ms frames, and 96 kbit/s (120 bytes/frame) per channel.
There is no sample-rate conversion.

The controller advertising-data limit is 251 bytes to fit the four-subgroup BASE.
On nRF7120 the controller runs in the application image, so this setting belongs
in the multichannel configuration fragment, not the unused IPC-radio image.

USB delivers 384 bytes per millisecond. The application assembles 3840-byte PCM
frames, encodes four independent channels, and sends each encoded channel
to its corresponding subgroup. Encoding starts once all four BISes are streaming
and pauses if any BIS stops. Codec teardown waits until all four have stopped.
The application starts broadcasting automatically; it does not implement the
original source's play/pause, volume, or test-tone button controls.

The application asserts at build time that the CPU clock configuration is
256 MHz. The nRF7120 MDK startup applies that clock configuration. Device type
is fixed to gateway, and encoder channel locations are fixed in ``main.c``;
application device-location UICR persistence is not used.

Building
********

From the SDK workspace root, with the SDK toolchain environment active::

   west build -b nrf7120dk/nrf7120/cpuapp nrf/applications/nrf_audio \
     -d build-multichannel -- \
     -DEXTRA_CONF_FILE=multichannel_auracast/overlay-multichannel_auracast.conf \
     -DEXTRA_DTC_OVERLAY_FILE=multichannel_auracast/usb.overlay

Both extra files are required. The USB overlay replaces the board's legacy
headset/headphones descriptors with a single four-channel playback interface.
High-speed USB is intentionally disabled; full-speed supports this payload.
Keep the normal sysbuild-generated SoC UICR image; it is separate from application
device-location persistence.

Hardware bring-up status
========================

USBHS support is backported from upstream Zephyr commit
``429bca7eff3b4695e1ab129dbeae96d4081b2b0d``, using this SDK's
``nrf7120_7120e.dtsi`` filename. Its FIFO-size binding and driver prerequisites
are included, along with the VREGUSB startup write of zero at offset ``0x48C``.

On the tested nRF7120 engineering-A DK, the normal firmware now completes USB
initialization. Windows recognizes ``Multichannel Auracast`` and exposes a
four-channel, 48 kHz, 16-bit playback endpoint with FL/FR/BL/BR channel mapping.
Windows WASAPI playback has been exercised with nonzero samples on all four
channels and no reported host callback underruns. Exclusive-mode output was
choppy on two physical broadcast sinks; an otherwise identical four-channel
tone test was reported smooth in shared mode. The PC player uses shared mode.
Long-duration playback and independent receiver channel isolation still need
validation.

In the earlier two-subgroup configuration, periodic advertising started and all
four BIS stream-start callbacks were observed. The four-language configuration
still requires on-device validation. Live UART reports from that earlier build included repeated
transmit-cadence violations and dropped log messages; these do not by themselves
establish that the CPU is saturated.

Performance diagnostics
=======================

Add the opt-in fragment to a separate build::

   west build -b nrf7120dk/nrf7120/cpuapp nrf/applications/nrf_audio \
     -d build-multichannel-performance -- \
     -DEXTRA_CONF_FILE="multichannel_auracast/overlay-multichannel_auracast.conf;multichannel_auracast/overlay-performance.conf" \
     -DEXTRA_DTC_OVERLAY_FILE=multichannel_auracast/usb.overlay

This logs the configured frequency and runtime PLL registers at startup, existing CPU load
every five seconds, and five-second ``PERF`` aggregates during playback:

* Four-channel encode time (minimum/mean/maximum), send time (mean/maximum),
  maximum total frame-processing time, frames exceeding the 10 ms processing
  budget, and maximum queued input frames after dequeue.
* USB receive packet count, unexpected-size count, maximum callback gap, and
  receive overrun count.

Times are elapsed wall time, including interrupts, preemption, and blocking,
not isolated codec CPU cycles. Frame processing excludes waiting for USB input.
An aggregate appears only while its input continues arriving; stopped output
is not evidence of zero load or zero drops.

The fragment suppresses the existing per-frame LE Audio TX warnings to avoid
flooding the log transport, but leaves TX errors enabled. Compare this build
with normal firmware when evaluating the effect of logging itself. It does
not change the codec, stream count, queues, or controller buffer sizes.
Flashing disconnects the USB stream; reconnect playback in the PC GUI afterward.

Measured four-channel playback
------------------------------

A hardware run of the earlier two-subgroup firmware with all four Windows tracks playing confirmed PLL
requested/current values of zero, the nRF7120 encoding for 256 MHz.
Seven consecutive five-second aggregate windows reported:

* CPU load approximately 78%.
* Four-channel encode mean 6.33--6.35 ms, maximum 7.28 ms.
* Two-programme send mean approximately 0.85 ms, maximum 1.02 ms.
* Maximum total frame-processing time 8.15 ms; zero 10 ms budget misses
  across 3508 processed frames, and zero input frames queued after dequeue.
* Zero unexpected USB packet sizes and zero receive overruns. Maximum
  receive callback gap was 1.15 ms.

The host reported no callback underruns, but received audio remained similarly
choppy with the TX warning flood suppressed. Sustained CPU saturation or
observed encoder deadline misses therefore do not explain this test.
This is not a worst-case codec benchmark, nor does timely submission prove
correct controller timestamping, on-air payload delivery, or receiver rendering.

After smooth Spotify playback narrowed the issue to the PC output path, the
same four-tone signal and Python mixer were tested through WASAPI exclusive
and shared mode for 20 seconds each, without changing firmware. Both modes
reported zero PortAudio underruns and no wholly silent output channels in any
callback. The receivers were reported choppy in exclusive mode and smooth in
shared mode. This isolates an exclusive-path compatibility issue in this setup,
not the precise cause inside PortAudio, Windows, or the USB driver.

PC and receiver setup
*********************

Use the PC's native UAC2 driver and select four-channel, 48 kHz, 16-bit output
in the sending software. A multichannel-capable player, DAW, or explicit audio
routing must send programme A to channels 1/2 and programme B to channels 3/4.
These channels now map to English/Spanish and French/Mandarin, respectively;
the content must match the fixed language labels.
Two ordinary stereo applications do not automatically target different pairs.
Avoid a host mixer configuration that downmixes or upmixes the programmes.

Windows Python audio source
===========================

The ``pc_source/usb_audio_source.py`` GUI provides the PC-side player without
requiring a DAW or additional audio routing software. Use the native Windows
10/11 UAC2 driver; do not replace the audio device driver with WinUSB/Zadig.
The firmware also exposes a read-only USB HID interface using the native Windows
HID driver. It provides the encoded BASE used for periodic advertising, so the
player can show each subgroup's programme name and language beside its USB
channel. This reads the source's advertising data over USB; it does not verify
that a receiver can receive the broadcast.

Install 64-bit Python 3.12, then open PowerShell in this application's
``pc_source`` folder and run::

   py -3.12 -m venv .venv
   .\.venv\Scripts\python.exe -m pip install -r requirements.txt
   .\.venv\Scripts\python.exe usb_audio_source.py

These commands do not require activating the virtual environment or changing
PowerShell's execution policy.

Building a Windows executable
-----------------------------

On a Windows 10/11 x64 build PC with 64-bit Python 3.12 and the Python launcher
installed, double-click ``pc_source/build_windows.bat``. Alternatively, run
``.\build_windows.bat`` from PowerShell in that folder.
Internet access is needed to install build dependencies.

The script creates an isolated ``.venv-build`` environment, installs
``requirements-build.txt``, runs the headless tests, and packages the player with
PyInstaller. Any failed step stops the build and leaves the error visible.
Build output and generated files are ignored by Git.

After a successful build, distribute ``pc_source/dist/MultichannelAuracast.exe``.
This is a single portable GUI executable containing Python, Qt, and the audio
libraries; users do not need Python, pip, or a separate installation. Double-click
it to launch the same player described below. Keep using the native UAC2 driver
and the DK's application/audio USB port.

Build on Windows: PyInstaller does not cross-compile Windows executables from
Linux. The first launch can take longer while the bundle extracts into the
user's temporary directory. The executable is unsigned; Windows SmartScreen
may warn about an unrecognized publisher. For wider distribution, sign the
executable with your organization's code-signing certificate.

A one-file build using Python 3.13 was launched on Windows and displayed live
USB metadata. The Python 3.12 batch build and operation on another PC have not
been verified.

Using the player
----------------

#. Flash the multichannel firmware and connect the DK's application/audio USB
   port, not just its J-Link/debug USB port, before starting the player.
#. The GUI automatically looks for a four-channel output whose name contains
   ``Multichannel Auracast``. Use **Find USB device** to retry discovery.
   It never selects the PC's default output or ordinary speakers.
   The channel headings are read from the source's BASE and refreshed every
   ten seconds. Connect only one Auracast device: if metadata is unavailable or
   the device selection is ambiguous, the GUI says so instead of displaying
   potentially incorrect labels. Older firmware needs to be updated.
#. Drop **two stereo files** onto the large drop area for programme A/B, or
   **four mono files** for USB channels 1 through 4. Bulk dropping switches
   modes automatically. Check the displayed filenames: mapping follows the
   order delivered by Windows drag-and-drop, which need not be selection order.
   Drop a single file onto a specific row, or use **Choose file**, to assign
   channels explicitly.
#. Use **Play all**, or a row's **Play**, to connect and start playback. Each file
   has independent play/pause and volume controls; stereo files remain paired.
   **Pause all** pauses every file, and **Play all** resumes all loaded files,
   including any that were individually paused.
#. Adjust the **Master** slider to scale all four USB channels together.

Files load paused at their beginning and loop independently at EOF. Pausing
freezes that file's position and sends silence on its channel(s); other playing
files continue. The USB stream remains open while paused. Unloaded channels also
send silence. Loading a replacement file leaves the other files playing;
replacing the complete set pauses the new set. Changing the mode selector
clears the files after confirmation.

WAV, FLAC, Ogg, AIFF, and MP3 are supported through ``soundfile``/libsndfile.
Files are decoded to floating-point PCM, resampled to 48 kHz on the PC if needed,
and converted to interleaved four-channel 16-bit PCM for USB. No sample-rate
conversion is added to the firmware. Audio is preloaded into RAM rather than
streamed from disk: simultaneous four-channel material uses about 46 MB per
minute, plus temporary loading/resampling buffers.

On Windows the player uses WASAPI **shared mode**. Exclusive mode caused
periodically choppy audio on the tested nRF7120 USB endpoint even though
PortAudio reported no callback underruns; shared mode was smooth in a controlled
four-channel comparison. The output format remains four-channel, 48 kHz, 16-bit.
Configure the endpoint for four-channel/quadraphonic playback in the Windows
Sound control panel; a stereo shared-mixer configuration can downmix the
programmes. Disable spatial audio and enhancements that alter channel routing.
Close any application holding exclusive access to the endpoint.

After hot-plugging, PortAudio may retain an old device list; restart the player
if **Find USB device** still cannot see the DK. Disconnects, unsupported output
formats, file errors, and audio underruns are reported in the GUI. A disconnected
device is not silently replaced or automatically resumed.

To run the PC source's routing, file-loading, device-selection, and headless GUI
tests without opening an audio output::

   .\.venv\Scripts\python.exe -m unittest -v

The GUI and routing tests have been exercised headlessly on Linux and Windows.
Windows USB enumeration and WASAPI playback have been exercised with the DK
attached. Glitch-free received audio still requires validation.

Receiver selection
==================

Each BIS carries one mono LC3 channel. The nRF Audio broadcast sink starts on
subgroup 0 BIS 0 and **BTN 5** cycles through the first BIS of subgroups 1, 2, 3,
then back to subgroup 0. These correspond to global BIS numbers 1 through 4.
Each receiver plays the selected mono channel. A paused receiver stays paused.

Validation
**********

Focused native tests exercise four-channel USB packet assembly, deinterleaving
into four encoder sessions (with the LC3 implementation mocked), output-buffer
bounds, mono subgroup routing, language metadata, and invalid input handling::

   west build --no-sysbuild -b native_sim/native/64 \
     nrf/tests/nrf_audio/multichannel_auracast -d build-multichannel-tests
   build-multichannel-tests/zephyr/zephyr.exe

Builds and native tests do not establish hardware operation. Before considering
the application validated on air:

* Confirm USB enumeration exposes one four-channel playback interface and no
  capture interface.
* Send four distinguishable channel signals and verify channel ordering and
  isolation in all four mono programmes.
* Inspect BASE/BIGInfo for four 48 kHz subgroups, four BISes, mono allocations
  and the ``eng``, ``spa``, ``fra`` and ``cmn`` metadata.
* Measure the CPU clock and worst-case four-channel encoding time at 256 MHz.
  Sustain continuous playback while checking CPU load, USB overruns, ISO send
  failures, and receiver packet loss.
* Verify controller scheduling and retransmission performance for four BISes.
  USB SOF and Bluetooth clocks are independent; the inherited TX timing logic
  may occasionally drop a frame or send an empty SDU to compensate for drift.

USB enumeration, host playback, and the performance measurements above have
been exercised on physical hardware. Glitch-free receiver validation remains
outstanding.
