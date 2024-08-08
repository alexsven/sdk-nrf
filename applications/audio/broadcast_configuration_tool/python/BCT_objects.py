#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
from tkinter import *

class BroadcastGroup:
    def __init__(self):
       self.preset = StringVar()
       self.num_sub_groups = IntVar()
       self.subGroups = []
       self.packing = StringVar()
       self.encryption = BooleanVar()
       self.broadcastCode = StringVar()

class BroadcastSubGroup:
    def __init__(self):
       self.context = StringVar()
       self.num_BISes = IntVar()
       self.BIS_locations = []
       self.language = StringVar()


class BAPPreset:
    def __init__(self, name, sdu_interval_us, framing, octets_per_sdu, bitrate_kbps, rtn,
                 max_transport_latency, pres_delay):
        self.name = name
        self.sdu_interval_us = sdu_interval_us
        self.framing = framing
        self.octets_per_sdu = octets_per_sdu
        self.bitrate_kbps = bitrate_kbps
        self.rtn = rtn
        self.max_transport_latency = max_transport_latency
        self.pres_delay = pres_delay

BAPPresets = [
    BAPPreset("Not_set", 0, "unframed", 0, 0, 0, 0, 0),
    BAPPreset("8_1_1", 7500, "unframed", 26, 27.734, 2, 8, 40000),
    BAPPreset("8_1_2", 7500, "unframed", 26, 27.734, 4, 45, 40000),
    BAPPreset("8_2_1", 10000, "unframed", 30, 24, 2, 10, 40000),
    BAPPreset("8_2_2", 10000, "unframed", 30, 24, 4, 60, 40000),
    BAPPreset("16_1_1", 7500, "unframed", 30, 32, 2, 8, 40000),
    BAPPreset("16_1_2", 7500, "unframed", 30, 32, 4, 45, 40000),
    BAPPreset("16_2_1", 10000, "unframed", 40, 32, 2, 10, 40000),
    BAPPreset("16_2_2", 10000, "unframed", 40, 32, 4, 60, 40000),
    BAPPreset("24_1_1", 7500, "unframed", 45, 48, 2, 8, 40000),
    BAPPreset("24_1_2", 7500, "unframed", 45, 48, 4, 45, 40000),
    BAPPreset("24_2_1", 10000, "unframed", 60, 48, 2, 10, 40000),
    BAPPreset("24_2_2", 10000, "unframed", 60, 48, 4, 60, 40000),
    BAPPreset("32_1_1", 7500, "unframed", 60, 64, 2, 8, 40000),
    BAPPreset("32_1_2", 7500, "unframed", 60, 64, 4, 45, 40000),
    BAPPreset("32_2_1", 10000, "unframed", 80, 64, 2, 10, 40000),
    BAPPreset("32_2_2", 10000, "unframed", 80, 64, 4, 60, 40000),
    BAPPreset("48_1_1", 7500, "unframed", 75, 80, 4, 15, 40000),
    BAPPreset("48_1_2", 7500, "unframed", 75, 80, 4, 50, 40000),
    BAPPreset("48_2_1", 10000, "unframed", 100, 80, 4, 20, 40000),
    BAPPreset("48_2_2", 10000, "unframed", 100, 80, 4, 65, 40000),
    BAPPreset("48_3_1", 7500, "unframed", 90, 96, 4, 15, 40000),
    BAPPreset("48_3_2", 7500, "unframed", 90, 96, 4, 50, 40000),
    BAPPreset("48_4_1", 10000, "unframed", 120, 96, 4, 20, 40000),
    BAPPreset("48_4_2", 10000, "unframed", 120, 96, 4, 65, 40000),
    BAPPreset("48_5_1", 7500, "unframed", 117, 124.8, 4, 15, 40000),
    BAPPreset("48_5_2", 7500, "unframed", 117, 124.8, 4, 50, 40000),
    BAPPreset("48_6_1", 10000, "unframed", 155, 124, 4, 20, 40000),
    BAPPreset("48_6_2", 10000, "unframed", 155, 124, 4, 65, 40000),
]