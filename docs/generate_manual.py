"""
Comprehensive SmartAttendance System User & Administration Manual Generator.
Generates an exhaustive, publication-grade PDF manual covering:
- Hardware specifications and interfaces
- Power management, battery specifications, and charging procedures
- Commissioning, SD card preparation, and Setup Wizard
- Biometric Face Recognition engine and multi-frame consensus
- 8-Step statistically robust facial enrollment (On-device and Web AP Portal)
- Lecture scheduling & course session management via Telegram Bot and Cloud API
- Daily attendance scanning operations, verification feedback, and debounce logic
- User database, roster management, and non-destructive re-enrollment
- Attendance reports, audit logs, and MicroSD CSV export
- Dual Telegram bot ecosystem (Student alerts, Lecturer portal, Admin commands)
- System settings, maintenance, and Over-The-Air (OTA) firmware updates
- Comprehensive troubleshooting, hardware diagnostics, and error code glossary
- Technical specifications, safety standards, and physical installation
"""

import os
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.pdfgen import canvas
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_RIGHT, TA_JUSTIFY

# High-contrast, publication-grade corporate color palette
C_PRIMARY = colors.HexColor("#1A365D")    # Deep Oxford Navy
C_SECONDARY = colors.HexColor("#0D9488")  # High-tech Teal
C_DARK = colors.HexColor("#1E293B")       # Dark Charcoal
C_BODY = colors.HexColor("#334155")       # Slate Gray
C_MUTED = colors.HexColor("#64748B")      # Muted Gray
C_BG_LIGHT = colors.HexColor("#F8FAFC")   # Light Slate Background
C_BORDER = colors.HexColor("#CBD5E1")     # Soft Border
C_CARD_BG = colors.HexColor("#F1F5F9")    # Card / Code Background

# Semantic Callout Palette
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


class NumberedCanvas(canvas.Canvas):
    """
    Two-pass canvas recording page states to print dynamic headers,
    footers, and accurate 'Page X of Y' page counters.
    """
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
        self.setFont("Helvetica", 8)
        self.setFillColor(C_MUTED)

        # Running Header
        header_text = "SmartAttendance™ Edge Biometric Terminal — Comprehensive System Manual"
        self.drawString(MARGIN, PAGE_HEIGHT - 25, header_text)
        self.setStrokeColor(C_BORDER)
        self.setLineWidth(0.5)
        self.line(MARGIN, PAGE_HEIGHT - 28, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - 28)

        # Running Footer
        footer_left = "Hardware: Elecrow CrowPanel 7″ ESP32-P4 v1.0 | Firmware v1.1.4+ | Telegram Sync"
        footer_right = f"Page {self._pageNumber} of {total_pages}"
        self.drawString(MARGIN, 20, footer_left)
        self.drawRightString(PAGE_WIDTH - MARGIN, 20, footer_right)
        self.line(MARGIN, 28, PAGE_WIDTH - MARGIN, 28)

        self.restoreState()


def get_styles():
    """Build and customize typographic hierarchy with explicit proportional leadings."""
    base = getSampleStyleSheet()

    styles = {
        "CoverTag": ParagraphStyle(
            "CoverTag",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=10,
            leading=14,
            textColor=C_SECONDARY,
            alignment=TA_LEFT,
            spaceAfter=6,
        ),
        "CoverTitle": ParagraphStyle(
            "CoverTitle",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=26,
            leading=32,
            textColor=C_PRIMARY,
            alignment=TA_LEFT,
            spaceAfter=8,
        ),
        "CoverSubtitle": ParagraphStyle(
            "CoverSubtitle",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=13,
            leading=18,
            textColor=C_DARK,
            alignment=TA_LEFT,
            spaceAfter=14,
        ),
        "CoverMeta": ParagraphStyle(
            "CoverMeta",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8.5,
            leading=13,
            textColor=C_BODY,
        ),
        "CoverMetaBold": ParagraphStyle(
            "CoverMetaBold",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=8.5,
            leading=13,
            textColor=C_PRIMARY,
        ),
        "H1": ParagraphStyle(
            "H1",
            parent=base["Heading1"],
            fontName="Helvetica-Bold",
            fontSize=16,
            leading=20,
            textColor=C_PRIMARY,
            spaceBefore=12,
            spaceAfter=6,
            keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2",
            parent=base["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=12,
            leading=16,
            textColor=C_SECONDARY,
            spaceBefore=9,
            spaceAfter=5,
            keepWithNext=True,
        ),
        "H3": ParagraphStyle(
            "H3",
            parent=base["Heading3"],
            fontName="Helvetica-Bold",
            fontSize=10,
            leading=13.5,
            textColor=C_DARK,
            spaceBefore=7,
            spaceAfter=3,
            keepWithNext=True,
        ),
        "Body": ParagraphStyle(
            "Body",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=9,
            leading=13,
            textColor=C_BODY,
            spaceAfter=5,
            alignment=TA_LEFT,
        ),
        "BodyBold": ParagraphStyle(
            "BodyBold",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=9,
            leading=13,
            textColor=C_DARK,
            spaceAfter=5,
        ),
        "Bullet": ParagraphStyle(
            "Bullet",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8.5,
            leading=12.5,
            textColor=C_BODY,
            leftIndent=12,
            firstLineIndent=-9,
            spaceAfter=3,
        ),
        "CalloutText": ParagraphStyle(
            "CalloutText",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8.5,
            leading=12,
            textColor=C_DARK,
        ),
        "CalloutBold": ParagraphStyle(
            "CalloutBold",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=8.5,
            leading=12,
            textColor=C_PRIMARY,
        ),
        "CodeText": ParagraphStyle(
            "CodeText",
            parent=base["Normal"],
            fontName="Courier",
            fontSize=7.5,
            leading=10.5,
            textColor=C_DARK,
        ),
        "TableHeader": ParagraphStyle(
            "TableHeader",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=8,
            leading=11,
            textColor=colors.white,
            alignment=TA_LEFT,
        ),
        "TableCell": ParagraphStyle(
            "TableCell",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8,
            leading=11,
            textColor=C_BODY,
            alignment=TA_LEFT,
        ),
        "TableCellBold": ParagraphStyle(
            "TableCellBold",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=8,
            leading=11,
            textColor=C_DARK,
            alignment=TA_LEFT,
        ),
        "TOCItem": ParagraphStyle(
            "TOCItem",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8.5,
            leading=13,
            textColor=C_DARK,
        ),
        "TOCItemBold": ParagraphStyle(
            "TOCItemBold",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=9,
            leading=13,
            textColor=C_PRIMARY,
        ),
    }
    return styles


def make_callout(box_type, title, content_paragraphs, styles):
    """
    Renders a styled single-cell callout box with a colored left accent bar.
    box_type: 'info', 'tip', 'warn', 'danger'
    """
    type_map = {
        "info": (C_INFO_BORDER, C_INFO_BG, "[i] NOTE:"),
        "tip": (C_SUCCESS_BORDER, C_SUCCESS_BG, "[v] BEST PRACTICE:"),
        "warn": (C_WARN_BORDER, C_WARN_BG, "[!] IMPORTANT:"),
        "danger": (C_DANGER_BORDER, C_DANGER_BG, "[X] CAUTION:"),
    }
    border_color, bg_color, default_prefix = type_map.get(
        box_type.lower(), (C_INFO_BORDER, C_INFO_BG, "NOTE:")
    )

    cell_flowables = [
        Paragraph(f"<b>{default_prefix} {title}</b>", styles["CalloutBold"]),
        Spacer(1, 2),
    ]
    if isinstance(content_paragraphs, list):
        for p in content_paragraphs:
            if isinstance(p, str):
                cell_flowables.append(Paragraph(p, styles["CalloutText"]))
            else:
                cell_flowables.append(p)
    elif isinstance(content_paragraphs, str):
        cell_flowables.append(Paragraph(content_paragraphs, styles["CalloutText"]))

    t = Table([[cell_flowables]], colWidths=[USABLE_WIDTH])
    t.setStyle(
        TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), bg_color),
            ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
            ("LINEBEFORE", (0, 0), (0, -1), 3.5, border_color),
            ("TOPPADDING", (0, 0), (-1, -1), 5),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ("LEFTPADDING", (0, 0), (-1, -1), 9),
            ("RIGHTPADDING", (0, 0), (-1, -1), 9),
        ])
    )
    return t


def build_manual_story():
    """Generates the full story list for the complete SmartAttendance manual."""
    styles = get_styles()
    story = []

    # =========================================================================
    # COVER PAGE
    # =========================================================================
    story.append(Spacer(1, 15))
    story.append(Paragraph("OFFICIAL SYSTEM SPECIFICATION & OPERATING MANUAL", styles["CoverTag"]))
    story.append(Paragraph("SmartAttendance™ Edge Biometric Terminal", styles["CoverTitle"]))
    story.append(Paragraph(
        "Complete End-to-End Operating Guide: Hardware, Power, Enrollment, Attendance Scanning, "
        "Scheduling, Database Management, Charging & Telegram Cloud Synchronization",
        styles["CoverSubtitle"]
    ))

    story.append(HRFlowable(width="100%", thickness=3, color=C_SECONDARY, spaceBefore=2, spaceAfter=14))

    # High-level architecture summary box
    cover_highlights = [
        "<b>Edge AI Core:</b> Espressif ESP32-P4 Dual-Core RISC-V @ 400 MHz with 32 MB 200 MHz Hex-PSRAM",
        "<b>Display & Vision:</b> 7.0″ 1024×600 IPS Touchscreen (MIPI-DSI) and OmniVision MIPI-CSI Camera",
        "<b>Power & Charging:</b> USB-C 5V/3A, Onboard LiPo Charging Circuit, STC8 Gas Gauge, Auto-Protection",
        "<b>Zero-Cloud Biometrics:</b> 100% On-Device Face Detection, Landmark Alignment & MobileFaceNet (< 1s)",
        "<b>Robust Enrollment:</b> 30-Frame K-Means Pose Clustering, Adaptive Outlier Filter, Quality Scorecard",
        "<b>Lecture Scheduling:</b> Conversational Telegram Bot Wizard (`/schedule`) & REST Cloud Sync API",
        "<b>Dual Telegram Ecosystem:</b> Student Check-in Push Alerts and Lecturer Session Summaries",
        "<b>Database & Reporting:</b> Offline SQLite3 (`/sdcard/attendance.db`) with On-Device CSV Export",
    ]
    cov_p = [Paragraph(f"• {item}", styles["Body"]) for item in cover_highlights]
    t_cov = Table([[cov_p]], colWidths=[USABLE_WIDTH])
    t_cov.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 1, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 10),
    ]))
    story.append(t_cov)
    story.append(Spacer(1, 25))

    # Metadata Grid
    meta_data = [
        [
            Paragraph("<b>Document Version:</b>", styles["CoverMetaBold"]),
            Paragraph("2.0.0 (Comprehensive Edition)", styles["CoverMeta"]),
            Paragraph("<b>Hardware Platform:</b>", styles["CoverMetaBold"]),
            Paragraph("CrowPanel Advanced 7.0″ ESP32-P4 v1.0", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Firmware Base:</b>", styles["CoverMetaBold"]),
            Paragraph("ESP-IDF v5.3+ / ESP-DL v1.1.4+", styles["CoverMeta"]),
            Paragraph("<b>Display Controller:</b>", styles["CoverMetaBold"]),
            Paragraph("EK79007 (MIPI-DSI) + GT911 Touch", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Target Audience:</b>", styles["CoverMetaBold"]),
            Paragraph("Administrators, Lecturers, IT & Facility Staff", styles["CoverMeta"]),
            Paragraph("<b>Security Standard:</b>", styles["CoverMetaBold"]),
            Paragraph("Offline Edge Biometric Vector Storage", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Publication Date:</b>", styles["CoverMetaBold"]),
            Paragraph("October 2026", styles["CoverMeta"]),
            Paragraph("<b>Document ID:</b>", styles["CoverMetaBold"]),
            Paragraph("SA-ESP32P4-UM-2026-FULL", styles["CoverMeta"]),
        ],
    ]
    t_meta = Table(meta_data, colWidths=[110, 160, 125, 145])
    t_meta.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("LEFTPADDING", (0, 0), (-1, -1), 2),
        ("RIGHTPADDING", (0, 0), (-1, -1), 2),
    ]))
    story.append(t_meta)

    story.append(Spacer(1, 20))
    story.append(make_callout(
        "tip",
        "Document Purpose & Compliance",
        "This user manual provides the authoritative operational, electrical, and administrative instructions for the entire SmartAttendance system ecosystem. It covers physical installation, power/charging management, biometric enrollment, classroom attendance scanning, lecture scheduling via Telegram, database maintenance, and disaster recovery.",
        styles
    ))

    story.append(PageBreak())

    # =========================================================================
    # TABLE OF CONTENTS & SYSTEM ARCHITECTURE
    # =========================================================================
    story.append(Paragraph("Table of Contents", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1.5, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    toc_entries = [
        ("Chapter 1: System Overview & Architecture", "Product philosophy, Edge AI capabilities, system block diagram"),
        ("Chapter 2: Hardware Architecture & Pin Interfaces", "ESP32-P4 SoC, display bridge, touch, camera, audio, pin mappings"),
        ("Chapter 3: Power Architecture, Battery & Charging", "USB-C, battery management, charging indicators, power-saving, shutdown"),
        ("Chapter 4: Commissioning & Initial Setup", "Unboxing, MicroSD card preparation, Setup Wizard, Master Admin PIN"),
        ("Chapter 5: Biometric Face Recognition Engine", "ESP-DL inference, 5-point alignment, 128D embeddings, dynamic margin"),
        ("Chapter 6: Facial Enrollment Procedures", "On-device UI, SoftAP Web Portal, 8-step quality evaluation scorecard"),
        ("Chapter 7: Lecture Scheduling & Course Management", "Telegram Bot `/schedule` wizard, FastAPI Cloud sync, active session"),
        ("Chapter 8: Daily Attendance Scanning Operations", "Main scanner UI, verification badges, multi-frame voting, debounce logic"),
        ("Chapter 9: User Database & Roster Administration", "Viewing users, search, role editing, non-destructive re-enrollment, delete"),
        ("Chapter 10: Attendance Records, Reports & CSV Export", "Session filtering, on-screen reports, SD card CSV export, cloud push"),
        ("Chapter 11: Telegram Bot Cloud Ecosystem", "Student check-in alerts, lecturer portal, admin remote control commands"),
        ("Chapter 12: System Settings, Maintenance & OTA Updates", "Backlight PWM, audio volume, Wi-Fi configuration, dual-bank OTA"),
        ("Chapter 13: Troubleshooting & Fault Diagnostics", "Hardware recovery, camera init, touch quirks, SD mount, error codes"),
        ("Chapter 14: Technical Specifications & Safety Ratings", "Electrical ratings, temperature limits, mechanical mounting, care"),
    ]

    toc_table_data = []
    for title, desc in toc_entries:
        toc_table_data.append([
            Paragraph(f"<b>{title}</b>", styles["TOCItemBold"]),
            Paragraph(f"<font color='{C_MUTED.hexval()}'>{desc}</font>", styles["TOCItem"]),
        ])

    t_toc = Table(toc_table_data, colWidths=[185, 355])
    t_toc.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3.5),
        ("TOPPADDING", (0, 0), (-1, -1), 3.5),
        ("LINEBELOW", (0, 0), (-1, -1), 0.5, C_BG_LIGHT),
    ]))
    story.append(t_toc)

    story.append(Spacer(1, 10))
    story.append(Paragraph("System Architecture Overview", styles["H2"]))
    story.append(Paragraph(
        "The <b>SmartAttendance</b> ecosystem operates on a decoupled edge-cloud architecture designed specifically for educational institutions and corporate enterprises. The physical hardware terminal performs 100% of face detection, alignment, feature extraction, and database matching on local RISC-V silicon. The cloud service operates as an asynchronous coordination layer (Telegram Bot + FastAPI sync engine) that distributes lecture schedules to terminals and broadcasts instant push notifications to students upon successful verification.",
        styles["Body"]
    ))

    # Architecture block table
    pipe_data = [
        [
            Paragraph("<b>1. PHYSICAL TERMINAL</b>", styles["TableHeader"]),
            Paragraph("<b>2. EDGE AI PIPELINE</b>", styles["TableHeader"]),
            Paragraph("<b>3. PERSISTENCE ENGINE</b>", styles["TableHeader"]),
            Paragraph("<b>4. TELEGRAM & CLOUD</b>", styles["TableHeader"]),
        ],
        [
            Paragraph("• 7″ IPS Touch (MIPI-DSI)<br/>• SC2336 Camera (MIPI-CSI)<br/>• LiPo Battery & STC8 Gauge<br/>• MicroSD FAT32 Storage<br/>• USB-C 5V/3A Power", styles["TableCell"]),
            Paragraph("• ESP-DL HumanFaceDetect<br/>• 5-Point Affine Warping<br/>• MobileFaceNet int8 Model<br/>• 128D Cosine Similarity<br/>• Dynamic Margin Gate", styles["TableCell"]),
            Paragraph("• SQLite3 Embedded DB<br/>• <code>/sdcard/attendance.db</code><br/>• CSV Attendance Logs<br/>• User Biometric Vectors<br/>• NVS Configuration", styles["TableCell"]),
            Paragraph("• Student Push Alerts<br/>• Lecturer <code>/schedule</code><br/>• Admin Remote Console<br/>• Web AP <code>register.html</code><br/>• Dual-Bank OTA Updates", styles["TableCell"]),
        ],
    ]
    t_pipe = Table(pipe_data, colWidths=[135, 135, 135, 135])
    t_pipe.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("BACKGROUND", (0, 1), (-1, -1), C_CARD_BG),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_pipe)

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 1 & 2: HARDWARE SPECIFICATIONS & PIN INTERFACES
    # =========================================================================
    story.append(Paragraph("Chapter 1: System Overview & Technical Specifications", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "The SmartAttendance terminal is built on the <b>Elecrow CrowPanel Advanced 7.0″ ESP32-P4 v1.0</b> development platform. The hardware combines high-speed MIPI interfaces with dual-core processing power to execute complex deep learning inference on edge silicon without external accelerators.",
        styles["Body"]
    ))

    hw_specs = [
        [Paragraph("<b>Component / Subsystem</b>", styles["TableHeader"]), Paragraph("<b>Technical Specification & Architecture Details</b>", styles["TableHeader"])],
        [Paragraph("Processor (SoC)", styles["TableCellBold"]), Paragraph("Espressif ESP32-P4 dual-core RISC-V @ up to 400 MHz (LP Core @ 40 MHz)", styles["TableCell"])],
        [Paragraph("System Memory (PSRAM)", styles["TableCellBold"]), Paragraph("32 MB Hex-Mode (16-line) PSRAM clocked at 200 MHz; external data bus", styles["TableCell"])],
        [Paragraph("Internal Flash", styles["TableCellBold"]), Paragraph("16 MB Quad-SPI Flash (6 MB factory app partition, NVS, PHY calibration)", styles["TableCell"])],
        [Paragraph("Display Panel", styles["TableCellBold"]), Paragraph("7.0″ IPS TFT LCD, 1024×600 native resolution, EK79007 MIPI-DSI bridge (2 data lanes @ 900 Mbps, DPI clock 51 MHz)", styles["TableCell"])],
        [Paragraph("Touch Controller", styles["TableCellBold"]), Paragraph("Goodix GT911 Capacitive Touch (I2C address 0x5D, SDA: GPIO45, SCL: GPIO46, RST: GPIO40, INT: GPIO42)", styles["TableCell"])],
        [Paragraph("Backlight Control", styles["TableCellBold"]), Paragraph("Hardware PWM on <b>GPIO31</b> (30 kHz frequency, 11-bit resolution, formula: <code>duty = percent*18 + 200</code>)", styles["TableCell"])],
        [Paragraph("Camera Subsystem", styles["TableCellBold"]), Paragraph("OmniVision SC2336 (24-pin MIPI-CSI, SCCB 0x36), 1280×720 RAW8 @ 30fps, hardware ISP Bayer BGGR", styles["TableCell"])],
        [Paragraph("MicroSD Card Interface", styles["TableCellBold"]), Paragraph("SDMMC 1-bit mode (CLK: GPIO43, CMD: GPIO44, D0: GPIO39), FAT32 mounted at <code>/sdcard</code>", styles["TableCell"])],
        [Paragraph("External RTC (Optional)", styles["TableCellBold"]), Paragraph("DS1302 Real-Time Clock on J7 Header (CLK: GPIO51, DAT: GPIO50, CE: GPIO49) with CR2032 coin backup", styles["TableCell"])],
        [Paragraph("Connectivity", styles["TableCellBold"]), Paragraph("Wi-Fi 802.11 b/g/n (2.4 GHz) via companion wireless bridge; SoftAP mode for captive enrollment portal", styles["TableCell"])],
        [Paragraph("Power Architecture", styles["TableCellBold"]), Paragraph("5V DC via USB Type-C or screw terminals; onboard LiPo battery charger with STC8 telemetry gauge", styles["TableCell"])],
    ]
    t_hw = Table(hw_specs, colWidths=[140, 400])
    t_hw.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 4),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_hw)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 2: Hardware Architecture & Pin Interfaces", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "Proper electrical connections and pin configurations are vital during assembly and maintenance. The table below details critical hardware interfaces:",
        styles["Body"]
    ))

    pin_table = [
        [Paragraph("<b>Interface</b>", styles["TableHeader"]), Paragraph("<b>GPIO Pin</b>", styles["TableHeader"]), Paragraph("<b>Signal Type</b>", styles["TableHeader"]), Paragraph("<b>Functional Notes & Electrical Behavior</b>", styles["TableHeader"])],
        [Paragraph("Backlight PWM", styles["TableCellBold"]), Paragraph("GPIO 31", styles["TableCell"]), Paragraph("PWM Output", styles["TableCell"]), Paragraph("High-frequency 30 kHz dimmer; 0% disables backlight, 100% full illumination.", styles["TableCell"])],
        [Paragraph("Touch I2C SDA", styles["TableCellBold"]), Paragraph("GPIO 45", styles["TableCell"]), Paragraph("Open Drain", styles["TableCell"]), Paragraph("External 4.7kΩ pull-up required; legacy I2C driver on I2C_NUM_0.", styles["TableCell"])],
        [Paragraph("Touch I2C SCL", styles["TableCellBold"]), Paragraph("GPIO 46", styles["TableCell"]), Paragraph("Open Drain", styles["TableCell"]), Paragraph("External 4.7kΩ pull-up required; clocked at 100 kHz standard speed.", styles["TableCell"])],
        [Paragraph("Touch Reset (RST)", styles["TableCellBold"]), Paragraph("GPIO 40", styles["TableCell"]), Paragraph("Output Push-Pull", styles["TableCell"]), Paragraph("Driven manually during boot to lock GT911 address at 0x5D.", styles["TableCell"])],
        [Paragraph("Touch Interrupt (INT)", styles["TableCellBold"]), Paragraph("GPIO 42", styles["TableCell"]), Paragraph("Output Push-Pull", styles["TableCell"]), Paragraph("Low level during reset pulse; releases high to complete address locking.", styles["TableCell"])],
        [Paragraph("SD Card Clock", styles["TableCellBold"]), Paragraph("GPIO 43", styles["TableCell"]), Paragraph("Clock Output", styles["TableCell"]), Paragraph("SDMMC 1-bit high-speed clock line.", styles["TableCell"])],
        [Paragraph("SD Card Command", styles["TableCellBold"]), Paragraph("GPIO 44", styles["TableCell"]), Paragraph("Bidirectional", styles["TableCell"]), Paragraph("SDMMC command/response line with internal pull-up.", styles["TableCell"])],
        [Paragraph("SD Card Data 0", styles["TableCellBold"]), Paragraph("GPIO 39", styles["TableCell"]), Paragraph("Bidirectional", styles["TableCell"]), Paragraph("SDMMC data line D0 for 1-bit sector transfers.", styles["TableCell"])],
        [Paragraph("Battery ADC Sense", styles["TableCellBold"]), Paragraph("GPIO 4 (ADC_CH4)", styles["TableCell"]), Paragraph("Analog Input", styles["TableCell"]), Paragraph("Voltage divider monitoring terminal battery voltage (3.3V to 4.2V).", styles["TableCell"])],
    ]
    t_pin = Table(pin_table, colWidths=[90, 75, 85, 290])
    t_pin.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_pin)

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 3: POWER ARCHITECTURE, BATTERY & CHARGING
    # =========================================================================
    story.append(Paragraph("Chapter 3: Power Architecture, Battery Management & Charging", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "The SmartAttendance terminal is engineered for dual-mode power operation: stationary wall-powered continuous operation and portable, battery-powered deployment for mobile classrooms, field examinations, and seminar venues.",
        styles["Body"]
    ))

    story.append(Paragraph("3.1 Power Supply Inputs & Requirements", styles["H2"]))
    power_points = [
        "<b>Primary USB Type-C Input:</b> 5.0 V DC ±5%, 3.0 A recommended. Powers the terminal and simultaneously charges the internal battery.",
        "<b>Auxiliary Screw Terminal (VIN / GND):</b> 5.0 V regulated DC input for fixed wall-mounted kiosk installations using enterprise DIN-rail power supplies.",
        "<b>Peak Consumption Profile:</b> During boot, camera burst capture, and Wi-Fi sync, current spikes reach <b>2.2 A</b>. A substandard 500mA PC USB port will induce brownout resets.",
    ]
    for pp in power_points:
        story.append(Paragraph(f"• {pp}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("3.2 Lithium-Ion Battery & STC8 Gas Gauge Architecture", styles["H2"]))
    story.append(Paragraph(
        "The CrowPanel Advanced ESP32-P4 incorporates an onboard Lithium-Polymer (LiPo) / Lithium-Ion charging management IC paired with an STC8 micro-controller acting as an intelligent fuel gauge:",
        styles["Body"]
    ))

    batt_specs = [
        [Paragraph("<b>Parameter</b>", styles["TableHeader"]), Paragraph("<b>Operating Value / Threshold</b>", styles["TableHeader"]), Paragraph("<b>System Behavior & Enforcement</b>", styles["TableHeader"])],
        [
            Paragraph("Battery Chemistry", styles["TableCellBold"]),
            Paragraph("Li-Po / Li-Ion (1S / 3.7V nominal)", styles["TableCell"]),
            Paragraph("Compatible with standard single-cell 3.7V packs (2500mAh to 10000mAh capacity).", styles["TableCell"]),
        ],
        [
            Paragraph("Full Voltage (100%)", styles["TableCellBold"]),
            Paragraph("4.20 V DC", styles["TableCell"]),
            Paragraph("Charging terminates automatically. Battery icon shows full solid green bar.", styles["TableCell"]),
        ],
        [
            Paragraph("Empty Voltage (0%)", styles["TableCellBold"]),
            Paragraph("3.30 V DC", styles["TableCell"]),
            Paragraph("Baseline cutoff voltage. Device initiates protected emergency shutdown.", styles["TableCell"]),
        ],
        [
            Paragraph("Low Battery Warning", styles["TableCellBold"]),
            Paragraph("15% State of Charge", styles["TableCell"]),
            Paragraph("Battery icon turns flashing amber/red. An advisory banner prompts: 'Battery Low (15%). Please connect charger.'", styles["TableCell"]),
        ],
        [
            Paragraph("Critical Shutdown", styles["TableCellBold"]),
            Paragraph("5% State of Charge", styles["TableCell"]),
            Paragraph("System pauses camera tasks, flushes SQLite transactions to MicroSD, safely unmounts FAT32, and powers down to prevent database corruption.", styles["TableCell"]),
        ],
        [
            Paragraph("Telemetry Polling", styles["TableCellBold"]),
            Paragraph("10-Second Interval", styles["TableCell"]),
            Paragraph("ADC channel 4 and STC8 coprocessor read cell voltage and compute smoothed moving average.", styles["TableCell"]),
        ],
    ]
    t_batt = Table(batt_specs, colWidths=[110, 130, 300])
    t_batt.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 4),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_batt)

    story.append(Spacer(1, 8))
    story.append(Paragraph("3.3 On-Screen Battery & Charging Indicators", styles["H2"]))
    story.append(Paragraph(
        "The top-right status bar of the LVGL touchscreen provides continuous real-time power telemetry:",
        styles["Body"]
    ))
    ui_batt_bullets = [
        "<b>Charging Active (<font color='#00CFFF'>Cyan Lightning Bolt</font>):</b> When a 5V USB-C charger is connected, the battery symbol switches to <code>LV_SYMBOL_CHARGE</code> in glowing cyan (<code>#00CFFF</code>), accompanied by the live percentage (e.g., <code>[bolt] 74%</code>).",
        "<b>Normal Discharging (<font color='#FFFFFF'>White Battery Bars</font>):</b><br/>"
        "• <b>90% – 100%:</b> <code>LV_SYMBOL_BATTERY_FULL</code> (4 bars)<br/>"
        "• <b>60% – 89%:</b> <code>LV_SYMBOL_BATTERY_3</code> (3 bars)<br/>"
        "• <b>30% – 59%:</b> <code>LV_SYMBOL_BATTERY_2</code> (2 bars)<br/>"
        "• <b>10% – 29%:</b> <code>LV_SYMBOL_BATTERY_1</code> (1 bar)",
        "<b>Critical Low (<font color='#FF4444'>Flashing Red Empty</font>):</b> Below 10%, the icon displays <code>LV_SYMBOL_BATTERY_EMPTY</code> in bold red (<code>#FF4444</code>).",
    ]
    for ub in ui_batt_bullets:
        story.append(Paragraph(f"• {ub}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("3.4 Recommended Charging & Battery Maintenance Procedures", styles["H2"]))
    charge_procedures = [
        "<b>Standard Full Charge Cycle:</b> Connect a 5V/3A USB-C charger. A fully discharged 5000mAh battery requires approximately <b>2.5 to 3.5 hours</b> to reach 100% full capacity.",
        "<b>Charging While Operating:</b> The device supports pass-through charging. It can conduct classroom attendance scanning while connected to power without overheating.",
        "<b>Storage Recommendations:</b> If storing the terminal inactive between academic semesters, charge the battery to approximately <b>50% – 60%</b> and store in a cool, dry room (15°C – 25°C). Recharge every 3 months to prevent cell degradation.",
        "<b>Battery Calibration Logging:</b> For technical audits, firmware includes a calibration logger (`BATTERY_CALIB_LOGGING=1` in `config.h`) that logs terminal mV and STC8 readings to `/sdcard/logs/batt_calib.csv` every 20 seconds.",
    ]
    for cp in charge_procedures:
        story.append(Paragraph(f"• {cp}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(make_callout(
        "warn",
        "Battery Safety Rules",
        "Do NOT puncture, expose to open flame, or crush the internal battery pack. If the battery shows physical swelling, immediately disconnect the USB-C charger, power down the device, and replace the cell with a manufacturer-approved 3.7V LiPo pack equipped with an integrated over-voltage/under-voltage protection circuit (PCM).",
        styles
    ))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 4: COMMISSIONING & INITIAL SETUP
    # =========================================================================
    story.append(Paragraph("Chapter 4: Commissioning & Initial Setup", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "Follow these procedures to prepare the MicroSD card, execute the First Boot Setup Wizard, and establish system administration security.",
        styles["Body"]
    ))

    story.append(Paragraph("4.1 MicroSD Card Preparation & Directory Layout", styles["H2"]))
    story.append(Paragraph(
        "The terminal requires a high-speed MicroSD card (UHS-I Class 10, FAT32 format). Before powering on the device, create the following directory structure on the card:",
        styles["Body"]
    ))

    sd_layout = (
        "/sdcard/\n"
        "  |-- models/p4/\n"
        "  |     |-- human_face_detect_msr_s8_v1.espdl   (Face detection stage 1)\n"
        "  |     |-- human_face_detect_mnp_s8_v1.espdl   (Face landmark alignment stage 2)\n"
        "  |     `-- mfn_s8_v1.espdl                     (MobileFaceNet 128D embedding model)\n"
        "  |-- attendance.db                            (SQLite database; auto-created on boot)\n"
        "  |-- attendance_backup.db                     (Periodic database snapshot)\n"
        "  |-- reports/                                 (Directory for exported attendance CSV files)\n"
        "  `-- logs/                                    (System telemetry & battery calibration logs)"
    )
    t_sd = Table([[Paragraph(f"<font face='Courier' size='7.5'>{sd_layout.replace(chr(10), '<br/>').replace(' ', '&nbsp;')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_sd.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 6),
    ]))
    story.append(t_sd)

    story.append(Spacer(1, 8))
    story.append(Paragraph("4.2 First Boot & Interactive Setup Wizard", styles["H2"]))
    story.append(Paragraph(
        "When powered on with a fresh database, the terminal automatically launches the <b>Interactive Setup Wizard</b>:",
        styles["Body"]
    ))

    wiz_steps = [
        "<b>Step 1: Hardware Self-Test:</b> The firmware validates 32MB Hex-PSRAM, MIPI-DSI display bridge, GT911 touch coordinates, camera sensor, and mounts `/sdcard`. All subsystems must show a green checkmark.",
        "<b>Step 2: Master Administrator PIN:</b> Set a secure <b>4-digit Master PIN</b> (default initial PIN is <code>1234</code>). This PIN protects User Management, Attendance Reports, and System Settings.",
        "<b>Step 3: Institution Identity:</b> Enter the Department Name (e.g., <code>Computer Science</code>) and Terminal Location (e.g., <code>Lecture Theatre 1</code>). These are saved to NVS and appear on report headers.",
        "<b>Step 4: Date, Time & RTC Sync:</b> Configure your timezone offset (e.g., UTC+1). When connected to Wi-Fi, time is synchronized via SNTP. If an external DS1302 RTC battery is installed, time is preserved across reboots.",
        "<b>Step 5: Wi-Fi Provisioning:</b> Select your institution's 2.4 GHz Wi-Fi network and enter the passphrase using the on-screen keyboard, or tap <b>[Skip Network]</b> for 100% offline deployment.",
    ]
    for ws in wiz_steps:
        story.append(Paragraph(f"• {ws}", styles["Bullet"]))

    story.append(Spacer(1, 10))

    # =========================================================================
    # CHAPTER 5: BIOMETRIC FACE RECOGNITION ENGINE
    # =========================================================================
    story.append(Paragraph("Chapter 5: Biometric Face Recognition Engine", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "SmartAttendance executes real-time machine vision entirely on the ESP32-P4 RISC-V cores. Understanding the biometric pipeline ensures optimal lighting and positioning.",
        styles["Body"]
    ))

    story.append(Paragraph("5.1 Real-Time Machine Vision Pipeline", styles["H2"]))

    stages = [
        [Paragraph("<b>Pipeline Stage</b>", styles["TableHeader"]), Paragraph("<b>Algorithm / Engine</b>", styles["TableHeader"]), Paragraph("<b>Operation & Quality Criteria</b>", styles["TableHeader"])],
        [
            Paragraph("1. Acquisition", styles["TableCellBold"]),
            Paragraph("MIPI-CSI Camera", styles["TableCell"]),
            Paragraph("Streams 1280×720 RAW8 frames at 30 fps. Double-buffered in SPIRAM (`s_detection_fb_buf`) to prevent DMA tearing.", styles["TableCell"]),
        ],
        [
            Paragraph("2. Face Detection", styles["TableCellBold"]),
            Paragraph("ESP-DL HumanFaceDetect", styles["TableCell"]),
            Paragraph("Quantized int8 neural network detects faces in 320×240 preview. Bounding box must be ≥ 30px with confidence ≥ 0.60.", styles["TableCell"]),
        ],
        [
            Paragraph("3. Landmark Alignment", styles["TableCellBold"]),
            Paragraph("5-Point Keypoint Warping", styles["TableCell"]),
            Paragraph("Extracts left eye, right eye, nose, left mouth, right mouth. Warps facial crop to a canonical 112×112 RGB image.", styles["TableCell"]),
        ],
        [
            Paragraph("4. Quality Filtering", styles["TableCellBold"]),
            Paragraph("Laplacian, Yaw & Glare Gates", styles["TableCell"]),
            Paragraph("Discards blurry frames (sharpness < 5.0), yaw > 35°, specular glare (>15% pixels Y>240), and open mouth ratio (>0.85).", styles["TableCell"]),
        ],
        [
            Paragraph("5. Embedding Extraction", styles["TableCellBold"]),
            Paragraph("MobileFaceNet int8", styles["TableCell"]),
            Paragraph("Compresses 112×112 aligned face into a compact, normalized 128-dimensional signed integer vector embedding.", styles["TableCell"]),
        ],
        [
            Paragraph("6. Multi-Frame Consensus", styles["TableCellBold"]),
            Paragraph("Dynamic Margin Voting", styles["TableCell"]),
            Paragraph("Accumulates 6 frames (or early-exit 3 unanimous frames ≥ 0.80). Validates tiered margin over runner-up before confirming identity.", styles["TableCell"]),
        ],
    ]
    t_stages = Table(stages, colWidths=[95, 125, 320])
    t_stages.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_stages)

    story.append(Spacer(1, 6))
    story.append(Paragraph("5.2 Tiered Dynamic Margin Anti-Collision Gate", styles["H2"]))
    story.append(Paragraph(
        "To prevent identity confusion between candidates with similar facial features (e.g., siblings or similar bone structures), the system enforces a <b>Dynamic Separation Margin</b>:",
        styles["Body"]
    ))
    margin_rules = [
        "<b>High Similarity (S ≥ 0.78):</b> High-confidence match. Requires a separation margin of ΔS ≥ 0.03 over candidate #2.",
        "<b>Mid Similarity (0.74 ≤ S < 0.78):</b> Standard match. Requires a separation margin of ΔS ≥ 0.05 over candidate #2.",
        "<b>Borderline Similarity (0.70 ≤ S < 0.74):</b> Borderline match. Requires a strict separation margin of <b>ΔS ≥ 0.08</b> over candidate #2. If margin is insufficient, identity is withheld as ambiguous.",
    ]
    for mr in margin_rules:
        story.append(Paragraph(f"• {mr}", styles["Bullet"]))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 6: FACIAL ENROLLMENT PROCEDURES
    # =========================================================================
    story.append(Paragraph("Chapter 6: Facial Enrollment Procedures", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "Facial enrollment registers an individual's biometric profile into the local SQLite database. SmartAttendance features an 8-step statistically robust template synthesis algorithm that avoids single-frame capture errors.",
        styles["Body"]
    ))

    story.append(Paragraph("6.1 Enrollment Method 1: Direct On-Device Touch UI", styles["H2"]))
    enroll_steps = [
        "<b>Open User Manager:</b> From the main screen, tap the top menu icon, select <b>User Management</b>, and enter the 4-digit Master Admin PIN.",
        "<b>Input User Information:</b> Tap <b>[+ Add User]</b>. Enter Full Name, Matric / Staff ID (e.g., <code>CSC/2022/1045</code>), Phone Number (international format: <code>+2348012345678</code>), and assign Role (<code>Student</code>, <code>Lecturer</code>, or <code>Admin</code>).",
        "<b>Position Candidate:</b> Guide the user to stand 0.5 to 0.8 meters directly in front of the camera, looking at the display with a neutral facial expression.",
        "<b>30-Frame Capture:</b> Tap <b>[Start Face Capture]</b>. The device captures 30 quality-verified facial frames while the progress bar advances from 0% to 100%.",
        "<b>Quality Scorecard:</b> Upon completion, the screen displays the enrollment quality score (e.g., <code>89/100 EXCELLENT</code>) and writes the synthesized vector to SQLite.",
    ]
    for es in enroll_steps:
        story.append(Paragraph(f"• {es}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("6.2 Enrollment Method 2: SoftAP Captive Web Portal (`register.html`)", styles["H2"]))
    story.append(Paragraph(
        "For bulk classroom registration without crowding the terminal touch screen, administrators can use any smartphone or laptop:",
        styles["Body"]
    ))
    web_steps = [
        "<b>Connect Wi-Fi:</b> On your mobile device, connect to the terminal's Wi-Fi hotspot: <code>Attendance_Setup</code>.",
        "<b>Access Web Portal:</b> Open a web browser and navigate to <code>http://192.168.4.1/register.html</code>.",
        "<b>Enter Session PIN:</b> Enter the dynamic 4-digit session PIN shown on the physical terminal screen banner.",
        "<b>Queue Candidate:</b> Fill in the student's name, matriculation number, department, and phone number, then tap <b>[Queue for Capture]</b>.",
        "<b>Automatic Camera Trigger:</b> The student on-site looks at the terminal screen. The device detects the queued candidate, captures the 30-frame sequence, displays a success prompt, and confirms back to the admin's phone.",
    ]
    for ws in web_steps:
        story.append(Paragraph(f"• {ws}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("6.3 The 8-Step Statistically Robust Enrollment Algorithm", styles["H2"]))

    algo_steps = [
        [Paragraph("<b>Step</b>", styles["TableHeader"]), Paragraph("<b>Algorithmic Function</b>", styles["TableHeader"]), Paragraph("<b>Mathematical & Embedded Implementation</b>", styles["TableHeader"])],
        [
            Paragraph("Step 1", styles["TableCellBold"]),
            Paragraph("Sample Acquisition", styles["TableCell"]),
            Paragraph("Collects N=30 valid frames. Rejects frames if face count≠1, sharpness<5.0, yaw>35°, or mouth ratio>0.85.", styles["TableCell"]),
        ],
        [
            Paragraph("Step 2", styles["TableCellBold"]),
            Paragraph("PSRAM Storage", styles["TableCell"]),
            Paragraph("Stores all 30 extracted 128D vectors in PSRAM-backed buffer array, eliminating SRAM stack overflow.", styles["TableCell"]),
        ],
        [
            Paragraph("Step 3", styles["TableCellBold"]),
            Paragraph("Adaptive Outlier Removal", styles["TableCell"]),
            Paragraph("Calculates 30×30 cosine similarity matrix. Rejects samples where mean similarity <code>m_i < max(μ - 1.5σ, 0.65)</code>.", styles["TableCell"]),
        ],
        [
            Paragraph("Step 4 & 5", styles["TableCellBold"]),
            Paragraph("K-Means Pose Clustering", styles["TableCell"]),
            Paragraph("Runs K=3 K-Means clustering on inliers to group frontal, slight left, and slight right poses. Prunes empty clusters.", styles["TableCell"]),
        ],
        [
            Paragraph("Step 6", styles["TableCellBold"]),
            Paragraph("Cluster Weighted Fusion", styles["TableCell"]),
            Paragraph("Synthesizes centroid vector weighted by cluster size. Rescales vector to mean L2 target norm and quantizes to int8.", styles["TableCell"]),
        ],
        [
            Paragraph("Step 7", styles["TableCellBold"]),
            Paragraph("Quality Scoring (Q)", styles["TableCell"]),
            Paragraph("Computes quality index: <code>Q = 0.40*R_inlier + 0.40*C_compact + 0.20*S_sharp</code> (0 to 100 scale).", styles["TableCell"]),
        ],
        [
            Paragraph("Step 8", styles["TableCellBold"]),
            Paragraph("Persistence & Feedback", styles["TableCell"]),
            Paragraph("Stores the single representative vector in SQLite. Displays score badge; enables 'Redo' button ONLY if POOR (<55).", styles["TableCell"]),
        ],
    ]
    t_algo = Table(algo_steps, colWidths=[65, 140, 335])
    t_algo.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_algo)

    story.append(Spacer(1, 8))
    story.append(make_callout(
        "tip",
        "Duplicate Face Prevention",
        "During enrollment, the terminal automatically compares the newly synthesized vector against the entire enrolled user cache. If cosine similarity matches an existing student (S ≥ 0.70), enrollment halts and displays: 'Face already registered! Cannot create duplicate identity.'",
        styles
    ))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 7: LECTURE SCHEDULING & COURSE MANAGEMENT
    # =========================================================================
    story.append(Paragraph("Chapter 7: Lecture Scheduling & Course Session Management", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "SmartAttendance integrates an automated lecture scheduling system. Lecturers create and manage class timetables directly through the official Telegram bot, which asynchronously synchronizes with the offline physical terminal.",
        styles["Body"]
    ))

    story.append(Paragraph("7.1 Lecturer Verification & Authentication", styles["H2"]))
    story.append(Paragraph(
        "Before scheduling lectures, instructors verify their identity via Telegram:",
        styles["Body"]
    ))
    sched_auth = [
        "Open Telegram and search for the institutional bot: <code>@MyUniAttendance_Bot</code>.",
        "Send the <code>/start</code> command.",
        "Tap the <b>[Share Contact Details]</b> button. The bot normalizes the instructor's phone number and matches it against the lecturer profiles synchronized from the physical terminal.",
        "Upon successful verification, the instructor's Telegram ID is linked to their profile, unlocking scheduling permissions.",
    ]
    for sa in sched_auth:
        story.append(Paragraph(f"• {sa}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("7.2 The 5-Step Scheduling Wizard (`/schedule`)", styles["H2"]))
    story.append(Paragraph(
        "To schedule a lecture session, the instructor sends <code>/schedule</code> to the bot. The conversational wizard guides them through five simple steps:",
        styles["Body"]
    ))

    wiz_table = [
        [Paragraph("<b>Step</b>", styles["TableHeader"]), Paragraph("<b>Prompt Field</b>", styles["TableHeader"]), Paragraph("<b>Example Input</b>", styles["TableHeader"]), Paragraph("<b>Validation & Formatting Rules</b>", styles["TableHeader"])],
        [Paragraph("Step 1", styles["TableCellBold"]), Paragraph("Course Code", styles["TableCell"]), Paragraph("<code>CSC 301</code>", styles["TableCell"]), Paragraph("Alphanumeric department course code (e.g., CSC301, EEE402).", styles["TableCell"])],
        [Paragraph("Step 2", styles["TableCellBold"]), Paragraph("Course Title", styles["TableCell"]), Paragraph("<code>Database Systems</code>", styles["TableCell"]), Paragraph("Full descriptive name of the lecture course.", styles["TableCell"])],
        [Paragraph("Step 3", styles["TableCellBold"]), Paragraph("Session Date", styles["TableCell"]), Paragraph("<code>24/10/2026</code>", styles["TableCell"]), Paragraph("Format: <code>DD/MM/YYYY</code>. Cannot be set to past dates.", styles["TableCell"])],
        [Paragraph("Step 4", styles["TableCellBold"]), Paragraph("Start Time", styles["TableCell"]), Paragraph("<code>09:00</code>", styles["TableCell"]), Paragraph("Format: <code>HH:MM</code> in 24-hour clock (e.g., 09:00, 14:30).", styles["TableCell"])],
        [Paragraph("Step 5", styles["TableCellBold"]), Paragraph("End Time", styles["TableCell"]), Paragraph("<code>11:00</code>", styles["TableCell"]), Paragraph("Format: <code>HH:MM</code>. Must be chronologically after Start Time.", styles["TableCell"])],
    ]
    t_wiz = Table(wiz_table, colWidths=[55, 95, 100, 290])
    t_wiz.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_wiz)

    story.append(Spacer(1, 6))
    story.append(Paragraph("7.3 Device Schedule Synchronization (`GET /api/get_schedules`)", styles["H2"]))
    story.append(Paragraph(
        "• <b>Cloud Storage:</b> The FastAPI backend stores active schedules in <code>bot_data.db</code>.<br/>"
        "• <b>Device Pull:</b> When the terminal connects to Wi-Fi (either automatically every 6 hours or when an admin taps <b>[Sync Now]</b>), it queries: <code>GET /api/get_schedules?since=&lt;timestamp&gt;</code>.<br/>"
        "• <b>Automatic Session Activation:</b> When the terminal's internal clock matches a scheduled lecture window, the main screen automatically updates its header banner to the active course code (e.g., <code>ACTIVE SESSION: CSC 301</code>). All attendance scans during that window are tagged with that course session ID.<br/>"
        "• <b>Schedule Deletions:</b> If an instructor cancels a class via Telegram, the deletion is logged in <code>schedule_deletions</code> and pruned from the terminal during the next sync.",
        styles["Body"]
    ))

    story.append(Spacer(1, 8))
    story.append(make_callout(
        "info",
        "Ad-Hoc (Unscheduled) Classes",
        "If a lecture occurs outside scheduled timetables, the lecturer or administrator can tap <b>[Select Course]</b> directly on the terminal touch screen and pick any registered course from the drop-down list to open an immediate manual attendance session.",
        styles
    ))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 8: DAILY ATTENDANCE SCANNING OPERATIONS
    # =========================================================================
    story.append(Paragraph("Chapter 8: Daily Attendance Scanning Operations", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "During class sessions, the terminal operates autonomously in <b>Attendance Scanning Mode</b>. Students walk up to the device, look at the screen, and receive immediate visual confirmation.",
        styles["Body"]
    ))

    story.append(Paragraph("8.1 Screen Layout & Visual Feedback", styles["H2"]))
    story.append(Paragraph(
        "The 7.0″ IPS display is divided into three functional zones:",
        styles["Body"]
    ))
    zones = [
        "<b>Top Telemetry Bar:</b> Displays the institutional logo, current time (synchronized via SNTP/RTC), active course code (e.g., <code>CSC 301: Database Systems</code>), Wi-Fi connection indicator, and battery status with live percentage.",
        "<b>Center Live Camera Viewfinder:</b> Streams high-definition 30 fps video. Detected faces are tracked dynamically with a color-coded bounding reticle:<br/>"
        "• <font color='#10B981'><b>GREEN RETICLE:</b></font> Verified match! Displays student name, matric ID, and match confidence.<br/>"
        "• <font color='#F59E0B'><b>AMBER RETICLE:</b></font> Evaluating multi-frame consensus; user is prompted to hold still.<br/>"
        "• <font color='#EF4444'><b>RED RETICLE:</b></font> Unknown face or quality violation (excessive glare, wide open mouth, or motion blur).",
        "<b>Bottom Status Banner:</b> Displays total attendance counter for the active class (e.g., <code>Verified: 54 / 62 Students</code>) and real-time guidance prompts.",
    ]
    for z in zones:
        story.append(Paragraph(f"• {z}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("8.2 Sub-Second Verification & Debounce Logic", styles["H2"]))
    story.append(Paragraph(
        "• <b>Rapid Verification:</b> Complete face detection, landmark affine warp, MobileFaceNet inference, and database query execute in <b>under 1.0 second</b>.<br/>"
        "• <b>Early Exit Path:</b> If three consecutive frames yield unanimous match scores of S ≥ 0.80, the system triggers an immediate early-exit confirmation without waiting for the full 6-frame window.<br/>"
        "• <b>15-Minute Duplicate Debounce:</b> Once a student is marked present, subsequent scans by the same student within 15 minutes display: <code>\"Already Recorded: [Student Name]\"</code> without creating duplicate database entries.",
        styles["Body"]
    ))

    story.append(Spacer(1, 6))
    story.append(Paragraph("8.3 Environmental Guidance & Anti-Spoofing Filters", styles["H2"]))

    env_filters = [
        [Paragraph("<b>Quality Filter</b>", styles["TableHeader"]), Paragraph("<b>Threshold Trigger</b>", styles["TableHeader"]), Paragraph("<b>Visual Prompt & System Action</b>", styles["TableHeader"])],
        [
            Paragraph("Specular Glare Gate", styles["TableCellBold"]),
            Paragraph("Luminance Y > 240 on >15% pixels", styles["TableCell"]),
            Paragraph("Prompts: <code>\"Glare detected — adjust angle away from light\"</code>. Prevents flash/sunlight identity distortion.", styles["TableCell"]),
        ],
        [
            Paragraph("Mouth Opening / Jaw Gate", styles["TableCellBold"]),
            Paragraph("Mouth height / Eye dist > 0.85", styles["TableCell"]),
            Paragraph("Prompts: <code>\"Please close mouth & look straight\"</code>. Rejects laughing, talking, and yawning distortions.", styles["TableCell"]),
        ],
        [
            Paragraph("Motion Sharpness Gate", styles["TableCellBold"]),
            Paragraph("Laplacian variance < 5.0", styles["TableCell"]),
            Paragraph("Prompts: <code>\"Blurry image — please hold still\"</code>. Prevents blurred edge matching.", styles["TableCell"]),
        ],
        [
            Paragraph("Extreme Head Pose", styles["TableCellBold"]),
            Paragraph("Yaw angle |θ| > 35.0°", styles["TableCell"]),
            Paragraph("Prompts: <code>\"Please face the camera directly\"</code>. Rejects profile angles.", styles["TableCell"]),
        ],
    ]
    t_env = Table(env_filters, colWidths=[120, 140, 280])
    t_env.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_env)

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 9 & 10: USER MANAGEMENT, REPORTS & CSV EXPORT
    # =========================================================================
    story.append(Paragraph("Chapter 9: User Database & Roster Administration", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "Authorized administrators manage user profiles via the on-device <b>User Management Module</b>:",
        styles["Body"]
    ))

    user_admin_points = [
        "<b>Roster Browser:</b> Displays a searchable card list of all registered students, lecturers, and administrators. Each card summarizes Full Name, Matric / Staff ID, Role tag, Registration Date, and Enrollment Quality Score.",
        "<b>Safe Re-Enrollment Protocol:</b> If a student changes their appearance (e.g., new glasses, beard, or facial modification) and recognition confidence drops, select their profile and tap <b>[Re-enroll Face]</b>. Firmware v1.1.4+ executes a <b>non-destructive in-place update</b>: it captures a new 30-frame template and updates the facial vector in SQLite without modifying their user ID, historical attendance records, or Telegram linkages.",
        "<b>User Deletion & Archive:</b> Tapping <b>[Delete User]</b> requires Master PIN confirmation. Deleting a user removes their biometric template to free memory while preserving past attendance logs under their original matric ID for historical audit compliance.",
    ]
    for uap in user_admin_points:
        story.append(Paragraph(f"• {uap}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 10: Attendance Records, Reports & CSV Export", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "The terminal maintains immutable audit trails stored locally in SQLite (`/sdcard/attendance.db`) and exports structured CSV reports for university administrative systems.",
        styles["Body"]
    ))

    story.append(Paragraph("10.1 On-Device Reports Browser", styles["H2"]))
    story.append(Paragraph(
        "Open <b>Attendance Reports</b> from the main menu (requires Master PIN). Administrators and lecturers can filter records by <b>Date</b> or <b>Course Code</b>. The interface presents total enrolled count, verified present count, absent count, and class attendance percentage.",
        styles["Body"]
    ))

    story.append(Spacer(1, 6))
    story.append(Paragraph("10.2 Exporting CSV Reports to MicroSD Card", styles["H2"]))
    story.append(Paragraph(
        "Tap the <b>[Export CSV to SD]</b> button. The terminal generates a standard UTF-8 Comma-Separated Values file at <code>/sdcard/reports/attendance_YYYYMMDD.csv</code> with the following schema:",
        styles["Body"]
    ))

    csv_sample = (
        "Session_ID,Date,Time,Course_Code,Course_Title,Student_ID,Full_Name,Role,Confidence,Status\n"
        "1042,2026-10-21,09:02:14,CSC301,Database Systems,CSC/2022/1045,Adesare Adegbagi,Student,0.842,PRESENT\n"
        "1042,2026-10-21,09:03:01,CSC301,Database Systems,CSC/2022/1088,Solomon Israel,Student,0.811,PRESENT\n"
        "1042,2026-10-21,09:04:22,CSC301,Database Systems,CSC/2022/1102,Fatima Bello,Student,0.875,PRESENT\n"
        "1042,2026-10-21,09:05:40,CSC301,Database Systems,CSC/2022/1150,Chukwuma Obi,Student,0.829,PRESENT"
    )
    t_csv = Table([[Paragraph(f"<font face='Courier' size='7'>{csv_sample.replace(chr(10), '<br/>')}</font>", styles["TableCell"])]], colWidths=[USABLE_WIDTH])
    t_csv.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 6),
    ]))
    story.append(t_csv)

    story.append(Spacer(1, 8))
    story.append(Paragraph("10.3 Cloud Push Synchronization (`POST /api/sync_attendance`)", styles["H2"]))
    story.append(Paragraph(
        "When the device connects to Wi-Fi, it pushes all un-synced attendance events to the institutional backend via <code>POST /api/sync_attendance</code>. Once acknowledged, the local SQLite database marks the records as synchronized (<code>synced = 1</code>).",
        styles["Body"]
    ))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 11 & 12: TELEGRAM BOT, SETTINGS & OTA UPDATES
    # =========================================================================
    story.append(Paragraph("Chapter 11: Dual Telegram Bot Ecosystem", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "The Telegram integration provides a zero-maintenance cloud bridge serving students, lecturers, and system administrators.",
        styles["Body"]
    ))

    tg_features = [
        "<b>Student Push Notifications:</b> When a student is verified at the terminal, the cloud bot instantly dispatches a personal Telegram notification: <i>'Hello Adesare! Your attendance has been recorded for CSC 301 (Database Systems) at 09:02 AM in Lab Block A.'</i>",
        "<b>Lecturer Real-Time Class Summaries:</b> Upon class conclusion, the bot compiles an automated summary message for the lecturer, detailing attendance percentage and attaching the complete CSV roll sheet.",
        "<b>Administrative Remote Control Commands:</b> Restricted strictly to authorized administrator Telegram chat IDs:",
    ]
    for tf in tg_features:
        story.append(Paragraph(f"• {tf}", styles["Bullet"]))

    bot_cmds = [
        [Paragraph("<b>Command</b>", styles["TableHeader"]), Paragraph("<b>Parameters</b>", styles["TableHeader"]), Paragraph("<b>Description & Operational Response</b>", styles["TableHeader"])],
        [Paragraph("<code>/status</code>", styles["TableCellBold"]), Paragraph("None", styles["TableCell"]), Paragraph("Returns terminal uptime, CPU temperature, free PSRAM/heap, active Wi-Fi SSID, and enrolled user count.", styles["TableCell"])],
        [Paragraph("<code>/sync</code>", styles["TableCellBold"]), Paragraph("None", styles["TableCell"]), Paragraph("Commands terminal to connect to Wi-Fi, upload pending attendance records, and pull new schedules.", styles["TableCell"])],
        [Paragraph("<code>/export</code>", styles["TableCellBold"]), Paragraph("<code>[course] [date]</code>", styles["TableCell"]), Paragraph("Generates and directly uploads the attendance CSV document into the admin Telegram chat.", styles["TableCell"])],
        [Paragraph("<code>/roster</code>", styles["TableCellBold"]), Paragraph("<code>[course_code]</code>", styles["TableCell"]), Paragraph("Lists enrolled students for the specified course and displays semester attendance percentages.", styles["TableCell"])],
        [Paragraph("<code>/ota</code>", styles["TableCellBold"]), Paragraph("<code>[firmware_url]</code>", styles["TableCell"]), Paragraph("Triggers Over-The-Air firmware upgrade; terminal downloads, validates SHA256, flashes, and reboots.", styles["TableCell"])],
        [Paragraph("<code>/reboot</code>", styles["TableCellBold"]), Paragraph("None", styles["TableCell"]), Paragraph("Executes a graceful software restart of the SmartAttendance hardware terminal.", styles["TableCell"])],
    ]
    t_cmds = Table(bot_cmds, colWidths=[75, 95, 370])
    t_cmds.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_cmds)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 12: System Settings, Maintenance & OTA Updates", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "The <b>System Settings</b> screen (Master PIN required) provides access to hardware tuning and lifecycle maintenance:",
        styles["Body"]
    ))

    sett_bullets = [
        "<b>Display Backlight & Dimming:</b> Adjust screen brightness from 10% to 100% via the verified 30 kHz PWM driver on GPIO31. Enable <b>Auto-Dimming</b> (1 to 15 minutes) to conserve battery during unattended periods.",
        "<b>Wi-Fi Network Configuration:</b> Scans for 2.4 GHz access points, displays live RSSI signal strength meters, and saves credentials in encrypted ESP-IDF NVS storage.",
        "<b>Dual-Bank Over-The-Air (OTA) Updates:</b> The terminal features two 6 MB application partitions (<code>ota_0</code> and <code>ota_1</code>). Firmware updates can be applied via the Local Web Portal (<code>http://192.168.4.1/update</code>) or remotely via the Telegram <code>/ota</code> command. If an update fails CRC verification, the bootloader automatically rolls back to the stable firmware bank.",
        "<b>Database Backup & Disaster Recovery:</b> The terminal automatically maintains a backup at <code>/sdcard/attendance_backup.db</code>. If hardware suffers catastrophic physical damage, removing the MicroSD card and inserting it into a replacement CrowPanel ESP32-P4 board restores 100% of user profiles and history instantly.",
    ]
    for sb in sett_bullets:
        story.append(Paragraph(f"• {sb}", styles["Bullet"]))

    story.append(PageBreak())

    # =========================================================================
    # CHAPTER 13 & 14: TROUBLESHOOTING & SPECIFICATIONS
    # =========================================================================
    story.append(Paragraph("Chapter 13: Troubleshooting & Fault Diagnostics", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    story.append(Paragraph(
        "Consult this diagnostic troubleshooting matrix compiled from hands-on embedded test engineering:",
        styles["Body"]
    ))

    trouble_data = [
        [Paragraph("<b>Observed Fault / Symptom</b>", styles["TableHeader"]), Paragraph("<b>Probable Root Cause</b>", styles["TableHeader"]), Paragraph("<b>Actionable Resolution Procedure</b>", styles["TableHeader"])],
        [
            Paragraph("Screen dark; no power LED", styles["TableCellBold"]),
            Paragraph("Inadequate power source or tripped battery PCM.", styles["TableCell"]),
            Paragraph("Connect a certified 5V 3A USB-C power supply. Do not power from 500mA PC USB ports. Power LED must illuminate blue.", styles["TableCell"]),
        ],
        [
            Paragraph("Touchscreen unresponsive", styles["TableCellBold"]),
            Paragraph("GT911 I2C address conflict or missing RST/INT pulse.", styles["TableCell"]),
            Paragraph("Power cycle terminal. Ensure no conductive metal touches display bezel. Re-flash firmware with verified GT911 0x5D init sequence.", styles["TableCell"]),
        ],
        [
            Paragraph("Camera shows black preview or init fails", styles["TableCellBold"]),
            Paragraph("24-pin MIPI-CSI ribbon unseated or wrong SCCB address.", styles["TableCell"]),
            Paragraph("Power down device, unlatch 24-pin FPC connector, confirm gold pins face PCB, firmly reseat cable, and snap connector down.", styles["TableCell"]),
        ],
        [
            Paragraph("'SD Card Mount Failed' banner", styles["TableCellBold"]),
            Paragraph("Card unformatted, corrupted FAT32, or missing models.", styles["TableCell"]),
            Paragraph("Reformat MicroSD on PC as FAT32 (32KB clusters). Confirm <code>/models/p4/</code> contains all three <code>.espdl</code> neural models.", styles["TableCell"]),
        ],
        [
            Paragraph("Frequent 'Unknown Face' rejections", styles["TableCellBold"]),
            Paragraph("Severe backlight glare, dim lighting, or poor template.", styles["TableCell"]),
            Paragraph("Reposition device away from direct sunlight. Check student score in User Manager; if rating is POOR (<55), perform a re-enrollment.", styles["TableCell"]),
        ],
        [
            Paragraph("Rapid battery drain (< 2 hours)", styles["TableCellBold"]),
            Paragraph("Backlight at 100% constant or degraded battery cell.", styles["TableCell"]),
            Paragraph("Set backlight to 60% and enable 2-minute auto-dim timeout. If issue persists, replace cell with a genuine 3.7V LiPo pack.", styles["TableCell"]),
        ],
        [
            Paragraph("Telegram notifications not delivered", styles["TableCellBold"]),
            Paragraph("Wi-Fi offline or phone number not linked in bot.", styles["TableCell"]),
            Paragraph("Check Wi-Fi icon on status bar. Have student verify contact card in <code>@MyUniAttendance_Bot</code> to link their Telegram ID.", styles["TableCell"]),
        ],
    ]
    t_trouble = Table(trouble_data, colWidths=[120, 140, 280])
    t_trouble.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_trouble)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 14: Technical Specifications & Safety Warnings", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    specs_final = [
        [Paragraph("<b>Electrical & Environmental Parameter</b>", styles["TableHeader"]), Paragraph("<b>Official Specification / Operating Boundary</b>", styles["TableHeader"])],
        [Paragraph("Input Operating Voltage", styles["TableCellBold"]), Paragraph("5.0 V DC (± 5%) via USB Type-C or screw terminal block", styles["TableCell"])],
        [Paragraph("Operating Current Consumption", styles["TableCellBold"]), Paragraph("Nominal: 1.1 A to 1.6 A; Peak Burst: 2.2 A", styles["TableCell"])],
        [Paragraph("Operating Ambient Temperature", styles["TableCellBold"]), Paragraph("0°C to +50°C (32°F to 122°F) non-condensing", styles["TableCell"])],
        [Paragraph("Storage Temperature", styles["TableCellBold"]), Paragraph("-20°C to +70°C (-4°F to 158°F)", styles["TableCell"])],
        [Paragraph("Relative Humidity Limits", styles["TableCellBold"]), Paragraph("10% to 90% non-condensing", styles["TableCell"])],
        [Paragraph("Display Viewing Angle", styles["TableCellBold"]), Paragraph("178° Horizontal / 178° Vertical full IPS viewing angle", styles["TableCell"])],
        [Paragraph("Verification Distance Range", styles["TableCellBold"]), Paragraph("Optimal: 0.5 m to 0.9 m; Usable: 0.3 m to 1.4 m", styles["TableCell"])],
        [Paragraph("Biometric False Acceptance Rate (FAR)", styles["TableCellBold"]), Paragraph("< 0.001% (at default similarity threshold 0.70 with dynamic margin)", styles["TableCell"])],
        [Paragraph("Biometric False Rejection Rate (FRR)", styles["TableCellBold"]), Paragraph("< 1.5% (under recommended indoor ambient lighting 150 - 500 lux)", styles["TableCell"])],
    ]
    t_specs = Table(specs_final, colWidths=[190, 350])
    t_specs.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_specs)

    story.append(Spacer(1, 8))
    story.append(Paragraph("14.2 Physical Installation & Maintenance Precautions", styles["H2"]))
    maint_notes = [
        "<b>Mounting Height:</b> For standard standing operation, mount terminal centerline at <b>1.45 to 1.55 meters</b> from the floor.",
        "<b>Cleaning:</b> Clean the 7.0″ glass capacitive touch panel with a soft microfiber cloth lightly moistened with optical screen cleaner. Never apply alcohol, solvents, or abrasive powders.",
        "<b>Thermal Clearance:</b> Ensure the rear of the CrowPanel enclosure has at least 15mm convective air clearance.",
    ]
    for mn in maint_notes:
        story.append(Paragraph(f"• {mn}", styles["Bullet"]))

    story.append(Spacer(1, 12))
    story.append(HRFlowable(width="100%", thickness=1, color=C_SECONDARY, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<font size='7.5' color='#64748B'>SmartAttendance™ is an open biometric IoT solution. ESP32-P4 is a trademark of Espressif Systems Co., Ltd. Elecrow CrowPanel is a trademark of Elecrow Technology Inc. All other trademarks belong to their respective holders. Copyright © 2026 SmartAttendance Project Team. All rights reserved.</font>",
        styles["Body"]
    ))

    return story


def generate_pdf(output_path):
    """Compiles the document story and writes the PDF."""
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    doc = SimpleDocTemplate(
        output_path,
        pagesize=letter,
        leftMargin=MARGIN,
        rightMargin=MARGIN,
        topMargin=MARGIN,
        bottomMargin=MARGIN,
    )
    story = build_manual_story()
    print(f"Building Comprehensive PDF at: {output_path} ...")
    doc.build(story, canvasmaker=NumberedCanvas)
    print("Comprehensive PDF build completed successfully.")


if __name__ == "__main__":
    out_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "SmartAttendance_User_Manual.pdf"
    )
    generate_pdf(out_file)
