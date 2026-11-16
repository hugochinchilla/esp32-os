"""Parts list and netlist of the Chinchilla hat, version 0.1.

One table drives the schematic, the PCB and the BOM, so the three cannot
disagree. Pin numbers are the footprint pad numbers.

Hardware facts (pins, resistor values) must match docs/hardware/README.md.
"""

# Pin order of the two ESP32-S3-DevKitC-1 headers, as in
# tools/gen_vga_adapter_diagram.py. J1 is the left header (3V3 at pin 1),
# J3 on the dev board is our J2.
DEVKIT_LEFT = ["3V3", "3V3", "RST", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15",
               "GPIO16", "GPIO17", "GPIO18", "GPIO8", "GPIO3", "GPIO46", "GPIO9",
               "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5V", "GND"]
DEVKIT_RIGHT = ["GND", "TX", "RX", "GPIO1", "GPIO2", "GPIO42", "GPIO41", "GPIO40",
                "GPIO39", "GPIO38", "GPIO37", "GPIO36", "GPIO35", "GPIO0", "GPIO45",
                "GPIO48", "GPIO47", "GPIO21", "GPIO20", "GPIO19", "GND", "GND"]

# Which net each dev board pin connects to. Pins not listed are left open.
GPIO_NET = {
    "3V3": "+3V3", "5V": "+5V", "GND": "GND",
    # VGA
    "GPIO4": "R_B2", "GPIO5": "R_B1", "GPIO6": "R_B0",
    "GPIO7": "G_B2", "GPIO15": "G_B1", "GPIO16": "G_B0",
    "GPIO17": "B_B2", "GPIO18": "B_B1", "GPIO8": "B_B2X",
    "GPIO9": "HSYNC_IO", "GPIO10": "VSYNC_IO",
    # sound
    "GPIO13": "AUDIO_PWM", "GPIO14": "BEEP",
    # clock chip
    "GPIO1": "I2C_SDA", "GPIO2": "I2C_SCL", "GPIO11": "RTC_INT",
    # microSD, 1-bit SDMMC
    "GPIO39": "SD_CLK", "GPIO40": "SD_CMD", "GPIO41": "SD_D0", "GPIO12": "SD_CD",
    # USB host and hub power switch
    "GPIO19": "USB_DN", "GPIO20": "USB_DP",
    "GPIO21": "USB_PWR_EN", "GPIO42": "USB_FAULT",
    # spare pins brought to the expansion header
    "GPIO38": "GPIO38", "GPIO47": "GPIO47", "GPIO48": "GPIO48",
}


def devkit_pins(names):
    return {str(i + 1): GPIO_NET.get(n) for i, n in enumerate(names)}


# Custom box symbols: (pin number, pin name, side, electrical type)
CUSTOM_SYMBOLS = {
    "DevKit_Left": [(str(i + 1), n, "L", "passive") for i, n in enumerate(DEVKIT_LEFT)],
    "DevKit_Right": [(str(i + 1), n, "R", "passive") for i, n in enumerate(DEVKIT_RIGHT)],
    "VGA_HD15": [("1", "RED", "L", "passive"), ("2", "GREEN", "L", "passive"),
                 ("3", "BLUE", "L", "passive"), ("13", "HSYNC", "L", "passive"),
                 ("14", "VSYNC", "L", "passive"), ("4", "ID2", "L", "passive"),
                 ("9", "KEY/5V", "L", "passive"), ("11", "ID0", "L", "passive"),
                 ("12", "ID1/SDA", "L", "passive"), ("15", "ID3/SCL", "L", "passive"),
                 ("5", "GND", "R", "passive"), ("6", "RED_RET", "R", "passive"),
                 ("7", "GREEN_RET", "R", "passive"), ("8", "BLUE_RET", "R", "passive"),
                 ("10", "SYNC_GND", "R", "passive"), ("0", "SHELL", "R", "passive")],
    "Jack_3.5mm": [("T", "TIP", "L", "passive"), ("R", "RING", "L", "passive"),
                   ("S", "SLEEVE", "L", "passive")],
    "microSD_Det": [("1", "DAT2", "L", "passive"), ("2", "DAT3/CD", "L", "passive"),
                    ("3", "CMD", "L", "passive"), ("4", "VDD", "L", "passive"),
                    ("5", "CLK", "L", "passive"), ("6", "VSS", "L", "passive"),
                    ("7", "DAT0", "L", "passive"), ("8", "DAT1", "L", "passive"),
                    ("9", "DET_A", "R", "passive"), ("10", "DET_B", "R", "passive"),
                    ("11", "SHELL", "R", "passive")],
    "USB_A_Stacked": [("1", "VBUS_A", "L", "passive"), ("2", "D-_A", "L", "passive"),
                      ("3", "D+_A", "L", "passive"), ("4", "GND_A", "L", "passive"),
                      ("5", "VBUS_B", "L", "passive"), ("6", "D-_B", "L", "passive"),
                      ("7", "D+_B", "L", "passive"), ("8", "GND_B", "L", "passive"),
                      ("9", "SHELL", "R", "passive")],
    "USBLC6-2SC6": [("1", "IO1", "L", "passive"), ("6", "IO1", "R", "passive"),
                    ("3", "IO2", "L", "passive"), ("4", "IO2", "R", "passive"),
                    ("5", "VBUS", "R", "passive"), ("2", "GND", "L", "passive")],
    "Header_Piezo": [("1", "3V3", "L", "passive"), ("2", "BEEP", "L", "passive")],
    "Header_1x06": [(str(i + 1), n, "L", "passive") for i, n in
                    enumerate(["3V3", "GPIO38", "GPIO47", "GPIO48", "5V", "GND"])],
}

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"

# Generic passives: the assembler may use any equivalent part.
R_MPN = {
    "1k": ("YAGEO", "RC0603FR-071KL", ""),
    "10k": ("YAGEO", "RC0603FR-0710KL", ""),
}
C_MPN = {
    "100n": ("Samsung", "CL10B104KB8NNNC", "C1591", FP_C, "100 nF 50 V X7R 0603"),
    "10u": ("Samsung", "CL21A106KAYNNNE", "C15850", FP_C0805, "10 uF 25 V X5R 0805"),
}

PARTS = []


def part(ref, sym, fp, value, pins, group, mfr="", mpn="", lcsc="", desc="", dnp=False,
         who="self"):
    """who: "assembler" = placed by the board maker, "self" = soldered by hand."""
    PARTS.append(dict(ref=ref, sym=sym, fp=fp, value=value, pins=pins, group=group,
                      mfr=mfr, mpn=mpn, lcsc=lcsc, desc=desc, dnp=dnp, who=who))


def R(ref, value, a, b, group):
    mfr, mpn, lcsc = R_MPN[value]
    part(ref, "Device:R", FP_R, value, {"1": a, "2": b}, group, mfr, mpn, lcsc,
         f"Resistor {value} ohm 1% 0603", who="assembler")


def C(ref, value, a, b, group):
    mfr, mpn, lcsc, fp, desc = C_MPN[value]
    part(ref, "Device:C", fp, value.replace("u", " uF").replace("n", " nF"),
         {"1": a, "2": b}, group, mfr, mpn, lcsc, desc, who="assembler")


# ---- Dev board sockets ------------------------------------------------------
SOCKET = ("Wurth Elektronik", "61302211821", "", "Socket strip 1x22, 2.54 mm, vertical")
part("J1", "hat:DevKit_Left", "Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical",
     "DevKitC left", devkit_pins(DEVKIT_LEFT), "devkit", *SOCKET)
part("J2", "hat:DevKit_Right", "Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical",
     "DevKitC right", devkit_pins(DEVKIT_RIGHT), "devkit", *SOCKET)

# ---- VGA: 3-bit resistor ladders from 1k resistors only ------------------------
# Top bit: two 1k in parallel (500 ohm). Middle bit: one 1k. Low bit: two 1k in
# series (2k). Exact 1:2:4 ratio; full white is 0.69 V into 75 ohm. Sync lines
# go through two 1k in parallel (500 ohm). One resistor value keeps the
# assembler's per-part fee down.
VGA_LADDER = []   # (ref, from net, to net, column GPIO, row) for the PCB placement
_n = [0]


def _vr(a, b, gpio, row):
    _n[0] += 1
    ref = f"R{_n[0]}"
    R(ref, "1k", a, b, "vga")
    VGA_LADDER.append((ref, gpio, row))


for colour, (top, mid, low) in {"R": ("GPIO4", "GPIO5", "GPIO6"),
                                "G": ("GPIO7", "GPIO15", "GPIO16"),
                                "B": ("GPIO17", "GPIO18", "GPIO8")}.items():
    out = f"VGA_{colour}"
    _vr(GPIO_NET[top], out, top, 0)
    _vr(GPIO_NET[top], out, top, 1)
    _vr(GPIO_NET[mid], out, mid, 0)
    _vr(GPIO_NET[low], f"{colour}_LOW_MID", low, 0)
    _vr(f"{colour}_LOW_MID", out, low, 1)
for gpio, out in (("GPIO9", "VGA_HSYNC"), ("GPIO10", "VGA_VSYNC")):
    _vr(GPIO_NET[gpio], out, gpio, 0)
    _vr(GPIO_NET[gpio], out, gpio, 1)
_n0 = _n[0]

# Every ground pin of the connector and its shell go to the ground plane:
# on the hand-built board, pin 10 alone was not enough for a picture.
part("J3", "hat:VGA_HD15",
     "Connector_Dsub:DSUB-15-HD_Female_Horizontal_P2.29x1.98mm_EdgePinOffset3.03mm_Housed_MountingHolesOffset4.94mm",
     "VGA", {"1": "VGA_R", "2": "VGA_G", "3": "VGA_B", "13": "VGA_HSYNC", "14": "VGA_VSYNC",
             "5": "GND", "6": "GND", "7": "GND", "8": "GND", "10": "GND", "0": "GND",
             "4": None, "9": None, "11": None, "12": None, "15": None},
     "vga", "Amphenol ICC", "L77HDE15SD1CH4F", "",
     "HD15 (VGA) female, right angle, through hole. Check the footprint against the part actually used")

# ---- Audio out and beeper -------------------------------------------------------
R("R20", "1k", "AUDIO_PWM", "AUDIO_FILT", "audio")
C("C1", "100n", "AUDIO_FILT", "GND", "audio")
C("C2", "10u", "AUDIO_FILT", "AUDIO_OUT", "audio")
# The jack sits on the back edge next to VGA and USB, so the enclosure needs one panel
part("J4", "hat:Jack_3.5mm", "Connector_Audio:Jack_3.5mm_CUI_SJ1-3523N_Horizontal", "Audio out",
     {"T": "AUDIO_OUT", "R": "AUDIO_OUT", "S": "GND"}, "audio",
     "Same Sky (CUI)", "SJ1-3523N", "", "3.5 mm stereo jack, through hole; mono signal on tip and ring")
# Piezo between 3V3 and GPIO14; the firmware keeps GPIO14 high when silent.
part("J8", "hat:Header_Piezo", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
     "Piezo", {"1": "+3V3", "2": "BEEP"}, "audio", "", "", "",
     "1x2 pin header 2.54 mm, to a passive piezo sounder")

# ---- Clock chip: DS3231MZ on I2C, CR2032 backup ----------------------------
part("U1", "Timer_RTC:DS3231MZ", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "DS3231MZ",
     {"1": None, "2": "+3V3", "3": "RTC_INT", "4": None, "5": "GND", "6": "VBAT",
      "7": "I2C_SDA", "8": "I2C_SCL"}, "rtc", "Analog Devices", "DS3231MZ+TRL", "",
     "RTC, I2C, MEMS resonator +-5 ppm, SOIC-8", who="assembler")
C("C3", "100n", "+3V3", "GND", "rtc")
C("C4", "100n", "VBAT", "GND", "rtc")
R("R21", "10k", "+3V3", "I2C_SDA", "rtc")
R("R22", "10k", "+3V3", "I2C_SCL", "rtc")
R("R23", "10k", "+3V3", "RTC_INT", "rtc")
part("BT1", "Device:Battery_Cell", "Battery:BatteryHolder_Keystone_3034_1x20mm", "CR2032 holder",
     {"1": "VBAT", "2": "GND"}, "rtc", "Keystone", "3034", "",
     "Coin cell holder for CR2032, SMD with large pads (hand solder). Insert a CR2032")

# ---- microSD, 1-bit SDMMC -----------------------------------------------------
part("J5", "hat:microSD_Det", "Connector_Card:microSD_HC_Molex_104031-0811", "microSD",
     {"1": "SD_D2", "2": "SD_D3", "3": "SD_CMD", "4": "+3V3", "5": "SD_CLK", "6": "GND",
      "7": "SD_D0", "8": "SD_D1", "9": "SD_CD", "10": "GND", "11": "GND"}, "sd",
     "Molex", "1040310811", "", "microSD socket, push-push, with card detect switch (hand solder, 1.1 mm pitch)")
R("R24", "10k", "+3V3", "SD_CMD", "sd")
R("R25", "10k", "+3V3", "SD_D0", "sd")
R("R26", "10k", "+3V3", "SD_D1", "sd")
R("R27", "10k", "+3V3", "SD_D2", "sd")
R("R28", "10k", "+3V3", "SD_D3", "sd")
R("R29", "10k", "+3V3", "SD_CD", "sd")
C("C5", "100n", "+3V3", "GND", "sd")
C("C6", "10u", "+3V3", "GND", "sd")

# ---- USB hub: CH334R, bus powered, two USB-A ports ------------------------------
part("U2", "Interface_USB:CH334R", "Package_SO:QSOP-16_3.9x4.9mm_P0.635mm", "CH334R",
     {"1": None, "2": None, "3": None, "4": None, "5": "USB2_DN", "6": "USB2_DP",
      "7": "USB1_DN", "8": "USB1_DP", "9": None, "10": "USB_DN", "11": "USB_DP",
      "12": "+5V", "13": "HUB_3V3", "14": "GND", "15": "HUB_XO", "16": "HUB_XI"}, "usb",
     "WCH", "CH334R", "C4154405", "USB 2.0 4-port hub controller, QSOP-16 (2 ports used)",
     who="assembler")
part("Y1", "Device:Crystal", "Crystal:Crystal_HC49-U_Vertical", "12 MHz",
     {"1": "HUB_XI", "2": "HUB_XO"}, "usb", "", "", "",
     "Crystal 12 MHz, HC-49 through hole, 12 to 20 pF load (the hub has its own load capacitors)")
C("C7", "10u", "+5V", "GND", "usb")
C("C8", "100n", "HUB_3V3", "GND", "usb")
C("C9", "10u", "HUB_3V3", "GND", "usb")
# Downstream power: one current-limited switch for both ports (GPIO21 on/off,
# GPIO42 reads overcurrent). On by default through R23.
part("U3", "Power_Management:TPS2051CDBV", "Package_TO_SOT_SMD:SOT-23-5", "TPS2051C",
     {"1": "VBUS_DN", "2": "GND", "3": "USB_FAULT", "4": "USB_PWR_EN", "5": "+5V"}, "usb",
     "Texas Instruments", "TPS2051CDBVR", "", "USB power switch, 0.5 A, active-high enable",
     who="assembler")
R("R30", "10k", "+3V3", "USB_PWR_EN", "usb")
R("R31", "10k", "+3V3", "USB_FAULT", "usb")
C("C10", "10u", "+5V", "GND", "usb")
C("C11", "10u", "VBUS_DN", "GND", "usb")
part("C12", "Device:C_Polarized", "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", "100 uF",
     {"1": "VBUS_DN", "2": "GND"}, "usb", "", "", "",
     "Electrolytic 100 uF 10 V or more, radial 6.3 mm, 2.5 mm pitch")
part("U4", "hat:USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", "USBLC6-2SC6",
     {"1": "USB1_DP", "6": "USB1_DP", "3": "USB1_DN", "4": "USB1_DN", "5": "VBUS_DN", "2": "GND"},
     "usb", "STMicroelectronics", "USBLC6-2SC6", "C7519", "USB ESD protection", who="assembler")
part("U5", "hat:USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", "USBLC6-2SC6",
     {"1": "USB2_DP", "6": "USB2_DP", "3": "USB2_DN", "4": "USB2_DN", "5": "VBUS_DN", "2": "GND"},
     "usb", "STMicroelectronics", "USBLC6-2SC6", "C7519", "USB ESD protection", who="assembler")
part("J6", "hat:USB_A_Stacked", "Connector_USB:USB_A_Wuerth_61400826021_Horizontal_Stacked",
     "USB-A x2", {"1": "VBUS_DN", "2": "USB1_DN", "3": "USB1_DP", "4": "GND",
                  "5": "VBUS_DN", "6": "USB2_DN", "7": "USB2_DP", "8": "GND", "9": "GND"},
     "usb", "Wurth Elektronik", "61400826021", "", "Dual stacked USB-A socket, through hole")

# ---- Expansion header (not fitted) ------------------------------------------------
part("J7", "hat:Header_1x06", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
     "Expansion", {"1": "+3V3", "2": "GPIO38", "3": "GPIO47", "4": "GPIO48", "5": "+5V", "6": "GND"},
     "devkit", "", "", "", "1x6 pin header, 2.54 mm. Not fitted", dnp=True)

POWER_NETS = ["+3V3", "+5V", "GND", "VBAT"]


def nets():
    out = {}
    for p in PARTS:
        for pin, net in p["pins"].items():
            if net:
                out.setdefault(net, []).append((p["ref"], pin))
    return out
