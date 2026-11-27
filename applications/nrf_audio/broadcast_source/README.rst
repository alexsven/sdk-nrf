.. _nrf_audio_broadcast_source_app:

.. ncs-sample::
   :title: nRF Audio: Broadcast source

   The nRF Audio broadcast source application implements the :ref:`BIS gateway mode <nrf_audio_app_overview>`.

   In this mode, transmitting broadcast audio happens using Broadcast Isochronous Stream (BIS) and Broadcast Isochronous Group (BIG).
   Play and pause are emulated by enabling and disabling stream, respectively.

   The following limitations apply to this application:

   * One BIG with two BIS streams.
   * Audio input: USB or I2S (Line-in or using Pulse Density Modulation).
   * Configuration: 16-bit, several bit rates ranging from 32 kbps to 124 kbps.

.. _nrf_audio_broadcast_source_app_requirements:

Requirements
************

The application shares the :ref:`requirements common to all nRF Audio application <nrf_audio_app_requirements>`.

.. _nrf_audio_broadcast_source_app_ui:

User interface
**************

Most of the user interface mappings are common across all nRF Audio applications.
See the :ref:`nrf_audio_app_ui` page for detailed overview.

This application uses specific mapping for the following user interface elements:

* Pressed on the broadcast source device during playback:

  * **PLAY/PAUSE** - Starts or pauses the playback of the stream.
  * **BTN 4** -  Toggles between the normal audio stream and different test tones generated on the device.
    Use this tone to check the synchronization of headsets.

* **LED1** - Blinking blue - Device has started broadcasting audio.
* **RGB** - Solid green - The device is programmed as the gateway.

.. _nrf_audio_broadcast_source_app_configuration:

Configuration
*************

The application requires the ``CONFIG_TRANSPORT_BIS`` Kconfig option to be set to ``y`` in the :ncs-file:`/applications/nrf_audio/prj.conf` file for `Building and running`_ to succeed.

For other configuration options, see :ref:`nrf_audio_app_configuration`.

For information about how to configure applications in the |NCS|, see :ref:`configure_application`.

.. _nrf_audio_broadcast_source_app_building:

Building and running
********************

This application can be found under :file:`applications/nrf_audio/broadcast_source` in the nRF Connect SDK folder structure, but it uses :file:`.conf` files at :file:`applications/nrf_audio/`.

The nRF5340 Audio DK comes preprogrammed with basic firmware that indicates if the kit is functional.
See :ref:`nrf_audio_app_dk_testing_out_of_the_box` for more information.

To build the application, complete the following steps:

1. Select the BIS mode by setting the ``CONFIG_TRANSPORT_BIS`` Kconfig option to ``y`` in the :ncs-file:`/applications/nrf_audio/prj.conf` file.
#. Complete the steps for building and programming common to all audio applications using one of the following methods:

   * :ref:`nrf_audio_app_building_script`
   * :ref:`nrf_audio_app_building_standard`

After programming, the broadcast source automatically starts broadcasting the default 48-kHz audio stream.

Wi-Fi stereo input
==================

An experimental Wi-Fi input is available for ``nrf7120dk/nrf7120/cpuapp``.
It uses the standard broadcast source and its one subgroup with left and right
BISes. USB and I2S are not used in this build. Build with both configuration
fragments from the workspace root::

   west build --sysbuild -b nrf7120dk/nrf7120/cpuapp nrf/applications/nrf_audio \
     -d build-broadcast-wifi -- \
     -DEXTRA_CONF_FILE="broadcast_source/overlay-broadcast_source.conf;broadcast_source/overlay-wifi.conf" \
     -DEXTRA_DTC_OVERLAY_FILE=broadcast_source/wifi.overlay

Flash the build with ``west flash -d build-broadcast-wifi``.
The Wi-Fi ROM patch is included in the merged image selected by the flash runner.

The Wi-Fi and Bluetooth LE coexistence driver is enabled. Wi-Fi low-power mode
is disabled to avoid delayed or bursty audio delivery. Connect the DK and PC
to the same access point; 5 GHz Wi-Fi is recommended for the first test.
To save a WPA2-PSK network, enter the following in the DK's RTT shell,
substituting the SSID and passphrase locally::

   wifi cred add -s "<SSID>" -k 1 -p "<passphrase>"
   kernel reboot cold

The add command reports errors; a successful command returns to the shell
prompt. After reboot, the DK automatically connects using the saved credentials
and attempts to reconnect if the connection is lost. Inspect its connection and
DHCP address in the RTT shell::

   wifi status
   net iface

To change the password for the same SSID, repeat the add command with the new
password and reboot. To move to a different SSID, add the new network, then
delete the old one and reboot::

   wifi cred add -s "<NEW_SSID>" -k 1 -p "<new passphrase>"
   wifi cred delete "<OLD_SSID>"
   kernel reboot cold

Two credential slots allow the new network to be added before deleting the old
one. If both slots are already occupied, delete an unwanted network first.
Remove the old entry rather than relying on automatic selection between
multiple saved networks. Check for errors after each command.

Credentials are stored unencrypted in the settings partition and can be
extracted by someone with device/debug access. Shell history is disabled, but
the passphrase is visible when entered, and ``wifi cred list`` displays saved
passwords in plaintext. Do not share RTT transcripts containing credentials.
Deleting an entry does not guarantee secure erasure of old flash records.
DHCP addresses can change; configure a DHCP reservation in the router if the
sender should always use the same address.

The audio receiver listens on UDP port 5004. The sender and receiver must be
able to exchange traffic on the local network.

On Windows 10/11 with 64-bit Python, install the sender dependency in a virtual
environment from the ``broadcast_source/wifi_sender`` directory::

   py -3 -m venv .venv
   .\.venv\Scripts\python.exe -m pip install -r requirements.txt
   .\.venv\Scripts\python.exe wifi_audio_sender.py --list-devices
   .\.venv\Scripts\python.exe wifi_audio_sender.py --device <index> --host <DK_IPv4_address>

Choose the loopback device corresponding to the Windows playback endpoint used
by the application. The sender captures the endpoint's shared audio mix, so
other applications and system sounds on the same endpoint are included.
It requests stereo, 48-kHz, 16-bit input; set the endpoint to a compatible
format if opening the stream fails. Press Ctrl+C to stop the sender. The
broadcaster outputs silence while no audio is available. UDP has no connection
confirmation; sender output does not establish that the DK received data.

The protocol sends a versioned header, session identifier and sequence number
followed by 5 ms of interleaved stereo PCM in each datagram. The DK buffers
audio, conceals short packet gaps with silence, rejects late packets, and uses
small sample-rate corrections to limit clock drift. Longer gaps and buffer
overruns cause rebuffering. Logs report these events and missed deadlines.

Use only a trusted network: UDP audio is not authenticated or encrypted by the
application, and other network clients can inject or intercept it. The default
Auracast broadcast is also unencrypted. Use audio you are entitled to broadcast;
WASAPI cannot capture protected content when the platform prevents it.
This input, its coexistence behavior and the Windows sender have been tested
on target hardware but require further validation; a successful build does
not prove glitch-free playback.

.. _nrf_audio_broadcast_source_app_testing:

Testing
*******

.. note::
    |nrf_audio_external_devices_note|

To test the broadcast source application, complete the following steps:

1. Make sure you have another nRF5340 Audio DK for testing purposes.
#. Program the other DK with the :ref:`broadcast sink <nrf_audio_broadcast_sink_app>` application.
   The broadcast sink device automatically synchronizes with the broadcast source after programming.
#. Proceed to testing the broadcast source using the :ref:`nrf_audio_broadcast_source_app_ui` buttons and LEDs.

Dependencies
************

For the list of dependencies, check the application's source files.
