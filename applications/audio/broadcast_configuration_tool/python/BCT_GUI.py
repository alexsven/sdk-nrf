#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

import sys
import serial
import logging
from BCT_objects import *
from tkinter import *
from tkinter import ttk
from ttkthemes import ThemedTk

sys.path.append("../../../nrf5340_audio/tools/uart_terminal/scripts")
from get_serial_ports import get_serial_ports

#defines
nordic_blue = "#009FC2"
preset_row = 1
subgroups_row = 2
packing_row = 3
encryption_row = 4
broadcast_code_row = 5

root = ThemedTk(theme='elegance', themebg=True)
root.title("Broadcast Configuration Tool")
root.minsize(800, 600)
root.grid_rowconfigure(0, weight=1)
root.grid_columnconfigure(0, weight=1)

# Create Broadcast group objects
BIG0 = BroadcastGroup()
BIG1 = BroadcastGroup()

s = ttk.Style()
s.configure("TNotebook", background=nordic_blue)
s.configure("TFrame", background=nordic_blue)

tab_control = ttk.Notebook(root)
overview = ttk.Frame(tab_control)
BIG0_tab = ttk.Frame(tab_control)
BIG1_tab = ttk.Frame(tab_control)
tabs = [BIG0_tab, BIG1_tab]

BIG0.preset.trace_add("write", lambda *args: populateGroup(BIG0_tab, BIG0))
BIG1.preset.trace_add("write", lambda *args: populateGroup(BIG1_tab, BIG1))

tab_control.add(overview, text="Overview")
tab_control.add(BIG0_tab, text="BIG0")
tab_control.add(BIG1_tab, text="BIG1")
tab_control.grid(row=0, column=0, sticky="nsew")

# Global variables
broadcastGroups = [BIG0, BIG1]
presets = ["Not_set", "Custom"]
contexts = ["Not_set"]
locations = ["Not_set"]


logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
console_handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)





def openSerial():
    ports = get_serial_ports()
    logger.debug("Ports")

    for port in ports:
        logger.debug(port)

    if (len(ports) == 0):
        logger.error("No serial ports found")
        sys.exit(1)

    if (len(ports) > 1):
        logger.warning("Multiple serial ports found")
    else:
        logger.info("Opening serial port: " + ports[0])
        ser = serial.Serial(ports[0], 115200, timeout=0)
        return ser

    return None

"""
Parse presets from structures.h and put them in a list
"""
def parseStructures(keyword, keyword_list):
    with open("../include/structures.h", "r") as f:
        lines = f.readlines()
        for line in lines:
            # Only get lines with both .preset and .name in them
            if keyword in line and ".name" in line:
                name = line.split("=")[2].split('\"')[1]
                keyword_list.append(name)
    return keyword_list


"""
Populate group tab
"""
def populateGroup(tab, BIG):
    # Clear the tab
    for widget in tab.winfo_children():
        widget.destroy()

    row_index = 0

    Label(tab, text="BAP Preset", font=("bold")).grid(row=0, column=0, padx=5, sticky=E)

    row_index = updatePreset(tab, BIG, row_index)

    Label(tab, text="Context").grid(row=row_index+1, column=0, padx=5, pady=5, sticky=NSEW)
    Label(tab, text="Number of BISes").grid(row=row_index+2, column=0, padx=5, pady=5, sticky=NSEW)
    Label(tab, text="Language").grid(row=row_index+3, column=0, padx=5, pady=5, sticky=NSEW)

    column_index = 1

    for subgroup in BIG.subGroups:
        Label(tab, text="Subgroup " + str(column_index)).grid(row=row_index, column=column_index, padx=5, pady=5, sticky=E)

        subgroup.context.set("Not_set")
        subgroup.context_drop = OptionMenu(tab, subgroup.context, *contexts)
        subgroup.context_drop.grid(row=row_index+1, column=column_index, padx=5, pady=5, sticky=E)

        subgroup.num_BISes.set(1)
        subgroup.num_BISes_spin = Spinbox(tab, from_=1, to=255, width=3, textvariable=subgroup.num_BISes)
        subgroup.num_BISes_spin.grid(row=row_index+2, column=column_index, padx=5, pady=5, sticky=E)

        subgroup.language.set("-")
        subgroup.language.trace_add("write", lambda *args: subgroup.language.set(subgroup.language.get()[:3]))
        subgroup.language_entry = Entry(tab, textvariable=subgroup.language, width=3 )
        subgroup.language_entry.grid(row=row_index+3, column=column_index, padx=5, pady=5, sticky=E)
        column_index += 1


def updatePreset(tab, BIG, row_index):
    BAPpreset = None

    for preset in BAPPresets:
        if preset.name == BIG.preset.get():
            custom = False
            BAPpreset = preset
            break
        else:
            custom = True

    row_index = presetPopulate(preset, tab, row_index, BIG, presets, custom)

    return row_index


def presetPopulate(preset, frame, row_index, BIG, presets, custom):
    row_index = 1
    Label(frame, text="Name:").grid(row=row_index, column=0, columnspan=1, sticky=NSEW)
    name_drop = OptionMenu(frame, BIG.preset, *presets).grid(row=row_index, column=1, columnspan=1, sticky=NSEW)
    row_index += 1

    Label(frame, text="SDU Interval (us):").grid(row=row_index, column=0, sticky=NSEW)

    sdu_var = IntVar()
    if BIG.preset.get() == "Custom":
        sdu_var.set(preset.sdu_interval_us)
        sdu_options = [7500, 10000]
        sdu_drop = OptionMenu(frame, sdu_var, *sdu_options).grid(row=row_index, column=1, sticky=NSEW)
    else :
        Label(frame, text=preset.sdu_interval_us).grid(row=row_index, column=1, sticky=NSEW)

    row_index += 1

    Label(frame, text="Framing:").grid(row=row_index, column=0, sticky=NSEW)

    if custom:
        framing_var = StringVar()
        framing_var.set(preset.framing)
        framing_options = ["unframed", "framed"]
        framing_drop = OptionMenu(frame, framing_var, *framing_options).grid(row=row_index, column=1, sticky=NSEW)
    else:
        Label(frame, text=preset.framing).grid(row=row_index, column=1, sticky=NSEW)

    row_index += 1

    Label(frame, text="Octets per SDU:").grid(row=row_index, column=0, sticky=NSEW)

    octets_var = IntVar()
    if custom:
        octets_var.set(preset.octets_per_sdu)
        octets_entry = Entry(frame, textvariable=octets_var).grid(row=row_index, column=1, sticky=NSEW)
    else:
        Label(frame, text=preset.octets_per_sdu).grid(row=row_index, column=1, sticky=NSEW)

    row_index += 1

    Label(frame, text="Bitrate (kbps):").grid(row=row_index, column=0, sticky=NSEW)

    if sdu_var.get() != 0:
        bitrate = round((octets_var.get() * 8 / (sdu_var.get() / 1000)), 3)
    else:
        bitrate = 0

    Label(frame, text=bitrate).grid(row=row_index, column=1, sticky=NSEW)
    row_index += 1

    Label(frame, text="Retransmits:").grid(row=row_index, column=0, sticky=NSEW)
    if custom:
        rtn_var = IntVar()
        rtn_var.set(preset.rtn)
        rtn_spin = Spinbox(frame, from_=0, to=10, width=3, textvariable=rtn_var).grid(row=row_index, column=1, sticky=NSEW)
    else:
        Label(frame, text=preset.rtn).grid(row=row_index, column=1, sticky=NSEW)
    row_index += 1

    Label(frame, text="Max Transport Latency (ms):").grid(row=row_index, column=0, sticky=NSEW)
    if custom:
        mtl_var = IntVar()
        mtl_var.set(preset.max_transport_latency)
        mtl_entry = Entry(frame, textvariable=mtl_var).grid(row=row_index, column=1, sticky=NSEW)
    else:
        Label(frame, text=preset.max_transport_latency).grid(row=row_index, column=1, sticky=NSEW)
    row_index += 1

    Label(frame, text="Presentation Delay (us):").grid(row=row_index, column=0, sticky=NSEW)
    if custom:
        pres_var = IntVar()
        pres_var.set(preset.pres_delay)
        pres_entry = Entry(frame, textvariable=pres_var).grid(row=row_index, column=1, sticky=NSEW)
    else:
        Label(frame, text=preset.pres_delay).grid(row=row_index, column=1, sticky=NSEW)
    row_index += 1


    return row_index


"""
Broadcast start command
"""
def broadcastStart(ser, preset):
    ser.write(b"bct preset " + preset + b" 0 0\n")
    ser.write(b"bct start 0\n")


def updateSubGroups(*args):
    index = 0

    for BIG in broadcastGroups:
        if BIG.num_sub_groups.get() > len(BIG.subGroups):
            BIG.subGroups.append(BroadcastSubGroup())
        elif BIG.num_sub_groups.get() < len(BIG.subGroups):
            BIG.subGroups.pop()

        populateGroup(tabs[index], BIG)
        index += 1


"""
Populate overview tab
"""
def populateOverview(tab, index):
    column_index = index + 1
    BIG = broadcastGroups[index]

    BIG.preset.set("Not_set")
    BIG.preset_drop = OptionMenu(tab, BIG.preset, *presets)
    BIG.preset_drop.grid(row=preset_row, column=column_index, padx=5, pady=5, sticky=E)

    BIG.num_sub_groups.set(0)
    BIG.num_sub_groups.trace_add("write", updateSubGroups)
    BIG.num_sub_groups_spin = Spinbox(tab, from_=0, to=255, width=3, textvariable=BIG.num_sub_groups)
    BIG.num_sub_groups_spin.grid(row=subgroups_row, column=column_index, padx=5, pady=5, sticky=E)

    BIG.packing.set("Sequential")
    packing_options = ["Sequential", "Interleaved"]
    BIG.packing_drop = OptionMenu(tab, BIG.packing, *packing_options)
    BIG.packing_drop.grid(row=packing_row, column=column_index, padx=5, pady=5, sticky=E)

    BIG.encryption.set(False)
    BIG.encryption_check = Checkbutton(tab, variable=BIG.encryption)
    BIG.encryption_check.grid(row=encryption_row, column=column_index, padx=5, pady=5, sticky=E)

    BIG.broadcastCode.set("")
    BIG.broadcastCode.trace_add("write", lambda *args: BIG.broadcastCode.set(BIG.broadcastCode.get()[:16]))
    BIG.broadcastCode_entry = Entry(tab, textvariable=BIG.broadcastCode, width=16)
    BIG.broadcastCode_entry.grid(row=broadcast_code_row, column=column_index, padx=5, pady=5, sticky=E)




def main():

    parseStructures("preset", presets)
    parseStructures("context", contexts)
    parseStructures("location", locations)

    #ser = openSerial()

    # if ser is None:
    #     logger.error("Failed to open serial port")
    #     sys.exit(1)

    # ser.write(b"bct preset 16_2_1 0 0\n")
    # ser.write(b"bct lang eng 0 0\n")
    # ser.write(b"bct start 0\n")


    Label(overview, text="BIG1", font="Helvetica 20").grid(row=0, column=2, padx=5, pady=5, sticky=E)
    Label(overview, text="BIG0", font="Helvetica 20").grid(row=0, column=1, padx=5, pady=5, sticky=E)

    # Overview
    Label(overview, text="Preset").grid(row=preset_row, column=0, padx=5, sticky=NSEW)
    Label(overview, text="Number of subgroups").grid(row=subgroups_row, column=0, padx=5, sticky=NSEW)
    Label(overview, text="Packing").grid(row=packing_row, column=0, padx=5, sticky=NSEW)
    Label(overview, text="Encryption").grid(row=encryption_row, column=0, padx=5, sticky=NSEW)
    Label(overview, text="Broadcast code (16 char)").grid(row=broadcast_code_row, column=0, padx=5, sticky=NSEW)

    populateOverview(overview, 0)
    populateOverview(overview, 1)

    start_btn = Button(overview, text="Start broadcasting", padx=5, pady=5, command= lambda: broadcastStart(ser, preset.get().encode()))
    start_btn.place(relx=0.5, rely=0.9, anchor=CENTER)

    root.mainloop()


if __name__ == "__main__":
    main()


