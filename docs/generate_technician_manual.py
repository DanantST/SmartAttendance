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
- GT911 touch controller manual reset/interrupt sequence and I2C bus lockup recovery
- Deep learning inference pipeline (ESP-DL, MobileFaceNet int8, 128D embeddings)
- Temporal consensus voting, double-buffering DMA architecture, tiered margin gating
- Power architecture, battery fuel gauge (STC8), ADC calibration formulas, color states (cyan charging, red <15%, white normal; no audio/speaker)
- FreeRTOS task prioritization, memory management (SPIRAM caps), and mutex safety
- Firmware flashing, partition table layout, dual-bank OTA updates with rollback protection
- In-depth Board-Level Diagnostics, Multimeter Test Points, Component Failure Modes & Repair Guide
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
        self.setFont(F_NORMAL, 8.5)
        self.setFillColor(C_MUTED)

        # Header
        header_text = "SmartAttendance™ ESP32-P4 Technical Specification & Repair Manual"
        self.drawString(MARGIN, PAGE_HEIGHT - 25, header_text)
        self.setStrokeColor(C_BORDER)
        self.setLineWidth(0.5)
        self.line(MARGIN, PAGE_HEIGHT - 28, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - 28)

        # Footer
        footer_left = "Hardware Architecture, Register Timing, OTA Rollback & Board Repair"
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
            fontSize=24, leading=30, textColor=C_PRIMARY, spaceAfter=8,
        ),
        "CoverSubtitle": ParagraphStyle(
            "CoverSubtitle", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=13, leading=18, textColor=C_DARK, spaceAfter=14,
        ),
        "CoverMeta": ParagraphStyle(
            "CoverMeta", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=9.5, leading=13.5, textColor=C_BODY,
        ),
        "CoverMetaBold": ParagraphStyle(
            "CoverMetaBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=9.5, leading=13.5, textColor=C_PRIMARY,
        ),
        "H1": ParagraphStyle(
            "H1", parent=base["Heading1"], fontName=F_BOLD,
            fontSize=15.5, leading=20, textColor=C_PRIMARY, spaceBefore=10, spaceAfter=5, keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2", parent=base["Heading2"], fontName=F_BOLD,
            fontSize=12.5, leading=16.5, textColor=C_SECONDARY, spaceBefore=7, spaceAfter=3, keepWithNext=True,
        ),
        "H3": ParagraphStyle(
            "H3", parent=base["Heading3"], fontName=F_BOLD,
            fontSize=11, leading=14.5, textColor=C_DARK, spaceBefore=5, spaceAfter=2.5, keepWithNext=True,
        ),
        # Strict user requirement: Font size 12 for the body text in Times New Roman
        "Body": ParagraphStyle(
            "Body", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, spaceAfter=4.5,
        ),
        "Bullet": ParagraphStyle(
            "Bullet", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, leftIndent=14, firstLineIndent=-10, spaceAfter=3,
        ),
        "TableHeader": ParagraphStyle(
            "TableHeader", parent=base["Normal"], fontName=F_BOLD,
            fontSize=9, leading=12.5, textColor=colors.white,
        ),
        "TableCell": ParagraphStyle(
            "TableCell", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=8.5, leading=11.5, textColor=C_BODY,
        ),
        "TableCellBold": ParagraphStyle(
            "TableCellBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=8.5, leading=11.5, textColor=C_DARK,
        ),
        "CalloutBold": ParagraphStyle(
            "CalloutBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10.5, leading=14.5, textColor=C_PRIMARY,
        ),
        "CalloutText": ParagraphStyle(
            "CalloutText", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=10, leading=14, textColor=C_DARK,
        ),
        "TOCItem": ParagraphStyle(
            "TOCItem", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=9.5, leading=13.5, textColor=C_DARK,
        ),
        "TOCItemBold": ParagraphStyle(
            "TOCItemBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10, leading=14, textColor=C_PRIMARY,
        ),
    }


def make_callout(box_type, title, content, styles):
    type_map = {
        "info": (C_INFO_BORDER, C_INFO_BG, "[i] ENGINEERING NOTE:"),
        "tip": (C_SUCCESS_BORDER, C_SUCCESS_BG, "[v] BEST PRACTICE / TUNING:"),
        "warn": (C_WARN_BORDER, C_WARN_BG, "[!] HARDWARE WARNING:"),
        "danger": (C_DANGER_BORDER, C_DANGER_BG, "[X] CRITICAL CONSTRAINT:"),
    }
    border, bg, prefix = type_map.get(box_type.lower(), (C_INFO_BORDER, C_INFO_BG, "NOTE:"))
    cells = [Paragraph(f"<b>{prefix} {title}</b>", styles["CalloutBold"]), Spacer(1, 1.5)]
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
    story.append(Spacer(1, 10))
    story.append(Paragraph("TECHNICAL SPECIFICATION & HARDWARE REPAIR MANUAL", styles["CoverTag"]))
    story.append(Paragraph("SmartAttendance™ ESP32-P4 Biometric Terminal", styles["CoverTitle"]))
    story.append(Paragraph(
        "Board-Level Schematics, Timing Constraints, Dual-Bank OTA Rollback Architecture, "
        "FreeRTOS Task Concurrency, and Exhaustive Hardware Troubleshooting & Repair Guide",
        styles["CoverSubtitle"]
    ))

    story.append(HRFlowable(width="100%", thickness=2.5, color=C_SECONDARY, spaceBefore=2, spaceAfter=10))

    highlights = [
        "<b>SoC Core:</b> Espressif ESP32-P4 (Dual-Core RISC-V @ 400MHz, Hex-PSRAM @ 200MHz, AI Instructions)",
        "<b>Display Bridge:</b> 7.0″ 1024×600 MIPI-DSI (EK79007 bridge, 2-lane D-PHY @ 900Mbps, LVGL 9)",
        "<b>Vision Sensor:</b> OmniVision SC2336 2MP MIPI-CSI (24-pin FPC at top of screen, SCCB addr 0x36)",
        "<b>Touch Controller:</b> Goodix GT911 Capacitive Touch (Manual GPIO RST/INT sequence, 16-bit register IO)",
        "<b>Power & Charging:</b> Left slide switch (UP=ON), USB-C below switch (5V 2-3A), STC8 ADC telemetry",
        "<b>Fail-Safe Updates:</b> Dual-bank 6MB A/B flash partitions (`ota_0`/`ota_1`) with automated rollback",
        "<b>Storage Architecture:</b> SDMMC 1-bit FAT32 on bottom slot (`/sdcard`) with SQLite3 DB (DO NOT REMOVE!)",
        "<b>Board Repair:</b> Comprehensive PDN multimeter test points, component failure diagnosis & recovery",
    ]
    cov_p = [Paragraph(f"• {h}", styles["Body"]) for h in highlights]
    t_cov = Table([[cov_p]], colWidths=[USABLE_WIDTH])
    t_cov.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 1, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 8),
    ]))
    story.append(t_cov)
    story.append(Spacer(1, 14))

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
            Paragraph("SA-ESP32P4-ENG-2026-REV7", styles["CoverMeta"]),
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
    story.append(Spacer(1, 12))

    story.append(make_callout(
        "warn",
        "Confidential Engineering Document",
        "This manual contains low-level hardware schematics, register configurations, multimeter diagnostic test points, and component repair procedures intended exclusively for engineering and technical repair personnel. End-user operating guidelines are documented in the companion SmartAttendance User Manual.",
        styles
    ))

    story.append(PageBreak())

    # TABLE OF CONTENTS
    story.append(Paragraph("Engineering Table of Contents", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1.5, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    sections = [
        ("Section 1: Hardware Architecture & Electrical Pinout", "Detailed pin mapping, power rail sequencing (LDO3/4), bus interfaces, slide switch"),
        ("Section 2: MIPI-DSI Display & Backlight Subsystems", "EK79007 bridge parameters, LVGL 9 SPIRAM buffers, GPIO31 PWM"),
        ("Section 3: GT911 Capacitive Touch Controller Quirks", "Manual GPIO reset sequence, 16-bit register commands, wake-up sequence, bus lockup recovery"),
        ("Section 4: MIPI-CSI Camera Driver & ISP Pipeline", "SC2336 24-pin CSI at top of screen, SCCB 0x36, Bayer BGGR, exposure timing, DMA buffer"),
        ("Section 5: High-Speed PSRAM & Flash Memory Architecture", "Hex-mode PSRAM @ 200MHz, experimental symbols, heap allocation caps"),
        ("Section 6: Power Supply, Battery Telemetry & STC8 Gauge", "Left-side USB-C 5V 2-3A, ADC voltage sense, STC8 UART packets, color states (no audio)"),
        ("Section 7: Deep Learning Vision Engine & Face Embeddings", "ESP-DL inference, 5-point landmark warping, MobileFaceNet 128D int8 vectors"),
        ("Section 8: Multi-Frame Temporal Consensus & Margin Gating", "6-frame window, 3-frame early exit, dynamic margin delta, anti-glare math"),
        ("Section 9: FreeRTOS Task Architecture & Mutex Locking", "Task priorities, stack sizing, UI recursive mutex safety, deadlock avoidance"),
        ("Section 10: Partition Table & Dual-Bank OTA Flashing", "6MB app partitions, A/B state machine, rollback protection, web portal update (/update)"),
        ("Section 11: SQLite3 Schema & On-Disk Data Structures", "Bottom MicroSD card (`attendance.db`), index optimization, backup mirroring (DO NOT REMOVE!)"),
        ("Section 12: Board-Level Diagnostics & Hardware Repair Guide", "PDN multimeter test points, component failure modes, I2C lockup, flash recovery, repair steps"),
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
    t_ct = Table([[Paragraph(f"<font face='Courier' size='6.5'>{code_touch.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_ct.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 4),
    ]))
    story.append(t_ct)

    story.append(PageBreak())

    # SECTION 4 & 5: CAMERA ISP & PSRAM ARCHITECTURE
    story.append(Paragraph("Section 4: MIPI-CSI Camera Driver & ISP Pipeline", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The OmniVision SC2336 2MP image sensor is mounted at the top of the screen via a 24-pin FPC ribbon cable. Sensor parameters:",
        styles["Body"]
    ))

    cam_points = [
        "<b>I2C Addressing:</b> SCCB slave address is strictly <b><code>0x36</code></b> (NOT <code>0x30</code>).",
        "<b>Interface & Pixel Format:</b> 2-lane MIPI-CSI transmitting RAW8 Bayer BGGR at 24 MHz input clock.",
        "<b>ISP Pipeline:</b> The ESP32-P4 hardware ISP processes Bayer BGGR directly into RGB565 via DMA double-buffering.",
        "<b>Exposure Register Limits:</b> Total lines: <code>0x044C</code> (1100 lines). Maximum safe integration time: <code>reg 0x3E01 = 0x08</code>, <code>reg 0x3E02 = 0x90</code> (2192 lines max). Setting exposure higher causes sensor FIFO overflow and blank frames.",
        "<b>Camera Profiles:</b> Firmware provides 5 software presets adjusting digital gain and integration time: Auto, Outdoor (low gain/fast shutter), Indoor, Bright, and Low Light.",
    ]
    for cp in cam_points:
        story.append(Paragraph(f"• {cp}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 5: High-Speed PSRAM & Flash Memory Architecture", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The board integrates 16 MB or 32 MB Hex-mode PSRAM running at 200 MHz. Key configuration requirements in <code>sdkconfig</code>:",
        styles["Body"]
    ))

    psram_points = [
        "<b>Hex-Mode PSRAM @ 200MHz:</b> <code>CONFIG_SPIRAM_MODE_HEX=y</code>, <code>CONFIG_SPIRAM_SPEED_200M=y</code>.",
        "<b>Experimental Symbol Suppression:</b> Must define <code>#define ESP_IDF_VERSION_VAL(a,b,c)</code> guards to prevent compilation failure when linking ESP-DL math kernels.",
        "<b>Heap Allocation Capabilities:</b> Display buffers and camera frame buffers MUST use <code>MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA</code>. Internal SRAM (512 KB) is reserved strictly for FreeRTOS task stacks and critical ISR handlers.",
        "<b>Heap Sanity Checking:</b> Background task monitors free PSRAM every 30 seconds; logs warning if available PSRAM drops below 2.0 MB.",
    ]
    for pp in psram_points:
        story.append(Paragraph(f"• {pp}", styles["Bullet"]))

    story.append(PageBreak())

    # SECTION 6, 7 & 8: POWER, VISION & RECOGNITION PIPELINE
    story.append(Paragraph("Section 6: Power Supply, Battery Telemetry & STC8 Gauge", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The terminal incorporates a comprehensive power management and battery telemetry subsystem:",
        styles["Body"]
    ))

    pwr_points = [
        "<b>Slide Switch:</b> Physical mechanical switch on left upper side. Cuts battery and input power completely.",
        "<b>USB-C Power Input:</b> Located directly below slide switch on left edge. 5.0V, 2.0A–3.0A recommended.",
        "<b>STC8 Coprocessor:</b> An onboard STC8 microcontroller monitors battery current, voltage, and charger status via UART.",
        "<b>ADC Fallback Formula:</b> If STC8 is unresponsive, firmware reads GPIO4 ADC (1/2 divider): <code>V_batt = (raw_adc / 4095.0) * 3.3 * 2.0 * 1.05</code>.",
        "<b>Color States (No Audio / No Green):</b> Cyan Blue (<code>#00CFFF</code>) with lightning bolt when charging; White/Black normal (15%–100%); Red (<code>#FF4444</code>) when low (&lt;15%).",
        "<b>Emergency Cutoff (5%):</b> Firmware detects critical threshold at 5% (3.40V) and initiates an automated atomic SQLite transaction close, flushes FAT32 caches, and powers down to prevent flash corruption.",
    ]
    for pwp in pwr_points:
        story.append(Paragraph(f"• {pwp}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Section 7 & 8: Deep Learning Vision Engine & Temporal Consensus", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Face verification executes entirely on-device using a 2-stage neural inference pipeline accelerated by ESP32-P4 SIMD instructions:",
        styles["Body"]
    ))

    cv_details = [
        "<b>Stage 1 Detection:</b> MobileNet-SSD detects faces and extracts 5 facial landmarks (eyes, nose, mouth corners) in ~22 ms.",
        "<b>Affine Landmark Alignment:</b> Normalizes facial roll, pitch, and yaw to standard 112×112 canvas.",
        "<b>Stage 2 Embeddings:</b> MobileFaceNet int8 quantized model outputs a 128-dimensional feature vector in ~38 ms.",
        "<b>Cosine Similarity Metric:</b> <code>cos_sim = dot(u, v) / (||u|| * ||v||)</code>. Acceptance threshold: <b>0.68</b> (standard) / <b>0.72</b> (strict).",
        "<b>Multi-Frame Temporal Consensus:</b> Rather than triggering attendance on a single frame, the system evaluates a sliding window of 6 consecutive frames. Verification succeeds if <b>at least 4 out of 6 frames</b> match the same identity with confidence &gt; 0.68. A 3-frame early exit fires if confidence exceeds 0.85.",
        "<b>Anti-Spoofing & Glare Filter:</b> Evaluates specular reflection histogram; rejects frames where saturated highlights cover &gt; 18% of facial landmarks.",
    ]
    for cd in cv_details:
        story.append(Paragraph(f"• {cd}", styles["Bullet"]))

    story.append(PageBreak())

    # SECTION 9 & 10: FREERTOS CONCURRENCY & DUAL-BANK OTA FLASHING
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
    story.append(Paragraph("Section 10: Partition Table & Dual-Bank OTA Flashing Architecture", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "The 16 MB flash memory is partitioned for safe dual-bank A/B firmware upgrades with automated hardware rollback:",
        styles["Body"]
    ))

    part_csv = (
        "# Name,     Type, SubType, Offset,   Size,     Flags\n"
        "nvs,        data, nvs,     0x9000,   0x6000,   # Non-volatile storage (PIN, Wi-Fi)\n"
        "otadata,    data, ota,     0xf000,   0x2000,   # Dual 4KB sectors tracking active bank\n"
        "phy_init,   data, phy,     0x11000,  0x1000,   # RF calibration parameters\n"
        "ota_0,      app,  ota_0,   0x20000,  0x600000, # 6 MB Application Bank 0\n"
        "ota_1,      app,  ota_1,   0x620000, 0x600000, # 6 MB Application Bank 1\n"
        "storage,    data, spiffs,  0xc20000, 0x3d0000, # 3.8 MB SPIFFS assets & fallback HTML"
    )
    t_part = Table([[Paragraph(f"<font face='Courier' size='6.5'>{part_csv.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_part.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 4),
    ]))
    story.append(t_part)

    story.append(Spacer(1, 4))
    story.append(Paragraph("Dual-Bank A/B State Machine & Rollback Protection", styles["H2"]))
    story.append(Paragraph(
        "When an update is initiated via Web Portal (<code>/update</code>) or Telegram (<code>/ota</code>):<br/>"
        "1. <b>Target Partition Selection:</b> <code>esp_ota_get_next_update_partition(NULL)</code> identifies the inactive bank.<br/>"
        "2. <b>Streaming Flash Write:</b> Incoming firmware chunks are written via <code>esp_ota_write()</code> and validated with CRC32.<br/>"
        "3. <b>Pending Verification State:</b> Upon restart, the bootloader flags the new partition as <code>ESP_OTA_IMG_PENDING_VERIFY</code>.<br/>"
        "4. <b>Automated Self-Test & Validation:</b> The application initializes peripherals (PSRAM, SC2336 camera, SDMMC, GT911 touch). If all subsystems initialize successfully, <code>main.c</code> executes: <code>esp_ota_mark_app_valid_cancel_rollback()</code>.<br/>"
        "5. <b>Automated Rollback Trigger:</b> If a watchdog timeout (WDT), panic, or fatal exception occurs before this call, the bootloader automatically marks the new bank as <code>ESP_OTA_IMG_INVALID</code> and reboots into the previous working bank, ensuring zero device bricking.",
        styles["Body"]
    ))

    story.append(Spacer(1, 4))
    story.append(Paragraph(
        "<b>Flashing Commands:</b><br/>"
        "• <b>Full Clean & Build:</b> <code>idf.py fullclean && idf.py build</code><br/>"
        "• <b>Direct Flashing via COM port:</b> <code>esptool.py --chip esp32p4 -p COM5 -b 460800 --before default_reset --after hard_reset write_flash 0x0 build/bootloader/bootloader.bin 0x10000 build/partition_table/partition-table.bin 0x20000 build/SmartAttendance.bin</code>",
        styles["Body"]
    ))

    story.append(PageBreak())

    # SECTION 11: DATABASE SCHEMA
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
        ("PADDING", (0, 0), (-1, -1), 4),
    ]))
    story.append(t_sql)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Security Credentials & Enrollment Security", styles["H2"]))
    story.append(Paragraph(
        "• <b>Master Admin PIN:</b> Stored in NVS under key <code>\"admin_pin\"</code>. Variable length: <b>4 to 8 numeric digits</b> (default initial PIN: <code>1234</code>). Protects launcher apps, reports, settings, and manual enrollment authorization.<br/>"
        "• <b>Dynamic Enrollment Session PIN:</b> Generated randomly in <code>ble_registration.c</code> via <code>esp_random() % 1000000</code>. Emitted as a <b>6-digit dynamic PIN</b> (format: <code>%06d</code>). Displayed in the screen banner and required for remote browser queueing over the <code>Attendance_Setup</code> Wi-Fi portal (which supports automatic captive portal detection).",
        styles["Body"]
    ))

    story.append(PageBreak())

    # SECTION 12: BOARD-LEVEL DIAGNOSTICS & REPAIR GUIDE
    story.append(Paragraph("Section 12: Board-Level Diagnostics & Hardware Repair Guide", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Technicians diagnosing hardware failures, electrical anomalies, or communication breakdowns must adhere to this empirical repair protocol:",
        styles["Body"]
    ))

    story.append(Paragraph("12.1 Power Distribution Network (PDN) Test Points & Voltage Rails", styles["H2"]))
    story.append(Paragraph(
        "Use a calibrated digital multimeter (DMM) referenced to system ground (GND on J7 header) to verify power rails:",
        styles["Body"]
    ))

    rails_table = [
        [Paragraph("<b>Power Rail / Net</b>", styles["TableHeader"]), Paragraph("<b>Nominal Voltage</b>", styles["TableHeader"]), Paragraph("<b>Acceptable Tolerance</b>", styles["TableHeader"]), Paragraph("<b>Diagnostic Test Point / Pin</b>", styles["TableHeader"]), Paragraph("<b>Failure Symptom if Out of Spec</b>", styles["TableHeader"])],
        [
            Paragraph("VBUS_IN", styles["TableCellBold"]),
            Paragraph("5.00 V", styles["TableCell"]),
            Paragraph("4.75 V – 5.25 V", styles["TableCell"]),
            Paragraph("USB-C VBUS Pin / Input TVS Diode", styles["TableCell"]),
            Paragraph("If &lt;4.6V, battery fails to charge; brownout resets during Wi-Fi transmit bursts.", styles["TableCell"]),
        ],
        [
            Paragraph("VBAT", styles["TableCellBold"]),
            Paragraph("3.70 V (Nominal)", styles["TableCell"]),
            Paragraph("3.00 V – 4.20 V", styles["TableCell"]),
            Paragraph("Battery Terminal JST Header / Pin 1", styles["TableCell"]),
            Paragraph("If &lt;3.0V, battery protection board trips; unit will not boot without USB-C connected.", styles["TableCell"]),
        ],
        [
            Paragraph("VDD_3V3_SYS", styles["TableCellBold"]),
            Paragraph("3.30 V", styles["TableCell"]),
            Paragraph("3.20 V – 3.35 V", styles["TableCell"]),
            Paragraph("Main DC-DC Buck Inductor / J7 Pin 2", styles["TableCell"]),
            Paragraph("If &lt;3.1V, ESP32-P4 SoC panics, QSPI flash bit flips, or SDMMC read timeouts occur.", styles["TableCell"]),
        ],
        [
            Paragraph("LDO3_2V5 (PMU)", styles["TableCellBold"]),
            Paragraph("2.50 V", styles["TableCell"]),
            Paragraph("2.45 V – 2.55 V", styles["TableCell"]),
            Paragraph("PMU LDO3 Output Capacitor", styles["TableCell"]),
            Paragraph("Powers MIPI D-PHY. If disabled or &lt;2.4V, D-PHY clock fails to lock; display remains black.", styles["TableCell"]),
        ],
        [
            Paragraph("LDO4_3V3 (PMU)", styles["TableCellBold"]),
            Paragraph("3.30 V", styles["TableCell"]),
            Paragraph("3.25 V – 3.35 V", styles["TableCell"]),
            Paragraph("PMU LDO4 Output Capacitor", styles["TableCell"]),
            Paragraph("Powers EK79007 digital IO bridge. If disabled, panel displays no picture.", styles["TableCell"]),
        ],
        [
            Paragraph("VBACKUP (RTC)", styles["TableCellBold"]),
            Paragraph("3.00 V", styles["TableCell"]),
            Paragraph("2.80 V – 3.30 V", styles["TableCell"]),
            Paragraph("DS1302 Coin Cell Holder / VCC Pin", styles["TableCell"]),
            Paragraph("If 0V, real-time clock loses time when power switch is DOWN; resets to 1970 on boot.", styles["TableCell"]),
        ],
    ]
    t_rails = Table(rails_table, colWidths=[85, 70, 75, 125, 185])
    t_rails.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 2.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_rails)

    story.append(Spacer(1, 6))
    story.append(Paragraph("12.2 Component Failure Modes, Diagnostics & Field Repair Procedures", styles["H2"]))

    trouble_cases = [
        ("Case 1: MIPI-DSI Display Blank, White Screen, or Rolling Distortion", [
            "<b>Root Causes:</b> 30-pin 0.5mm pitch FPC ribbon mis-seated; oxidation on gold contact fingers; LDO3 (2.5V) or LDO4 (3.3V) uninitialized; backlight boost circuit fault.",
            "<b>Diagnostic Procedure:</b> Connect oscilloscope to GPIO31 to verify 30 kHz PWM carrier. Check serial log for <code>[esp_lcd_mipi_dsi]</code> init errors. Measure LDO3 (2.5V) and LDO4 (3.3V).",
            "<b>Repair Steps:</b><br/>"
            "1. Slide power switch DOWN and disconnect USB-C.<br/>"
            "2. Open 30-pin FPC flip-lock latch carefully using a plastic pry tool.<br/>"
            "3. Inspect ribbon gold fingers under 20x loupe for solder bridging, scratches, or oxidation. Clean contacts with 99.9% isopropyl alcohol (IPA) and lint-free swab.<br/>"
            "4. Reseat ribbon perfectly straight and press the lock down firmly until it clicks flush.<br/>"
            "5. If screen displays white with no pixels, replace the EK79007 bridge module.",
        ]),
        ("Case 2: GT911 Capacitive Touch Controller I2C Arbitration Deadlock", [
            "<b>Root Causes:</b> Missing 4.7kΩ pull-up resistors on GPIO45/46; cracked solder joints on 6-pin touch FPC header; I2C slave holding SDA low.",
            "<b>Diagnostic Procedure:</b> Probe GPIO45 (SDA) and GPIO46 (SCL) with logic analyzer. Idle level must be 3.3V. Inspect GPIO40 (RST) and GPIO42 (INT) timing on boot.",
            "<b>Repair Steps:</b><br/>"
            "1. Verify that address latching sequence holds GPIO42 LOW while releasing GPIO40 HIGH.<br/>"
            "2. <b>I2C Bus Deadlock Recovery:</b> If GT911 holds SDA low, toggle GPIO46 (SCL) 9 times manually to force the slave state machine to release the bus.<br/>"
            "3. Re-flow solder on 6-pin 0.5mm touch header if cold solder joints are detected.",
        ]),
        ("Case 3: SC2336 Camera Subsystem Probe Failure (SCCB Addr 0x36)", [
            "<b>Root Causes:</b> 24-pin CSI ribbon inverted or unseated; camera lens out of focus; sensor address mismatch (must be 0x36, NOT 0x30).",
            "<b>Diagnostic Procedure:</b> Inspect serial log for <code>esp_cam_sensor_detect: SC2336 not found</code>. Probe 3.3V supply on camera FPC connector pins 1-2.",
            "<b>Repair Steps:</b><br/>"
            "1. Re-latch the 24-pin ribbon at the top of the screen ensuring contacts face towards the PCB.<br/>"
            "2. Confirm firmware specifies <code>.sccb_addr = 0x36</code>.<br/>"
            "3. <b>Lens Focus Tuning:</b> If captured face images are blurry or yield low quality scores (&lt;55), break the adhesive dot on the M12 lens thread and gently rotate clockwise/counter-clockwise until sharp focus is achieved at 60cm–90cm (2–3 feet) working distance.",
        ]),
        ("Case 4: MicroSD Storage & SQLite Database Corrupt / Read-Only", [
            "<b>Root Causes:</b> MicroSD card removed during active write; slot contact fatigue; dirty pads; FAT32 journal corruption.",
            "<b>Diagnostic Procedure:</b> Check serial console for <code>sdmmc_req: sdmmc_send_cmd returned 0x107</code>.",
            "<b>Repair Steps:</b><br/>"
            "1. <b>Mandatory Rule:</b> MicroSD card MUST NOT be removed during operation.<br/>"
            "2. Inspect the bottom MicroSD socket pins. Clean card gold pads with IPA.<br/>"
            "3. If FAT32 filesystem is damaged, mount card on a PC and run <code>fsck.vfat -a /dev/sdX1</code>.<br/>"
            "4. To repair SQLite database, run: <code>sqlite3 attendance.db \"PRAGMA integrity_check;\"</code>. If corrupted, restore from daily backup snapshot: <code>cp attendance.db.bak attendance.db</code>.<br/>"
            "5. Verify that <code>/sdcard/models/p4/</code> contains all required MobileFaceNet neural weights.",
        ]),
        ("Case 5: ESP32-P4 SoC Boot Loop, Brownout, or ROM Download Mode Lock", [
            "<b>Root Causes:</b> Corrupted partition table or interrupted flash; strapping pin GPIO35 floating; brownout on 3.3V rail.",
            "<b>Diagnostic Procedure:</b> Connect USB-C to PC, open serial terminal at 115200 baud. Check ROM boot code.",
            "<b>Repair Steps:</b><br/>"
            "1. Force UART download mode: Pull GPIO35 LOW while toggling EN pin.<br/>"
            "2. Erase entire flash: <code>esptool.py --chip esp32p4 -p COMx erase_flash</code>.<br/>"
            "3. Reflash complete partition suite: bootloader at 0x0, partition-table at 0x10000, and firmware at 0x20000.<br/>"
            "4. <b>OTA Boot Loop Recovery:</b> If the device is stuck in an OTA rollback loop, clear the <code>otadata</code> partition to restore factory boot default: <code>esptool.py --chip esp32p4 -p COMx erase_region 0xf000 0x2000</code>.",
        ]),
    ]

    for title, steps in trouble_cases:
        story.append(Paragraph(f"<b>{title}</b>", styles["H3"]))
        for s in steps:
            story.append(Paragraph(f"• {s}", styles["Bullet"]))
        story.append(Spacer(1, 2))

    story.append(Spacer(1, 4))
    story.append(Paragraph("12.3 Diagnostic J7 Header Pinout & Test Points", styles["H2"]))
    story.append(Paragraph(
        "The board exposes an unpopulated 10-pin header (J7) for logic analyzer probing and factory test automation:",
        styles["Body"]
    ))

    j7_matrix = [
        [Paragraph("<b>J7 Pin</b>", styles["TableHeader"]), Paragraph("<b>Signal Name</b>", styles["TableHeader"]), Paragraph("<b>Type</b>", styles["TableHeader"]), Paragraph("<b>Description & Diagnostic Use Case</b>", styles["TableHeader"])],
        [Paragraph("Pin 1", styles["TableCellBold"]), Paragraph("GND", styles["TableCell"]), Paragraph("Power", styles["TableCell"]), Paragraph("System ground reference for oscilloscope and DMM probes.", styles["TableCell"])],
        [Paragraph("Pin 2", styles["TableCellBold"]), Paragraph("VDD_3V3", styles["TableCell"]), Paragraph("Power", styles["TableCell"]), Paragraph("Main 3.3V digital supply rail test point.", styles["TableCell"])],
        [Paragraph("Pin 3", styles["TableCellBold"]), Paragraph("GPIO 49", styles["TableCell"]), Paragraph("IO / RTC_CE", styles["TableCell"]), Paragraph("DS1302 Real-Time Clock Chip Enable line.", styles["TableCell"])],
        [Paragraph("Pin 4", styles["TableCellBold"]), Paragraph("GPIO 50", styles["TableCell"]), Paragraph("IO / RTC_DAT", styles["TableCell"]), Paragraph("DS1302 Real-Time Clock Bidirectional Data line.", styles["TableCell"])],
        [Paragraph("Pin 5", styles["TableCellBold"]), Paragraph("GPIO 51", styles["TableCell"]), Paragraph("IO / RTC_CLK", styles["TableCell"]), Paragraph("DS1302 Real-Time Clock Serial Clock line.", styles["TableCell"])],
        [Paragraph("Pin 6", styles["TableCellBold"]), Paragraph("GPIO 35", styles["TableCell"]), Paragraph("Boot Strapping", styles["TableCell"]), Paragraph("Pull LOW during reset to force ESP32-P4 ROM UART Download mode.", styles["TableCell"])],
        [Paragraph("Pin 7", styles["TableCellBold"]), Paragraph("TXD0 / IO37", styles["TableCell"]), Paragraph("UART Out", styles["TableCell"]), Paragraph("Primary debug UART transmit (115200 baud, 8N1).", styles["TableCell"])],
        [Paragraph("Pin 8", styles["TableCellBold"]), Paragraph("RXD0 / IO38", styles["TableCell"]), Paragraph("UART In", styles["TableCell"]), Paragraph("Primary debug UART receive for factory test automation.", styles["TableCell"])],
    ]
    t_j7 = Table(j7_matrix, colWidths=[65, 85, 65, 325])
    t_j7.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 2.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_j7)

    story.append(Spacer(1, 8))
    story.append(HRFlowable(width="100%", thickness=1, color=C_SECONDARY, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<font size='8' color='#64748B'>SmartAttendance™ Engineering Specification. Document ID: SA-ESP32P4-ENG-2026-REV7. Copyright © 2026 SmartAttendance Project Team. All rights reserved.</font>",
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
