"""
SmartAttendance Technician & Engineering Manual Generator.
Formatted in Times New Roman typography with 12pt body text.
Exhaustive, highly technical engineering guide for hardware developers,
embedded engineers, and maintenance technicians:
- Physical layout: Slide power switch on left side (UP=ON, DOWN=OFF), USB-C charging slot below switch (5V 2-3A),
  MicroSD memory card on the bottom slot (DO NOT REMOVE!), camera lens at top of screen
- SoC architecture, Hex-mode PSRAM @ 200MHz, ESP-IDF sdkconfig
- Board-level pinouts, schematics, J7 headers, and LDO power rails
- MIPI-DSI (EK79007) and MIPI-CSI (SC2336) register configurations and timings
- GT911 touch controller manual reset/interrupt sequence and I2C locking
- Deep learning inference pipeline (ESP-DL, MobileFaceNet int8, 128D embeddings)
- Temporal consensus voting, double-buffering DMA architecture, tiered margin gating
- Power architecture, battery fuel gauge (STC8), ADC calibration formulas, color states (cyan charging, red <15%, white normal; no audio/speaker)
- FreeRTOS task prioritization, memory management (SPIRAM caps), and mutex safety
- Firmware flashing, partition table layout, dual-bank OTA updates, and board repair
"""

import os
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.pdfgen import canvas
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_RIGHT, TA_JUSTIFY
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase.pdfmetrics import registerFontFamily

# Typography Setup: Genuine Times New Roman with Core Fallback
win_fonts = "C:\\Windows\\Fonts"
if os.path.exists(os.path.join(win_fonts, "times.ttf")):
    pdfmetrics.registerFont(TTFont("TimesNewRoman", os.path.join(win_fonts, "times.ttf")))
    pdfmetrics.registerFont(TTFont("TimesNewRoman-Bold", os.path.join(win_fonts, "timesbd.ttf")))
    pdfmetrics.registerFont(TTFont("TimesNewRoman-Italic", os.path.join(win_fonts, "timesi.ttf")))
    pdfmetrics.registerFont(TTFont("TimesNewRoman-BoldItalic", os.path.join(win_fonts, "timesbi.ttf")))
    registerFontFamily(
        "TimesNewRoman",
        normal="TimesNewRoman",
        bold="TimesNewRoman-Bold",
        italic="TimesNewRoman-Italic",
        boldItalic="TimesNewRoman-BoldItalic"
    )
    F_NORMAL = "TimesNewRoman"
    F_BOLD = "TimesNewRoman-Bold"
    F_ITALIC = "TimesNewRoman-Italic"
    F_BOLD_ITALIC = "TimesNewRoman-BoldItalic"
else:
    F_NORMAL = "Times-Roman"
    F_BOLD = "Times-Bold"
    F_ITALIC = "Times-Italic"
    F_BOLD_ITALIC = "Times-BoldItalic"

# High-contrast engineering palette
C_PRIMARY = colors.HexColor("#0F172A")    # Deep Slate
C_SECONDARY = colors.HexColor("#0284C7")  # Technical Blue
C_DARK = colors.HexColor("#1E293B")       # Dark Charcoal
C_BODY = colors.HexColor("#1E293B")       # Dark Slate Body
C_MUTED = colors.HexColor("#64748B")      # Muted Slate
C_BG_LIGHT = colors.HexColor("#F8FAFC")   # Light Code Background
C_BORDER = colors.HexColor("#CBD5E1")     # Grid Border
C_CARD_BG = colors.HexColor("#F1F5F9")    # Component Card Background

C_SUCCESS_BORDER = colors.HexColor("#10B981")
C_SUCCESS_BG = colors.HexColor("#ECFDF5")
C_WARN_BORDER = colors.HexColor("#F59E0B")
C_WARN_BG = colors.HexColor("#FFFBEB")
C_DANGER_BORDER = colors.HexColor("#EF4444")
C_DANGER_BG = colors.HexColor("#FEF2F2")
C_INFO_BORDER = colors.HexColor("#3B82F6")
C_INFO_BG = colors.HexColor("#EFF6FF")

PAGE_WIDTH, PAGE_HEIGHT = letter
MARGIN = 36  # 0.5 inch margins = 540 pt usable width
USABLE_WIDTH = PAGE_WIDTH - (2 * MARGIN)


class TechNumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            if self._pageNumber > 1:
                self.draw_header_footer(num_pages)
            canvas.Canvas.showPage(self)
        canvas.Canvas.save(self)

    def draw_header_footer(self, total_pages):
        self.saveState()
        self.setFont(F_NORMAL, 9)
        self.setFillColor(C_MUTED)

        # Header
        header_text = "SmartAttendance™ — Hardware Architecture & Technician Engineering Manual"
        self.drawString(MARGIN, PAGE_HEIGHT - 25, header_text)
        self.setStrokeColor(C_BORDER)
        self.setLineWidth(0.5)
        self.line(MARGIN, PAGE_HEIGHT - 28, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - 28)

        # Footer
        footer_left = "Elecrow CrowPanel Advanced 7″ ESP32-P4 v1.0 | Engineering Reference"
        footer_right = f"Page {self._pageNumber} of {total_pages}"
        self.drawString(MARGIN, 20, footer_left)
        self.drawRightString(PAGE_WIDTH - MARGIN, 20, footer_right)
        self.line(MARGIN, 28, PAGE_WIDTH - MARGIN, 28)

        self.restoreState()


def get_styles():
    base = getSampleStyleSheet()
    return {
        "CoverTag": ParagraphStyle(
            "CoverTag", parent=base["Normal"], fontName=F_BOLD,
            fontSize=11, leading=15, textColor=C_SECONDARY, spaceAfter=6,
        ),
        "CoverTitle": ParagraphStyle(
            "CoverTitle", parent=base["Normal"], fontName=F_BOLD,
            fontSize=26, leading=32, textColor=C_PRIMARY, spaceAfter=8,
        ),
        "CoverSubtitle": ParagraphStyle(
            "CoverSubtitle", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=13.5, leading=18, textColor=C_DARK, spaceAfter=14,
        ),
        "CoverMeta": ParagraphStyle(
            "CoverMeta", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=10, leading=14, textColor=C_BODY,
        ),
        "CoverMetaBold": ParagraphStyle(
            "CoverMetaBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10, leading=14, textColor=C_PRIMARY,
        ),
        "H1": ParagraphStyle(
            "H1", parent=base["Heading1"], fontName=F_BOLD,
            fontSize=16, leading=20, textColor=C_PRIMARY, spaceBefore=12, spaceAfter=5, keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2", parent=base["Heading2"], fontName=F_BOLD,
            fontSize=12.5, leading=16, textColor=C_SECONDARY, spaceBefore=8, spaceAfter=4, keepWithNext=True,
        ),
        "H3": ParagraphStyle(
            "H3", parent=base["Heading3"], fontName=F_BOLD,
            fontSize=11, leading=14.5, textColor=C_DARK, spaceBefore=6, spaceAfter=3, keepWithNext=True,
        ),
        # Requested: Font size 12 for the body text in Times New Roman
        "Body": ParagraphStyle(
            "Body", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, spaceAfter=4.5,
        ),
        "Bullet": ParagraphStyle(
            "Bullet", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, leftIndent=14, firstLineIndent=-10, spaceAfter=3,
        ),
        "CodeText": ParagraphStyle(
            "CodeText", parent=base["Normal"], fontName="Courier",
            fontSize=7.5, leading=10, textColor=C_DARK,
        ),
        "TableHeader": ParagraphStyle(
            "TableHeader", parent=base["Normal"], fontName=F_BOLD,
            fontSize=8.5, leading=12, textColor=colors.white,
        ),
        "TableCell": ParagraphStyle(
            "TableCell", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=8, leading=11, textColor=C_BODY,
        ),
        "TableCellBold": ParagraphStyle(
            "TableCellBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=8, leading=11, textColor=C_DARK,
        ),
        "CalloutBold": ParagraphStyle(
            "CalloutBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10, leading=14, textColor=C_PRIMARY,
        ),
        "CalloutText": ParagraphStyle(
            "CalloutText", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=9.5, leading=13.5, textColor=C_DARK,
        ),
        "TOCItem": ParagraphStyle(
            "TOCItem", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=9.5, leading=13.5, textColor=C_DARK,
        ),
        "TOCItemBold": ParagraphStyle(
            "TOCItemBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10, leading=13.5, textColor=C_PRIMARY,
        ),
    }


def make_callout(box_type, title, content, styles):
    type_map = {
        "info": (C_INFO_BORDER, C_INFO_BG, "[i] ENGINEERING NOTE:"),
        "tip": (C_SUCCESS_BORDER, C_SUCCESS_BG, "[v] DESIGN RECOMMENDATION:"),
        "warn": (C_WARN_BORDER, C_WARN_BG, "[!] HARDWARE WARNING:"),
        "danger": (C_DANGER_BORDER, C_DANGER_BG, "[X] CRITICAL CONSTRAINT:"),
    }
    border, bg, prefix = type_map.get(box_type.lower(), (C_INFO_BORDER, C_INFO_BG, "NOTE:"))
    cells = [Paragraph(f"<b>{prefix} {title}</b>", styles["CalloutBold"]), Spacer(1, 2)]
    if isinstance(content, list):
        for c in content:
            cells.append(Paragraph(c, styles["CalloutText"]))
    else:
        cells.append(Paragraph(content, styles["CalloutText"]))

    t = Table([[cells]], colWidths=[USABLE_WIDTH])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), bg),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("LINEBEFORE", (0, 0), (0, -1), 3.5, border),
        ("PADDING", (0, 0), (-1, -1), 5),
    ]))
    return t


def build_technician_story():
    styles = get_styles()
    story = []

    # COVER PAGE
    story.append(Spacer(1, 15))
    story.append(Paragraph("TECHNICAL SPECIFICATION & ENGINEERING MANUAL", styles["CoverTag"]))
    story.append(Paragraph("SmartAttendance™ Hardware & Firmware Reference", styles["CoverTitle"]))
    story.append(Paragraph(
        "Low-Level Subsystem Architecture, Pinout Matrix, MIPI CSI/DSI Drivers, "
        "ESP-DL Neural Inference, Power Topology, FreeRTOS Concurrency & Board Diagnostics",
        styles["CoverSubtitle"]
    ))

    story.append(HRFlowable(width="100%", thickness=2.5, color=C_SECONDARY, spaceBefore=2, spaceAfter=14))

    highlights = [
        "<b>SoC & Memory:</b> Espressif ESP32-P4 RISC-V Dual-Core @ 400 MHz + 32MB Hex-PSRAM @ 200 MHz",
        "<b>Physical Controls:</b> Left-side slide switch (UP=ON, DOWN=OFF), USB-C charging slot (5V 2-3A)",
        "<b>Display Bridge:</b> EK79007 MIPI-DSI (2 lanes @ 900 Mbps, 51 MHz DPI clock, 1024×600 IPS panel)",
        "<b>Touch Controller:</b> GT911 Capacitive Touch with verified manual GPIO40/42 I2C address lock (0x5D)",
        "<b>Camera Subsystem:</b> SC2336 24-Pin MIPI-CSI (SCCB 0x36) at top of screen, hardware ISP Bayer BGGR",
        "<b>DMA & Concurrency:</b> Double-buffered frame acquisition in PSRAM (`s_detection_fb_buf`) eliminating tearing",
        "<b>Edge AI Models:</b> Quantized int8 ESP-DL FaceDetect (MSR/MNP) & MobileFaceNet 128D embedding extractor",
        "<b>Power & Gas Gauge:</b> Dual power rails (LDO3 2.5V, LDO4 3.3V), STC8 battery fuel gauge (no audio/speaker)",
        "<b>Storage Architecture:</b> SDMMC 1-bit FAT32 on bottom slot (`/sdcard`) with SQLite3 DB (DO NOT REMOVE!)",
    ]
    cov_p = [Paragraph(f"• {h}", styles["Body"]) for h in highlights]
    t_cov = Table([[cov_p]], colWidths=[USABLE_WIDTH])
    t_cov.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 1, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 9),
    ]))
    story.append(t_cov)
    story.append(Spacer(1, 18))

    meta = [
        [
            Paragraph("<b>Target Audience:</b>", styles["CoverMetaBold"]),
            Paragraph("Embedded Engineers, Firmware Devs, Hardware Techs", styles["CoverMeta"]),
            Paragraph("<b>Hardware Revision:</b>", styles["CoverMetaBold"]),
            Paragraph("CrowPanel Advanced 7″ ESP32-P4 v1.0", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Framework / SDK:</b>", styles["CoverMetaBold"]),
            Paragraph("ESP-IDF v5.3+ / ESP-DL v1.1.4+", styles["CoverMeta"]),
            Paragraph("<b>Typography:</b>", styles["CoverMetaBold"]),
            Paragraph("Times New Roman (12pt Body)", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Document ID:</b>", styles["CoverMetaBold"]),
            Paragraph("SA-ESP32P4-ENG-2026-REV6", styles["CoverMeta"]),
            Paragraph("<b>Publication Date:</b>", styles["CoverMetaBold"]),
            Paragraph("October 2026", styles["CoverMeta"]),
        ],
    ]
    t_meta = Table(meta, colWidths=[110, 160, 120, 150])
    t_meta.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("PADDING", (0, 0), (-1, -1), 2.5),
    ]))
    story.append(t_meta)
    story.append(Spacer(1, 14))

    story.append(make_callout(
        "warn",
        "Confidential Engineering Document",
        "This manual contains low-level hardware schematics, register configurations, and flash partition offsets intended exclusively for technical personnel. End-user operating guidelines are documented in the companion SmartAttendance User Manual.",
        styles
    ))

    story.append(PageBreak())

    # TABLE OF CONTENTS
    story.append(Paragraph("Engineering Table of Contents", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1.5, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    sections = [
        ("Section 1: Hardware Architecture & Electrical Pinout", "Detailed pin mapping, power rail sequencing (LDO3/4), bus interfaces, slide switch"),
        ("Section 2: MIPI-DSI Display & Backlight Subsystems", "EK79007 bridge parameters, LVGL 9 SPIRAM buffers, GPIO31 PWM"),
        ("Section 3: GT911 Capacitive Touch Controller Quirks", "Manual GPIO reset sequence, 16-bit register commands, wake-up sequence"),
        ("Section 4: MIPI-CSI Camera Driver & ISP Pipeline", "SC2336 24-pin CSI at top of screen, SCCB 0x36, Bayer BGGR, exposure timing, DMA buffer"),
        ("Section 5: High-Speed PSRAM & Flash Memory Architecture", "Hex-mode PSRAM @ 200MHz, experimental symbols, heap allocation caps"),
        ("Section 6: Power Supply, Battery Telemetry & STC8 Gauge", "Left-side USB-C 5V 2-3A, ADC voltage sense, STC8 UART packets, color states (no audio)"),
        ("Section 7: Deep Learning Vision Engine & Face Embeddings", "ESP-DL inference, 5-point landmark warping, MobileFaceNet 128D int8 vectors"),
        ("Section 8: Multi-Frame Temporal Consensus & Margin Gating", "6-frame window, 3-frame early exit, dynamic margin delta, anti-glare math"),
        ("Section 9: FreeRTOS Task Architecture & Mutex Locking", "Task priorities, stack sizing, UI recursive mutex safety, deadlock avoidance"),
        ("Section 10: Partition Table & Dual-Bank OTA Flashing", "6MB app partitions, factory fallback, esp_ota routines, flashing scripts"),
        ("Section 11: SQLite3 Schema & On-Disk Data Structures", "Bottom MicroSD card (`attendance.db`), index optimization, backup mirroring (DO NOT REMOVE!)"),
        ("Section 12: Board-Level Diagnostics & Hardware Repair", "Multimeter test points, FPC connector troubleshooting, recovery flashing"),
    ]
    t_toc = []
    for s_title, s_desc in sections:
        t_toc.append([
            Paragraph(f"<b>{s_title}</b>", styles["TOCItemBold"]),
            Paragraph(f"<font color='{C_MUTED.hexval()}'>{s_desc}</font>", styles["TOCItem"]),
        ])
    t_table = Table(t_toc, colWidths=[200, 340])
    t_table.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("PADDING", (0, 0), (-1, -1), 3),
        ("LINEBELOW", (0, 0), (-1, -1), 0.5, C_BG_LIGHT),
    ]))
    story.append(t_table)
    story.append(PageBreak())

    # SECTION 1: HARDWARE ARCHITECTURE & PINOUT
    story.append(Paragraph("Section 1: Hardware Architecture & Electrical Pinout", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The Elecrow CrowPanel Advanced 7″ ESP32-P4 v1.0 integrates the ESP32-P4 SoC with external Hex-PSRAM, MIPI CSI/DSI bridges, and power management circuits. Physical layout verified from hands-on testing:",
        styles["Body"]
    ))
    layout_points = [
        "<b>Power Switch:</b> Physical slide switch located on the <b>left edge</b> (slide UP to connect battery/power, slide DOWN to disconnect).",
        "<b>Power / Charging Input:</b> USB Type-C port located on the <b>left edge directly below the slide switch</b>. Recommended power supply: <b>5.0V, 2.0A to 3.0A DC</b>.",
        "<b>Camera Module:</b> OmniVision SC2336 mounted along the <b>top edge of the screen</b> via 24-pin ribbon cable.",
        "<b>MicroSD Storage:</b> High-speed SDMMC slot located on the <b>bottom edge of the board</b>. <font color='#EF4444'><b>MANDATORY: DO NOT REMOVE DURING OPERATION!</b></font> The slot holds <code>attendance.db</code> and AI model weights.",
        "<b>Audio Subsystem Notice:</b> The audio amplifier and speaker have been permanently stripped from firmware v1.2 (2026-06-11). Audio defines in <code>config.h</code> are inactive tombstones.",
    ]
    for lp in layout_points:
        story.append(Paragraph(f"• {lp}", styles["Bullet"]))

    story.append(Spacer(1, 4))
    story.append(Paragraph("Hardware Pinout & Bus Mapping Matrix", styles["H2"]))

    pin_matrix = [
        [Paragraph("<b>Peripheral</b>", styles["TableHeader"]), Paragraph("<b>GPIO Pin</b>", styles["TableHeader"]), Paragraph("<b>Interface / Net Name</b>", styles["TableHeader"]), Paragraph("<b>Engineering Notes</b>", styles["TableHeader"])],
        [Paragraph("Backlight PWM", styles["TableCellBold"]), Paragraph("GPIO 31", styles["TableCell"]), Paragraph("LEDC PWM Channel 0", styles["TableCell"]), Paragraph("30 kHz carrier frequency, 11-bit resolution (duty 200-2047).", styles["TableCell"])],
        [Paragraph("Touch SDA", styles["TableCellBold"]), Paragraph("GPIO 45", styles["TableCell"]), Paragraph("I2C_NUM_0 SDA", styles["TableCell"]), Paragraph("Requires external 4.7kΩ pull-up to 3.3V.", styles["TableCell"])],
        [Paragraph("Touch SCL", styles["TableCellBold"]), Paragraph("GPIO 46", styles["TableCell"]), Paragraph("I2C_NUM_0 SCL", styles["TableCell"]), Paragraph("100 kHz standard clock speed with pull-up.", styles["TableCell"])],
        [Paragraph("Touch Reset", styles["TableCellBold"]), Paragraph("GPIO 40", styles["TableCell"]), Paragraph("GT911_RST", styles["TableCell"]), Paragraph("Must be driven via manual GPIO pulse to fix 0x5D address.", styles["TableCell"])],
        [Paragraph("Touch Interrupt", styles["TableCellBold"]), Paragraph("GPIO 42", styles["TableCell"]), Paragraph("GT911_INT", styles["TableCell"]), Paragraph("Hold low during RST release to latch address 0x5D.", styles["TableCell"])],
        [Paragraph("Camera CSI Data", styles["TableCellBold"]), Paragraph("DSI/CSI PHY", styles["TableCell"]), Paragraph("MIPI-CSI 2-Lane", styles["TableCell"]), Paragraph("OmniVision SC2336 24-pin FPC ribbon; RAW8 format at top of screen.", styles["TableCell"])],
        [Paragraph("Camera SCCB SDA", styles["TableCellBold"]), Paragraph("GPIO 7", styles["TableCell"]), Paragraph("SCCB I2C SDA", styles["TableCell"]), Paragraph("Sensor address 0x36 (alternate address; NOT 0x30).", styles["TableCell"])],
        [Paragraph("Camera SCCB SCL", styles["TableCellBold"]), Paragraph("GPIO 8", styles["TableCell"]), Paragraph("SCCB I2C SCL", styles["TableCell"]), Paragraph("Clock for camera sensor register read/write.", styles["TableCell"])],
        [Paragraph("SDMMC Clock", styles["TableCellBold"]), Paragraph("GPIO 43", styles["TableCell"]), Paragraph("SD_CLK", styles["TableCell"]), Paragraph("SDMMC 1-bit clock line; 20 MHz to 40 MHz clock (bottom slot).", styles["TableCell"])],
        [Paragraph("SDMMC Command", styles["TableCellBold"]), Paragraph("GPIO 44", styles["TableCell"]), Paragraph("SD_CMD", styles["TableCell"]), Paragraph("SDMMC command line with internal pull-up enabled.", styles["TableCell"])],
        [Paragraph("SDMMC Data 0", styles["TableCellBold"]), Paragraph("GPIO 39", styles["TableCell"]), Paragraph("SD_D0", styles["TableCell"]), Paragraph("SDMMC data 0 bidirectional transfer.", styles["TableCell"])],
        [Paragraph("Battery ADC", styles["TableCellBold"]), Paragraph("GPIO 4", styles["TableCell"]), Paragraph("ADC_CHANNEL_4", styles["TableCell"]), Paragraph("Monitors 3.3V to 4.2V battery via resistor divider network.", styles["TableCell"])],
        [Paragraph("DS1302 RTC CLK", styles["TableCellBold"]), Paragraph("GPIO 51", styles["TableCell"]), Paragraph("J7 Header IO51", styles["TableCell"]), Paragraph("Serial clock for external battery-backed RTC.", styles["TableCell"])],
        [Paragraph("DS1302 RTC DAT", styles["TableCellBold"]), Paragraph("GPIO 50", styles["TableCell"]), Paragraph("J7 Header IO50", styles["TableCell"]), Paragraph("Bidirectional serial data line for RTC register transfers.", styles["TableCell"])],
        [Paragraph("DS1302 RTC CE", styles["TableCellBold"]), Paragraph("GPIO 49", styles["TableCell"]), Paragraph("J7 Header IO49", styles["TableCell"]), Paragraph("Chip Enable (active high) for DS1302 bus access.", styles["TableCell"])],
    ]
    t_pm = Table(pin_matrix, colWidths=[90, 75, 95, 280])
    t_pm.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 2.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_pm)

    story.append(Spacer(1, 4))
    story.append(make_callout(
        "danger",
        "Power Rail Sequencing (LDO3 & LDO4)",
        "The EK79007 display bridge and MIPI PHY require internal LDO3 (2.5 V) and LDO4 (3.3 V) rails. If firmware fails to initialize these PMU regulators prior to display driver initialization, the MIPI-DSI link will fail to negotiate clock synchronization, leaving the panel permanently dark.",
        styles
    ))

    story.append(PageBreak())

    # SECTION 2 & 3: DISPLAY, TOUCH & MIPI PIPELINE
    story.append(Paragraph("Section 2: MIPI-DSI Display & Backlight Subsystems", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The display panel connects via a 2-lane MIPI-DSI interface managed by an EK79007 driver IC. Key physical parameters:",
        styles["Body"]
    ))
    dsi_points = [
        "<b>Lane Bit Rate:</b> 900 Mbps across 2 differential data lanes.",
        "<b>DPI Pixel Clock:</b> 51 MHz nominal.",
        "<b>Resolution & Porch Timing:</b> 1024×600 active pixels. HSYNC pulse: 20, HBP: 140, HFP: 160. VSYNC pulse: 3, VBP: 20, VFP: 12.",
        "<b>Critical Driver Constraint:</b> Do <b>NOT</b> call <code>esp_lcd_panel_disp_on_off()</code>. The EK79007 bridge does not support software display power toggle; attempting to do so locks the MIPI PHY.",
        "<b>LVGL 9 Optimization:</b> Allocate draw buffers strictly from PSRAM: <code>swap_bytes=false</code>, <code>buff_spiram=true</code>, <code>sw_rotate=true</code>, <code>double_buffer=true</code>.",
        "<b>Backlight Formula:</b> LEDC 30 kHz on GPIO31. Duty formula: <code>duty = (pct != 0) ? ((pct * 18) + 200) : 0</code>.",
    ]
    for dp in dsi_points:
        story.append(Paragraph(f"• {dp}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 3: GT911 Capacitive Touch Controller Quirks", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The Goodix GT911 capacitive touch IC requires a precise hardware reset timing sequence to latch its I2C slave address at <code>0x5D</code>. Standard generic drivers fail because they do not control RST and INT with sub-millisecond precision.",
        styles["Body"]
    ))

    code_touch = (
        "// 1. Install legacy I2C driver on I2C_NUM_0 (GPIO 45 SDA, GPIO 46 SCL, 100 kHz)\n"
        "i2c_config_t i2c_conf = {\n"
        "    .mode = I2C_MODE_MASTER, .sda_io_num = 45, .scl_io_num = 46,\n"
        "    .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,\n"
        "    .master.clk_speed = 100000,\n"
        "};\n"
        "i2c_param_config(I2C_NUM_0, &i2c_conf);\n"
        "i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);\n\n"
        "// 2. Hardware timing pulse to force address 0x5D:\n"
        "gpio_set_direction(40, GPIO_MODE_OUTPUT); // RST\n"
        "gpio_set_direction(42, GPIO_MODE_OUTPUT); // INT\n"
        "gpio_set_level(40, 0); gpio_set_level(42, 0);  // Both driven low\n"
        "vTaskDelay(pdMS_TO_TICKS(1));\n"
        "gpio_set_level(40, 1);                          // Release RST high while INT stays low\n"
        "vTaskDelay(pdMS_TO_TICKS(50));\n"
        "gpio_set_level(42, 1);                          // Release INT high -> Latches 0x5D address!\n"
        "vTaskDelay(pdMS_TO_TICKS(20));\n\n"
        "// 3. Configure esp_lcd_panel_io_i2c (16-bit register commands are MANDATORY):\n"
        "esp_lcd_panel_io_i2c_config_t io_cfg = {\n"
        "    .dev_addr = 0x5D, .control_phase_bytes = 1, .dc_bit_offset = 0,\n"
        "    .lcd_cmd_bits = 16,     // MUST be 16, NOT 8!\n"
        "    .lcd_param_bits = 16,   // MUST be 16, NOT 8!\n"
        "    .flags.disable_control_phase = 1,\n"
        "};\n\n"
        "// 4. Touch driver configuration (RST and INT set to NC because handled above):\n"
        "esp_lcd_touch_config_t tp_cfg = {\n"
        "    .x_max = 1024, .y_max = 600,\n"
        "    .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = GPIO_NUM_NC,\n"
        "    .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },\n"
        "};\n"
        "esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp);\n"
        "vTaskDelay(pdMS_TO_TICKS(1000));\n"
        "esp_lcd_touch_exit_sleep(tp); // Mandatory GT911 wake-up command"
    )
    t_code = Table([[Paragraph(f"<font face='Courier' size='6.5'>{code_touch.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_code.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 5),
    ]))
    story.append(t_code)

    story.append(PageBreak())

    # SECTION 4 & 5: CAMERA, PSRAM & MEMORY
    story.append(Paragraph("Section 4: MIPI-CSI Camera Driver & ISP Pipeline", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The camera sensor is an OmniVision SC2336 connected to the ESP32-P4 hardware ISP via 2-lane MIPI-CSI at the top of the display:",
        styles["Body"]
    ))

    cam_specs = [
        "<b>Sensor Mount:</b> Positioned along the top display bezel, pointing forward toward passing users.",
        "<b>SCCB Bus Address:</b> <code>0x36</code> (7-bit address; not 0x30).",
        "<b>Bayer Pixel Format:</b> Native sensor output is <b>BGGR</b> RAW8. Do NOT enable HMIRROR without switching ISP Bayer order to GBRG, or severe chromatic distortion will result.",
        "<b>Resolution & Framerate:</b> <code>MIPI_2lane_24Minput_RAW8_1280x720_30fps</code> (the only hardware-supported profile on this board revision).",
        "<b>Indoor Exposure Baseline:</b> <code>exposure_us = 15000</code>, <code>exposure_val = 1100</code>, <code>gain = 4000</code>.",
        "<b>Double-Buffering DMA Protection:</b> A dedicated buffer <code>s_detection_fb_buf</code> is allocated in PSRAM during boot. When the detection task is ready, the camera task copies the frame atomically, ensuring the face detector and landmark alignment never read a partially overwritten buffer.",
        "<b>Camera Profiles:</b> Firmware provides 5 runtime exposure presets selectable via UI buttons: Auto, Outdoor, Indoor, Bright, and Low Light.",
    ]
    for cs in cam_specs:
        story.append(Paragraph(f"• {cs}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 5: High-Speed PSRAM & Flash Memory Architecture", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The ESP32-P4 requires 32MB Hex-mode PSRAM running at 200 MHz to support simultaneous 720p MIPI video decoding, LVGL frame rendering, and neural network tensor allocations.",
        styles["Body"]
    ))

    sdk_symbols = (
        "# Mandatory symbols in sdkconfig / sdkconfig.defaults.esp32p4:\n"
        "CONFIG_IDF_EXPERIMENTAL_FEATURES=y          # Required to unlock 200MHz PSRAM\n"
        "CONFIG_SPIRAM=y\n"
        "CONFIG_SPIRAM_MODE_HEX=y                    # 16-line Hex-mode bus\n"
        "CONFIG_SPIRAM_SPEED_200M=y                  # 200 MHz clock rate\n"
        "CONFIG_SPIRAM_USE_MALLOC=y\n"
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y\n"
        "CONFIG_SPIRAM_RODATA=y\n"
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y           # 16 MB QSPI Flash\n"
        "CONFIG_I2C_SKIP_LEGACY_CONFLICT_CHECK=y    # Avoids driver/i2c legacy symbol clash"
    )
    t_sdk = Table([[Paragraph(f"<font face='Courier' size='7'>{sdk_symbols.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_sdk.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 5),
    ]))
    story.append(t_sdk)

    story.append(Spacer(1, 6))
    story.append(make_callout(
        "danger",
        "Stack Allocation Anti-Pattern",
        "Never allocate large frame arrays on the FreeRTOS task stack (e.g., <code>uint8_t crop[112*112*3]</code>). Doing so causes an immediate task stack overflow. All vision tensors and image crops must be dynamically allocated from PSRAM using <code>heap_caps_malloc(size, MALLOC_CAP_SPIRAM)</code> and freed upon task completion.",
        styles
    ))

    story.append(PageBreak())

    # SECTION 6 & 7: POWER, BATTERY, AND VISION ENGINE
    story.append(Paragraph("Section 6: Power Supply, Battery Telemetry & STC8 Gauge", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The power management system combines continuous USB-C DC input with single-cell LiPo battery backup:",
        styles["Body"]
    ))

    power_details = [
        "<b>Mechanical Power Switch:</b> Slide switch on left edge (UP connects battery/DC, DOWN disconnects power).",
        "<b>DC Input & Charging:</b> USB Type-C port on left edge below switch. Requires <b>5V, 2A to 3A</b> for proper simultaneous operation and cell charging.",
        "<b>ADC Sensing Network:</b> GPIO 4 (ADC1 Channel 4) measures battery terminal voltage through a 2:1 resistive divider (3.30V empty to 4.20V full).",
        "<b>STC8 Gas Gauge Protocol:</b> Onboard STC8 coprocessor samples voltage and communicates percentage to the ESP32-P4.",
        "<b>UI Color Display Implementation:</b><br/>"
        "• <b>Charging:</b> Displays <code>LV_SYMBOL_CHARGE</code> and percentage in <b>Cyan Blue</b> (<code>#00CFFF</code>).<br/>"
        "• <b>Normal (≥15%):</b> Displays icon and percentage in <b>Theme White/Black</b> (NO green color, NO bar meter).<br/>"
        "• <b>Low Battery (<15%):</b> Icon and percentage turn solid <b>Red</b> (<code>#FF4444</code>).<br/>"
        "• <b>5% Emergency Shutdown:</b> Calls <code>db_sync_to_disk()</code>, unmounts <code>/sdcard</code>, and safely halts to protect database integrity.",
        "<b>Calibration Logger:</b> When `BATTERY_CALIB_LOGGING=1` in `config.h`, records ADC mV and STC8 percentage to `/sdcard/logs/batt_calib.csv` every 20 seconds.",
    ]
    for pd in power_details:
        story.append(Paragraph(f"• {pd}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 7: Deep Learning Vision Engine & Face Embeddings", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The biometric engine uses Espressif ESP-DL quantized neural network models executed on the RISC-V SIMD architecture:",
        styles["Body"]
    ))

    ai_steps = [
        "<b>Model Locations on SD Card:</b><br/>"
        "• <code>/sdcard/models/p4/human_face_detect_msr_s8_v1.espdl</code> (Fast multi-scale face proposal)<br/>"
        "• <code>/sdcard/models/p4/human_face_detect_mnp_s8_v1.espdl</code> (5-point keypoint landmark detection)<br/>"
        "• <code>/sdcard/models/p4/mfn_s8_v1.espdl</code> (MobileFaceNet 128D int8 embedding extractor)",
        "<b>Keypoint Indexing:</b> ESP-DL outputs 5 landmarks in order: <code>[Left Eye, Left Mouth, Nose, Right Eye, Right Mouth]</code>. Landmarks are copied 1-to-1 without reordering to keep yaw angles accurate.",
        "<b>Affine Transform:</b> Warps cropped face keypoints to fixed coordinate anchors in a canonical 112×112 RGB565 matrix.",
        "<b>Feature Extraction:</b> MobileFaceNet outputs a 128-dimensional signed int8 vector <code>e ∈ [-128, 127]^128</code>.",
    ]
    for as_ in ai_steps:
        story.append(Paragraph(f"• {as_}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("Section 8: Multi-Frame Temporal Consensus & Margin Gating", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "To achieve near-zero false acceptance rates without sacrificing throughput:",
        styles["Body"]
    ))
    consensus_details = [
        "<b>Sliding Window Accumulator:</b> Tracks up to 6 valid frames over a 1500ms window.",
        "<b>Early Exit Fast Path:</b> If 3 consecutive frames show unanimous candidate matching with similarity S ≥ 0.80, verification completes immediately.",
        "<b>Dynamic Margin Gate:</b> Required separation margin over runner-up candidate #2 depends on Top-1 similarity:<br/>"
        "• <b>S ≥ 0.78:</b> Requires ΔS ≥ 0.03.<br/>"
        "• <b>0.74 ≤ S < 0.78:</b> Requires ΔS ≥ 0.05.<br/>"
        "• <b>0.70 ≤ S < 0.74:</b> Requires strict ΔS ≥ 0.08.",
        "<b>Visual-Only Feedback:</b> Results are displayed on screen via <code>ui_show_attendance_feedback()</code> (green banner `#003D1A` for success, orange `#3D2000` for duplicates, red text for unknown). No audio chime is emitted.",
        "<b>Quality Gates:</b> Specular glare filter (rejects frames with >15% pixels Y>240) and mouth elongation gate (rejects frames with ratio > 0.85).",
    ]
    for cd in consensus_details:
        story.append(Paragraph(f"• {cd}", styles["Bullet"]))

    story.append(PageBreak())

    # SECTION 9 & 10: FREERTOS CONCURRENCY & OTA FLASHING
    story.append(Paragraph("Section 9: FreeRTOS Task Architecture & Mutex Locking", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The firmware executes across two symmetric RISC-V cores. Strict task priority and synchronization rules prevent race conditions:",
        styles["Body"]
    ))

    tasks_table = [
        [Paragraph("<b>Task Name</b>", styles["TableHeader"]), Paragraph("<b>Priority</b>", styles["TableHeader"]), Paragraph("<b>Stack Size</b>", styles["TableHeader"]), Paragraph("<b>Core Affinity & Architectural Role</b>", styles["TableHeader"])],
        [Paragraph("LVGL UI Task", styles["TableCellBold"]), Paragraph("10 (Highest)", styles["TableCell"]), Paragraph("8,192 bytes", styles["TableCell"]), Paragraph("Core 1. Handles screen rendering and touch event dispatch.", styles["TableCell"])],
        [Paragraph("Detection Task", styles["TableCellBold"]), Paragraph("8", styles["TableCell"]), Paragraph("32,768 bytes", styles["TableCell"]), Paragraph("Core 0. Runs ESP-DL neural face detector and landmark alignment.", styles["TableCell"])],
        [Paragraph("Camera Task", styles["TableCellBold"]), Paragraph("7", styles["TableCell"]), Paragraph("8,192 bytes", styles["TableCell"]), Paragraph("Core 0. Receives MIPI DMA interrupts and manages frame buffers.", styles["TableCell"])],
        [Paragraph("Database Task", styles["TableCellBold"]), Paragraph("6", styles["TableCell"]), Paragraph("8,192 bytes", styles["TableCell"]), Paragraph("Core 1. Executes SQLite3 attendance writes and user queries.", styles["TableCell"])],
        [Paragraph("Network Task", styles["TableCellBold"]), Paragraph("5", styles["TableCell"]), Paragraph("16,384 bytes", styles["TableCell"]), Paragraph("Core 0. Manages Wi-Fi, HTTPS sync API, and Telegram bot calls.", styles["TableCell"])],
        [Paragraph("Battery Task", styles["TableCellBold"]), Paragraph("4", styles["TableCell"]), Paragraph("4,096 bytes", styles["TableCell"]), Paragraph("Core 1. Periodic 10s ADC voltage and STC8 telemetry polling.", styles["TableCell"])],
    ]
    t_tt = Table(tasks_table, colWidths=[90, 65, 80, 305])
    t_tt.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_tt)

    story.append(Spacer(1, 6))
    story.append(Paragraph(
        "<b>LVGL Thread Safety Rule:</b> Any non-display task invoking LVGL widget functions must acquire <code>ui_acquire()</code>. If <code>ui_acquire()</code> returns false (timeout), the caller MUST skip the UI call and bypass <code>ui_release()</code> to avoid corrupting the FreeRTOS recursive semaphore.",
        styles["Body"]
    ))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 10: Partition Table & Dual-Bank OTA Flashing", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The 16 MB flash memory is partitioned for safe dual-bank A/B firmware upgrades:",
        styles["Body"]
    ))

    part_csv = (
        "# Name,     Type, SubType, Offset,   Size,     Flags\n"
        "nvs,        data, nvs,     0x9000,   0x6000,   \n"
        "otadata,    data, ota,     0xf000,   0x2000,   \n"
        "phy_init,   data, phy,     0x11000,  0x1000,   \n"
        "ota_0,      app,  ota_0,   0x20000,  0x600000, # 6 MB Application Bank 0\n"
        "ota_1,      app,  ota_1,   0x620000, 0x600000, # 6 MB Application Bank 1\n"
        "storage,    data, spiffs,  0xc20000, 0x3d0000, "
    )
    t_part = Table([[Paragraph(f"<font face='Courier' size='7'>{part_csv.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_part.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 5),
    ]))
    story.append(t_part)

    story.append(Spacer(1, 6))
    story.append(Paragraph(
        "<b>Flashing Commands:</b><br/>"
        "• <b>Full Clean & Build:</b> <code>idf.py fullclean && idf.py build</code><br/>"
        "• <b>Flashing via COM port:</b> <code>esptool.py --chip esp32p4 -p COM5 -b 460800 --before default_reset --after hard_reset write_flash 0x0 build/bootloader/bootloader.bin 0x10000 build/partition_table/partition-table.bin 0x20000 build/SmartAttendance.bin</code>",
        styles["Body"]
    ))

    story.append(PageBreak())

    # SECTION 11 & 12: DATABASE SCHEMA & HARDWARE DIAGNOSTICS
    story.append(Paragraph("Section 11: SQLite3 Schema & On-Disk Data Structures", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Biometric profiles and attendance events are stored in <code>/sdcard/attendance.db</code> on the bottom MicroSD slot. <font color='#EF4444'><b>MANDATORY: DO NOT REMOVE THE CARD!</b></font>",
        styles["Body"]
    ))

    sql_schema = (
        "-- Users Table (Biometric Templates & Biographical Data)\n"
        "CREATE TABLE IF NOT EXISTS users (\n"
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    uuid TEXT UNIQUE NOT NULL,\n"
        "    name TEXT NOT NULL,\n"
        "    student_id TEXT UNIQUE,\n"
        "    phone_number TEXT,\n"
        "    telegram_id TEXT,\n"
        "    role TEXT NOT NULL CHECK(role IN ('student','lecturer','admin')),\n"
        "    face_embedding BLOB,               -- 128-byte int8 MobileFaceNet vector\n"
        "    created_at INTEGER NOT NULL,\n"
        "    updated_at INTEGER,\n"
        "    enroll_quality INTEGER DEFAULT 0,  -- Quality score (0 - 100)\n"
        "    enroll_accepted INTEGER DEFAULT 0,\n"
        "    enroll_rejected INTEGER DEFAULT 0,\n"
        "    model_version INTEGER DEFAULT 1,\n"
        "    embedding_dim INTEGER DEFAULT 128\n"
        ");\n\n"
        "-- Attendance Log Table\n"
        "CREATE TABLE IF NOT EXISTS attendance (\n"
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    user_id INTEGER NOT NULL,\n"
        "    session_id INTEGER,\n"
        "    course_code TEXT,\n"
        "    timestamp INTEGER NOT NULL,\n"
        "    confidence REAL NOT NULL,\n"
        "    synced INTEGER DEFAULT 0,          -- 0=local only, 1=synced to cloud\n"
        "    FOREIGN KEY (user_id) REFERENCES users(id)\n"
        ");"
    )
    t_sql = Table([[Paragraph(f"<font face='Courier' size='6.5'>{sql_schema.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_sql.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 5),
    ]))
    story.append(t_sql)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 12: Board-Level Diagnostics & Hardware Repair", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Technicians diagnosing hardware failures should follow this empirical troubleshooting matrix:",
        styles["Body"]
    ))

    diag_matrix = [
        [Paragraph("<b>Hardware Symptom</b>", styles["TableHeader"]), Paragraph("<b>Diagnostic Test Point</b>", styles["TableHeader"]), Paragraph("<b>Root Cause & Repair Procedure</b>", styles["TableHeader"])],
        [
            Paragraph("Board fails to enumerate on USB", styles["TableCellBold"]),
            Paragraph("Check left-side slide switch (must be UP); measure 5V rail at USB-C input.", styles["TableCell"]),
            Paragraph("Slide switch up to connect power. Ensure USB-C power supply provides at least 5V 2A. Check for shorts on 3.3V rail.", styles["TableCell"]),
        ],
        [
            Paragraph("Display blank; backlight off", styles["TableCellBold"]),
            Paragraph("Probe GPIO31 with oscilloscope; check LDO3 (2.5V) and LDO4 (3.3V).", styles["TableCell"]),
            Paragraph("Verify 30 kHz PWM carrier on GPIO31. If PWM is present but backlight is off, check FPC backlight driver boost circuit.", styles["TableCell"]),
        ],
        [
            Paragraph("Touch I2C arbitration failure", styles["TableCellBold"]),
            Paragraph("Probe GPIO45 (SDA) and GPIO46 (SCL) with logic analyzer.", styles["TableCell"]),
            Paragraph("Verify external 4.7kΩ pull-up resistors to 3.3V. Inspect GPIO40 (RST) and GPIO42 (INT) for broken solder joints on 6-pin touch FPC.", styles["TableCell"]),
        ],
        [
            Paragraph("Camera probe returns SCCB error", styles["TableCellBold"]),
            Paragraph("Verify 24-pin FPC latch connection at top edge of screen; probe 3.3V analog rail.", styles["TableCell"]),
            Paragraph("Ensure gold contacts face towards PCB. Confirm sensor address is 0x36. Replace SC2336 camera module if SCCB fails to respond.", styles["TableCell"]),
        ],
        [
            Paragraph("MicroSD read timeout / missing DB", styles["TableCellBold"]),
            Paragraph("Check bottom MicroSD slot; verify GPIO43 (CLK), GPIO44 (CMD), GPIO39 (D0).", styles["TableCell"]),
            Paragraph("Ensure card is firmly inserted in bottom slot (DO NOT REMOVE during operation). Confirm FAT32 format with <code>models/p4/</code> present.", styles["TableCell"]),
        ],
    ]
    t_diag = Table(diag_matrix, colWidths=[120, 140, 280])
    t_diag.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_diag)

    story.append(Spacer(1, 10))
    story.append(HRFlowable(width="100%", thickness=1, color=C_SECONDARY, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<font size='8' color='#64748B'>SmartAttendance™ Engineering Specification. Document ID: SA-ESP32P4-ENG-2026-REV6. Copyright © 2026 SmartAttendance Project Team. All rights reserved.</font>",
        styles["Body"]
    ))

    return story


def generate_technician_pdf(output_path):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    doc = SimpleDocTemplate(
        output_path,
        pagesize=letter,
        leftMargin=MARGIN, rightMargin=MARGIN,
        topMargin=MARGIN, bottomMargin=MARGIN,
    )
    story = build_technician_story()
    print(f"Building Technician Manual PDF (Times New Roman 12pt) at: {output_path} ...")
    doc.build(story, canvasmaker=TechNumberedCanvas)
    print("Technician Manual PDF build completed successfully.")


if __name__ == "__main__":
    out_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "SmartAttendance_Technician_Manual.pdf"
    )
    generate_technician_pdf(out_file)
