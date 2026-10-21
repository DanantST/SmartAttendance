"""
SmartAttendance Admin Operator & User Manual Generator (Layman Edition).
Formatted in Times New Roman typography with 12pt body text.
Exclusively covers practical operational procedures, administrative workflows, and user guidance:
- Device hardware controls:
  * Slide power switch on the left side (slide UP to power ON, DOWN to power OFF)
  * USB Type-C charging slot on left side below power switch (5V 2A-3A recommended)
  * MicroSD memory card on the BOTTOM slot (CRITICAL: DO NOT REMOVE!)
  * Camera lens at the TOP of the screen
- Powering on, battery color indicators:
  * Cyan Blue (#00CFFF) with lightning bolt when charging
  * White / Black during normal operation (15% - 100%)
  * Vivid Red (#FF4444) when battery is low (< 15%)
  * Automatic protective shutdown at 5% (saves all data safely)
  * No green battery color, no bar meter
- Contactless facial verification with 100% visual feedback:
  * Green reticle & green bottom banner on success (no speaker / no audio)
  * Orange banner for duplicate scans (15-minute debounce)
  * Red text for unrecognized faces
  * 5 quick Camera Profile buttons (Auto, Outdoor, Indoor, Bright, Low Light)
- Initial Setup Wizard (Setting Admin PIN, Department info, Date/Time, Wi-Fi)
- Enrolling students & staff (On-device touch screen and Smartphone Wi-Fi portal)
- Managing registered users, updating face scans for new looks, and student search
- Scheduling classes via the Telegram Bot (@MyUniAttendance_Bot)
- Viewing attendance records and exporting reports to Excel CSV
- Screen brightness, light/dark theme switch, and Wi-Fi connection
- Everyday troubleshooting, screen cleaning, and FAQs
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

# Clean corporate palette
C_PRIMARY = colors.HexColor("#1E3A8A")    # Classic Navy
C_SECONDARY = colors.HexColor("#0284C7")  # Technical Blue
C_DARK = colors.HexColor("#0F172A")       # Soft Charcoal
C_BODY = colors.HexColor("#1E293B")       # Dark Slate Body
C_MUTED = colors.HexColor("#64748B")      # Muted Slate
C_BG_LIGHT = colors.HexColor("#F8FAFC")   # Light Background
C_BORDER = colors.HexColor("#CBD5E1")     # Border
C_CARD_BG = colors.HexColor("#F1F5F9")    # Card Background

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
MARGIN = 36  # 0.5 in margins = 540 pt usable width
USABLE_WIDTH = PAGE_WIDTH - (2 * MARGIN)


class OperatorNumberedCanvas(canvas.Canvas):
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
        header_text = "SmartAttendance™ Biometric Terminal — Administrator & Operator Guide"
        self.drawString(MARGIN, PAGE_HEIGHT - 25, header_text)
        self.setStrokeColor(C_BORDER)
        self.setLineWidth(0.5)
        self.line(MARGIN, PAGE_HEIGHT - 28, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - 28)

        # Footer
        footer_left = "Daily Usage, Enrollment, Scheduling, Reports & Battery Management"
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
            fontSize=14, leading=19, textColor=C_DARK, spaceAfter=14,
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
            fontSize=17, leading=22, textColor=C_PRIMARY, spaceBefore=12, spaceAfter=6, keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2", parent=base["Heading2"], fontName=F_BOLD,
            fontSize=13, leading=17, textColor=C_SECONDARY, spaceBefore=9, spaceAfter=4, keepWithNext=True,
        ),
        "H3": ParagraphStyle(
            "H3", parent=base["Heading3"], fontName=F_BOLD,
            fontSize=11.5, leading=15, textColor=C_DARK, spaceBefore=7, spaceAfter=3, keepWithNext=True,
        ),
        # Requested: Font size 12 for the body text in Times New Roman
        "Body": ParagraphStyle(
            "Body", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, spaceAfter=5,
        ),
        "Bullet": ParagraphStyle(
            "Bullet", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=12, leading=16, textColor=C_BODY, leftIndent=14, firstLineIndent=-10, spaceAfter=3.5,
        ),
        "TableHeader": ParagraphStyle(
            "TableHeader", parent=base["Normal"], fontName=F_BOLD,
            fontSize=9.5, leading=13, textColor=colors.white,
        ),
        "TableCell": ParagraphStyle(
            "TableCell", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=9, leading=12.5, textColor=C_BODY,
        ),
        "TableCellBold": ParagraphStyle(
            "TableCellBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=9, leading=12.5, textColor=C_DARK,
        ),
        "CalloutBold": ParagraphStyle(
            "CalloutBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=11, leading=15, textColor=C_PRIMARY,
        ),
        "CalloutText": ParagraphStyle(
            "CalloutText", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=10.5, leading=14.5, textColor=C_DARK,
        ),
        "TOCItem": ParagraphStyle(
            "TOCItem", parent=base["Normal"], fontName=F_NORMAL,
            fontSize=10, leading=14, textColor=C_DARK,
        ),
        "TOCItemBold": ParagraphStyle(
            "TOCItemBold", parent=base["Normal"], fontName=F_BOLD,
            fontSize=10.5, leading=14, textColor=C_PRIMARY,
        ),
    }


def make_callout(box_type, title, content, styles):
    type_map = {
        "info": (C_INFO_BORDER, C_INFO_BG, "[i] HELPFUL NOTE:"),
        "tip": (C_SUCCESS_BORDER, C_SUCCESS_BG, "[v] BEST PRACTICE / TIP:"),
        "warn": (C_WARN_BORDER, C_WARN_BG, "[!] IMPORTANT:"),
        "danger": (C_DANGER_BORDER, C_DANGER_BG, "[X] CAUTION:"),
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
        ("PADDING", (0, 0), (-1, -1), 6),
    ]))
    return t


def build_operator_story():
    styles = get_styles()
    story = []

    # COVER PAGE
    story.append(Spacer(1, 15))
    story.append(Paragraph("ADMINISTRATOR & OPERATOR USER GUIDE", styles["CoverTag"]))
    story.append(Paragraph("SmartAttendance™ Touchscreen Terminal", styles["CoverTitle"]))
    story.append(Paragraph(
        "Complete Step-by-Step Instructions for Classroom Attendance, Student Registration, "
        "Lecture Scheduling via Telegram, Battery Charging, and Report Management",
        styles["CoverSubtitle"]
    ))

    story.append(HRFlowable(width="100%", thickness=2.5, color=C_SECONDARY, spaceBefore=2, spaceAfter=14))

    highlights = [
        "<b>Simple Touchscreen Operation:</b> 7-inch color display designed for intuitive everyday classroom use.",
        "<b>Fast & Hygienic Attendance:</b> Sub-second contactless facial verification with on-screen visual confirmation.",
        "<b>Works Anywhere:</b> Operates 100% offline without internet; optional Wi-Fi syncs records seamlessly.",
        "<b>Quick Student Registration:</b> Register students directly on screen or from your smartphone via Wi-Fi.",
        "<b>Automated Class Scheduling:</b> Lecturers schedule classes easily through the official Telegram Bot.",
        "<b>Portable Battery Power:</b> Built-in rechargeable battery provides 4 to 6 hours of continuous scanning.",
        "<b>Instant Reports:</b> Export attendance directly to an Excel-compatible CSV file on the memory card.",
        "<b>Telegram Push Alerts:</b> Students receive an immediate confirmation message on their phones when marked present.",
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
            Paragraph("Course Lecturers, Department Officers, School Admins", styles["CoverMeta"]),
            Paragraph("<b>Edition:</b>", styles["CoverMetaBold"]),
            Paragraph("Non-Technical / Layman Operator Guide", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Device Model:</b>", styles["CoverMetaBold"]),
            Paragraph("SmartAttendance 7.0″ Touch Terminal", styles["CoverMeta"]),
            Paragraph("<b>Telegram Bot:</b>", styles["CoverMetaBold"]),
            Paragraph("@MyUniAttendance_Bot", styles["CoverMeta"]),
        ],
        [
            Paragraph("<b>Typography:</b>", styles["CoverMetaBold"]),
            Paragraph("Times New Roman (12pt Body)", styles["CoverMeta"]),
            Paragraph("<b>Publication Date:</b>", styles["CoverMetaBold"]),
            Paragraph("October 2026", styles["CoverMeta"]),
        ],
    ]
    t_meta = Table(meta, colWidths=[110, 160, 120, 150])
    t_meta.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("PADDING", (0, 0), (-1, -1), 3),
    ]))
    story.append(t_meta)
    story.append(Spacer(1, 14))

    story.append(make_callout(
        "tip",
        "About This Manual",
        "This guide is written specifically for day-to-day operators. It explains how to operate the slide switch, check battery colors, charge the device, enroll students, run attendance sessions, schedule classes, and manage reports. For circuit schematics and developer flashing guides, refer to the separate Technician Manual.",
        styles
    ))

    story.append(PageBreak())

    # TABLE OF CONTENTS
    story.append(Paragraph("Table of Contents", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1.5, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    chapters = [
        ("Chapter 1: Getting to Know Your Terminal", "Exterior controls, slide power switch, USB-C slot, camera lens, memory card slot"),
        ("Chapter 2: Powering On, Battery Levels & Charging", "Slide switch operation, battery color indicators (cyan/white/red), 5V 2-3A charging"),
        ("Chapter 3: First-Time Setup Wizard", "Setting your 4-digit Admin PIN, department name, clock, and Wi-Fi connection"),
        ("Chapter 4: Daily Attendance Scanning in the Classroom", "Taking attendance, visual feedback banner, reticle colors, camera profiles (Outdoor/Indoor)"),
        ("Chapter 5: Enrolling New Students & Staff", "On-screen registration, smartphone Wi-Fi portal, lighting tips for best quality"),
        ("Chapter 6: Managing Registered Students", "Searching student roster, updating face scans for new looks, removing users"),
        ("Chapter 7: Scheduling Lectures via Telegram", "Using the @MyUniAttendance_Bot 5-step wizard to schedule classes automatically"),
        ("Chapter 8: Viewing & Exporting Attendance Reports", "Checking today's roll on screen, saving Excel-compatible CSV files to memory card"),
        ("Chapter 9: System Settings & Personalization", "Adjusting screen brightness, light/dark theme switch, Wi-Fi passwords, changing PIN"),
        ("Chapter 10: Simple Troubleshooting & FAQs", "Common questions, what to do if a face is not recognized, cleaning the screen"),
    ]

    t_toc = []
    for c_title, c_desc in chapters:
        t_toc.append([
            Paragraph(f"<b>{c_title}</b>", styles["TOCItemBold"]),
            Paragraph(f"<font color='{C_MUTED.hexval()}'>{c_desc}</font>", styles["TOCItem"]),
        ])
    t_table = Table(t_toc, colWidths=[205, 335])
    t_table.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("PADDING", (0, 0), (-1, -1), 4),
        ("LINEBELOW", (0, 0), (-1, -1), 0.5, C_BG_LIGHT),
    ]))
    story.append(t_table)
    story.append(Spacer(1, 10))

    story.append(Paragraph("Quick Overview: How the System Works", styles["H2"]))
    story.append(Paragraph(
        "The SmartAttendance terminal is an all-in-one smart device designed to make taking attendance fast, accurate, and completely paperless. Here is how simple the daily routine is:",
        styles["Body"]
    ))
    daily_overview = [
        "<b>1. Bring to Class:</b> Place the device on the lecturer's desk or mount it by the classroom entrance.",
        "<b>2. Power On:</b> Slide the power switch on the left side <b>UP</b>. The device boots in about 5 seconds.",
        "<b>3. Automatic Session:</b> If the class was scheduled via Telegram, the device automatically shows the active course name on screen. If not, tap the screen to select your course.",
        "<b>4. Students Walk By:</b> Students pause for just one second in front of the screen. A green box and green visual confirmation banner appear confirming their name.",
        "<b>5. Instant Records:</b> Attendance is saved immediately into the memory card. Students receive a Telegram alert on their phones, and the lecturer gets a complete attendance sheet after class.",
    ]
    for d in daily_overview:
        story.append(Paragraph(f"• {d}", styles["Bullet"]))

    story.append(PageBreak())

    # CHAPTER 1 & 2: DEVICE PARTS, POWER & CHARGING
    story.append(Paragraph("Chapter 1: Getting to Know Your Terminal", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Familiarize yourself with the exterior parts and verified physical controls of the terminal:",
        styles["Body"]
    ))

    parts = [
        [Paragraph("<b>Component</b>", styles["TableHeader"]), Paragraph("<b>Location</b>", styles["TableHeader"]), Paragraph("<b>Operational Function & Instructions</b>", styles["TableHeader"])],
        [
            Paragraph("Power Slide Switch", styles["TableCellBold"]),
            Paragraph("Left Side (Upper)", styles["TableCell"]),
            Paragraph("Mechanical slide switch: <b>Slide UP to power ON</b>. <b>Slide DOWN to power OFF</b>. Provides instant hardware power control.", styles["TableCell"]),
        ],
        [
            Paragraph("USB-C Charging Slot", styles["TableCellBold"]),
            Paragraph("Left Side (Below Switch)", styles["TableCell"]),
            Paragraph("Standard USB Type-C port for charging and power. <b>Recommended power rating: 5V, 2A to 3A</b>. Compatible with standard phone fast chargers.", styles["TableCell"]),
        ],
        [
            Paragraph("Camera Lens", styles["TableCellBold"]),
            Paragraph("Top of Screen", styles["TableCell"]),
            Paragraph("Integrated high-definition camera facing outward toward students. Keep the lens clean and free of fingerprints.", styles["TableCell"]),
        ],
        [
            Paragraph("7.0″ Touchscreen", styles["TableCellBold"]),
            Paragraph("Front Face", styles["TableCell"]),
            Paragraph("Crisp 7-inch color display. Shows live camera view, visual confirmation banner, camera profiles, clock, and battery level.", styles["TableCell"]),
        ],
        [
            Paragraph("Memory Card Slot", styles["TableCellBold"]),
            Paragraph("Bottom Slot", styles["TableCell"]),
            Paragraph("Houses the MicroSD card holding student records, attendance logs, and AI models. <font color='#EF4444'><b>CRITICAL: DO NOT REMOVE!</b></font> Removing the card halts attendance functions.", styles["TableCell"]),
        ],
        [
            Paragraph("Status Bar", styles["TableCellBold"]),
            Paragraph("Top Edge of Display", styles["TableCell"]),
            Paragraph("Displays current time, active course code, Wi-Fi connection indicator, and battery percentage with charging status.", styles["TableCell"]),
        ],
    ]
    t_parts = Table(parts, colWidths=[120, 110, 310])
    t_parts.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 4),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_parts)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 2: Powering On, Battery Levels & Charging", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))

    power_steps = [
        "<b>Turning On:</b> Slide the switch on the left side <b>UP</b>. The screen lights up immediately and loads the main scanner in ~5 seconds.",
        "<b>Turning Off:</b> Slide the switch on the left side <b>DOWN</b> to completely power off the unit.",
        "<b>Charging the Device:</b> Plug a certified <b>5V 2A–3A USB Type-C charger</b> into the charging slot on the left side (located right below the power switch). A full charge takes approximately <b>2.5 to 3.5 hours</b>.",
        "<b>Continuous Power:</b> You can keep the USB-C charger connected during long lecture days. The device supports pass-through charging without battery degradation.",
    ]
    for ps in power_steps:
        story.append(Paragraph(f"• {ps}", styles["Bullet"]))

    story.append(Spacer(1, 4))
    story.append(Paragraph("Understanding the Battery Color Indicators (No Bars / No Green)", styles["H2"]))
    story.append(Paragraph(
        "The battery status in the top-right corner uses clean <b>numerical percentages and color indicators</b> (no multi-colored bars, and no green indicator):",
        styles["Body"]
    ))

    batt_table = [
        [Paragraph("<b>Status / Mode</b>", styles["TableHeader"]), Paragraph("<b>Screen Display</b>", styles["TableHeader"]), Paragraph("<b>Color Indicator</b>", styles["TableHeader"]), Paragraph("<b>What It Means & What You Should Do</b>", styles["TableHeader"])],
        [
            Paragraph("Charging Connected", styles["TableCellBold"]),
            Paragraph("Lightning Bolt Icon + Percentage (e.g. <code>[bolt] 74%</code>)", styles["TableCell"]),
            Paragraph("<font color='#00CFFF'><b>Glowing Cyan Blue (#00CFFF)</b></font>", styles["TableCell"]),
            Paragraph("The device is actively charging from the USB-C slot. Both the lightning bolt icon and percentage text glow in bright cyan blue.", styles["TableCell"]),
        ],
        [
            Paragraph("Normal Battery (15% – 100%)", styles["TableCellBold"]),
            Paragraph("Battery Icon + Percentage (e.g. <code>88%</code>)", styles["TableCell"]),
            Paragraph("<b>White</b> (Dark Theme) / <b>Black</b> (Light Theme)", styles["TableCell"]),
            Paragraph("Standard discharging mode. Battery is healthy and ready for active attendance scanning. (Lasts ~4 to 6 hours continuous).", styles["TableCell"]),
        ],
        [
            Paragraph("Low Battery (< 15%)", styles["TableCellBold"]),
            Paragraph("Battery Icon + Percentage (e.g. <code>12%</code>)", styles["TableCell"]),
            Paragraph("<font color='#FF4444'><b>Vivid Red (#FF4444)</b></font>", styles["TableCell"]),
            Paragraph("Both the icon and percentage turn solid red. A notification banner warns that the battery is low. Plug in the USB-C charger soon.", styles["TableCell"]),
        ],
        [
            Paragraph("Critical Cutoff (5%)", styles["TableCellBold"]),
            Paragraph("Flashing Red Alert", styles["TableCell"]),
            Paragraph("<font color='#FF4444'><b>Red / Auto-Protect</b></font>", styles["TableCell"]),
            Paragraph("<b>At 5%, the terminal automatically saves all attendance records to the memory card and safely shuts down</b> to prevent data loss.", styles["TableCell"]),
        ],
    ]
    t_batt = Table(batt_table, colWidths=[95, 120, 110, 215])
    t_batt.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_batt)

    story.append(PageBreak())

    # CHAPTER 3 & 4: SETUP WIZARD & DAILY ATTENDANCE
    story.append(Paragraph("Chapter 3: First-Time Setup Wizard", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "When you power on a fresh terminal, a simple 4-step wizard welcomes you:",
        styles["Body"]
    ))

    wizard_steps = [
        "<b>Step 1: Set Your Master Admin PIN:</b> Choose a secure <b>4-digit PIN</b> (default initial code is <code>1234</code>). This PIN protects student management, reports, and settings from unauthorized tampering. Do not share this PIN with students.",
        "<b>Step 2: Enter School / Department Details:</b> Type your Department Name (e.g., <i>Computer Science</i>) and Terminal Location (e.g., <i>Lecture Hall B</i>). These names appear on all exported attendance reports.",
        "<b>Step 3: Set Date & Time:</b> Confirm the current time and date. If you connect to Wi-Fi, the device sets its clock automatically over the internet.",
        "<b>Step 4: Connect to Campus Wi-Fi (Optional):</b> Select your Wi-Fi network and type the password. If your classroom does not have Wi-Fi, tap <b>[Skip]</b> — the device works 100% offline!",
    ]
    for ws in wizard_steps:
        story.append(Paragraph(f"• {ws}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 4: Daily Attendance Scanning in the Classroom", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "During class, the device runs in automatic scanning mode. Taking attendance is completely contactless and visual (the system does not use audio or sound chimes):",
        styles["Body"]
    ))

    scan_steps = [
        "<b>1. Student Positioning:</b> The student approaches the terminal and pauses about <b>2 to 3 feet</b> (arm's length) in front of the screen, looking toward the camera lens at the top of the display.",
        "<b>2. Sub-Second Facial Verification:</b> The device tracks the face with a green reticle box and verifies their identity in under 1 second.",
        "<b>3. Understanding Visual Screen Confirmations:</b><br/>"
        "• <font color='#10B981'><b>GREEN RETICLE & GREEN BANNER:</b></font> <b>Success!</b> The bottom feedback banner turns deep green with: <code>\"Welcome, [Student Name]! Recorded at 09:15 AM\"</code>.<br/>"
        "• <font color='#F59E0B'><b>ORANGE BANNER:</b></font> <b>Duplicate Scan:</b> Shows <code>\"Already Recorded: [Student Name]\"</code> if scanned again within 15 minutes.<br/>"
        "• <font color='#EF4444'><b>RED TEXT ('Unknown'):</b></font> The face is not registered in the system, or the student needs to step closer.",
        "<b>4. Helpful Guidance Prompts on Screen:</b><br/>"
        "• <i>\"Please close mouth & look straight\"</i> — student is laughing, talking, or yawning.<br/>"
        "• <i>\"Glare detected — adjust angle from light\"</i> — sunlight or a bright lamp is reflecting into the lens.<br/>"
        "• <i>\"Blurry image — please hold still\"</i> — student is walking past too quickly.",
        "<b>5. Camera Profile Buttons (Right Panel):</b> If classroom lighting changes, tap any of the 5 quick profile buttons on the right side of the screen: <b>Auto</b>, <b>Outdoor</b>, <b>Indoor</b>, <b>Bright</b>, or <b>Low Light</b> for instant exposure optimization.",
    ]
    for ss in scan_steps:
        story.append(Paragraph(f"• {ss}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(make_callout(
        "tip",
        "Selecting an Unscheduled Lecture",
        "If an extra or unscheduled class takes place, tap <b>[Select Course]</b> on the top banner, enter your 4-digit Admin PIN, and pick the course from the drop-down list. All scans will immediately be filed under that course.",
        styles
    ))

    story.append(PageBreak())

    # CHAPTER 5: ENROLLING STUDENTS & STAFF
    story.append(Paragraph("Chapter 5: Enrolling New Students & Staff", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Registration only needs to be done once per student. You can enroll students directly on the touch screen, or use your phone for fast queueing during orientation:",
        styles["Body"]
    ))

    story.append(Paragraph("Method 1: Directly on the Terminal Touchscreen", styles["H2"]))
    method1 = [
        "From the main screen, tap the top menu icon and select <b>User Management</b>.",
        "Enter your <b>4-digit Master Admin PIN</b>.",
        "Tap the green <b>[+ Add User]</b> button.",
        "Type the student's <b>Full Name</b>, <b>Matriculation / Student ID</b> (e.g., <code>CSC/2022/1045</code>), and <b>Phone Number</b> (e.g., <code>08012345678</code>). Select their Role (<code>Student</code> or <code>Lecturer</code>).",
        "Tap <b>[Start Face Capture]</b>. Guide the student to stand in front of the camera with a natural, relaxed face.",
        "The screen captures 30 quick frames while the progress bar advances from 0% to 100%.",
        "A <b>Quality Scorecard</b> appears (e.g., <code>89/100 EXCELLENT</code>). Tap <b>[Save]</b> to finish registration.",
    ]
    for m in method1:
        story.append(Paragraph(f"• {m}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("Method 2: From Your Smartphone or Laptop (Wi-Fi Portal)", styles["H2"]))
    story.append(Paragraph(
        "To avoid typing names on the terminal touch screen while a line of students waits:",
        styles["Body"]
    ))
    method2 = [
        "On your mobile phone or laptop, connect to the terminal's Wi-Fi network: <b><code>Attendance_Setup</code></b>.",
        "Open your web browser (Chrome, Safari, etc.) and go to: <b><code>http://192.168.4.1/register.html</code></b>.",
        "Enter the <b>4-digit session PIN</b> shown on the terminal screen banner.",
        "Type the student's information into the web form and tap <b>[Queue for Capture]</b>.",
        "The student looks at the terminal display; the device scans them automatically and confirms on both the screen and your phone!",
    ]
    for m in method2:
        story.append(Paragraph(f"• {m}", styles["Bullet"]))

    story.append(Spacer(1, 6))
    story.append(Paragraph("Understanding Enrollment Quality Scores", styles["H2"]))

    score_guide = [
        [Paragraph("<b>Quality Rating</b>", styles["TableHeader"]), Paragraph("<b>Score Range</b>", styles["TableHeader"]), Paragraph("<b>What It Means & What You Should Do</b>", styles["TableHeader"])],
        [
            Paragraph("<font color='#059669'><b>EXCELLENT</b></font>", styles["TableCellBold"]),
            Paragraph("85 to 100", styles["TableCell"]),
            Paragraph("Optimal face scan. High accuracy under all indoor lighting conditions. Saved automatically.", styles["TableCell"]),
        ],
        [
            Paragraph("<font color='#0284C7'><b>GOOD</b></font>", styles["TableCellBold"]),
            Paragraph("70 to 84", styles["TableCell"]),
            Paragraph("Very reliable face scan. Suitable for normal daily classroom verification.", styles["TableCell"]),
        ],
        [
            Paragraph("<font color='#D97706'><b>AVERAGE</b></font>", styles["TableCellBold"]),
            Paragraph("55 to 69", styles["TableCell"]),
            Paragraph("Minor blur or slight head tilt during capture. Accepted, but student should look straighter.", styles["TableCell"]),
        ],
        [
            Paragraph("<font color='#EF4444'><b>POOR</b></font>", styles["TableCellBold"]),
            Paragraph("Below 55", styles["TableCell"]),
            Paragraph("Too blurry, dark, or angled. <b>Tap the 'Redo Enrollment' button</b> to re-scan under better posture.", styles["TableCell"]),
        ],
    ]
    t_score = Table(score_guide, colWidths=[95, 80, 365])
    t_score.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_score)

    story.append(Spacer(1, 6))
    story.append(make_callout(
        "tip",
        "Top 3 Rules for High Quality Face Scans",
        "1. <b>Natural Expression:</b> Student must keep their mouth closed and face relaxed during scanning.<br/>"
        "2. <b>Avoid Backlight:</b> Never place the terminal directly facing a bright sunny window.<br/>"
        "3. <b>Eyeglasses:</b> Keep clear prescription glasses on; remove dark sunglasses or hats with low visors.",
        styles
    ))

    story.append(PageBreak())

    # CHAPTER 6 & 7: MANAGING USERS & TELEGRAM SCHEDULING
    story.append(Paragraph("Chapter 6: Managing Registered Students", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Administrators can view, search, update, or remove enrolled users at any time via the <b>User Management</b> screen:",
        styles["Body"]
    ))

    user_mgmt = [
        "<b>Searching Students:</b> Open User Management (enter Admin PIN). Use the search bar to find any student by their name or matriculation number.",
        "<b>Updating a Face Scan (New Hair, Beard, or Glasses):</b> If a student significantly changes their appearance and recognition confidence drops, find their profile card and tap <b>[Re-enroll Face]</b>. This updates their face scan <b>without losing their past attendance history or student ID!</b>",
        "<b>Removing a Student:</b> If a student drops a course or graduates, tap <b>[Delete User]</b> and confirm. Their face scan is removed, but their historical attendance records remain safely archived for university records.",
    ]
    for um in user_mgmt:
        story.append(Paragraph(f"• {um}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 7: Scheduling Lectures via Telegram", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Instructors can schedule their classes anytime from their phone using the official Telegram Bot: <b><code>@MyUniAttendance_Bot</code></b>.",
        styles["Body"]
    ))

    story.append(Paragraph("Step 1: One-Time Lecturer Verification", styles["H2"]))
    story.append(Paragraph(
        "1. Open Telegram and search for <code>@MyUniAttendance_Bot</code>.<br/>"
        "2. Tap <b>Start</b> (or send <code>/start</code>).<br/>"
        "3. Tap the button <b>[Share Contact Details]</b>. The bot automatically links your phone number to your lecturer profile.",
        styles["Body"]
    ))

    story.append(Paragraph("Step 2: Scheduling a Lecture in 5 Steps (`/schedule`)", styles["H2"]))
    story.append(Paragraph(
        "Send <code>/schedule</code> to the bot. The bot will ask you five quick questions:",
        styles["Body"]
    ))

    sched_steps = [
        [Paragraph("<b>Step</b>", styles["TableHeader"]), Paragraph("<b>Bot Asks For</b>", styles["TableHeader"]), Paragraph("<b>Example Answer</b>", styles["TableHeader"]), Paragraph("<b>Helpful Guidance</b>", styles["TableHeader"])],
        [Paragraph("1", styles["TableCellBold"]), Paragraph("Course Code", styles["TableCell"]), Paragraph("<code>CSC 301</code>", styles["TableCell"]), Paragraph("Type your department course code.", styles["TableCell"])],
        [Paragraph("2", styles["TableCellBold"]), Paragraph("Course Title", styles["TableCell"]), Paragraph("<code>Database Systems</code>", styles["TableCell"]), Paragraph("Type the full name of the subject.", styles["TableCell"])],
        [Paragraph("3", styles["TableCellBold"]), Paragraph("Lecture Date", styles["TableCell"]), Paragraph("<code>24/10/2026</code>", styles["TableCell"]), Paragraph("Format: Day/Month/Year (DD/MM/YYYY).", styles["TableCell"])],
        [Paragraph("4", styles["TableCellBold"]), Paragraph("Start Time", styles["TableCell"]), Paragraph("<code>09:00</code>", styles["TableCell"]), Paragraph("Format: Hour:Minute in 24h time (e.g. 09:00, 14:00).", styles["TableCell"])],
        [Paragraph("5", styles["TableCellBold"]), Paragraph("End Time", styles["TableCell"]), Paragraph("<code>11:00</code>", styles["TableCell"]), Paragraph("Format: Hour:Minute when lecture finishes.", styles["TableCell"])],
    ]
    t_sched = Table(sched_steps, colWidths=[40, 110, 110, 280])
    t_sched.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_sched)

    story.append(Spacer(1, 6))
    story.append(Paragraph(
        "<b>What Happens Next?</b> When the terminal connects to Wi-Fi, it automatically downloads your schedule. When class time arrives, the device automatically sets up the session and begins taking attendance under your course code!",
        styles["Body"]
    ))

    story.append(PageBreak())

    # CHAPTER 8 & 9: REPORTS, CSV & SYSTEM SETTINGS
    story.append(Paragraph("Chapter 8: Viewing & Exporting Attendance Reports", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "You can inspect class attendance immediately on screen or save it to an Excel-friendly file:",
        styles["Body"]
    ))

    report_features = [
        "<b>Viewing On-Screen Reports:</b> Open the Menu, select <b>Attendance Reports</b>, and enter your Admin PIN. Choose today's date or select a course code to see the total number of students registered, present count, absent count, and attendance percentage.",
        "<b>Exporting to Excel (CSV File):</b> Tap the blue <b>[Export CSV to SD]</b> button. The device writes an Excel-compatible spreadsheet file directly to the memory card under <code>/sdcard/reports/</code>.",
        "<b>Accessing Exported Files:</b> Connect the terminal via USB to your computer (or access via the web portal) and open the file in Microsoft Excel, Google Sheets, or WPS Office. The sheet shows Student Name, Matric Number, Course Code, Exact Verification Time, and Status (PRESENT).",
        "<b>Automatic Telegram Class Summary:</b> When the lecture session ends, the Telegram bot automatically sends the lecturer a message with total attendance numbers and attaches the full CSV roll sheet directly to their Telegram chat!",
    ]
    for rf in report_features:
        story.append(Paragraph(f"• {rf}", styles["Bullet"]))

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 9: System Settings & Personalization", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Open <b>System Settings</b> from the menu (enter Admin PIN) to customize terminal operation:",
        styles["Body"]
    ))

    settings_items = [
        [Paragraph("<b>Setting</b>", styles["TableHeader"]), Paragraph("<b>How to Adjust</b>", styles["TableHeader"]), Paragraph("<b>Recommendation for Operators</b>", styles["TableHeader"])],
        [
            Paragraph("Screen Brightness", styles["TableCellBold"]),
            Paragraph("Drag the Brightness slider (10% to 100%)", styles["TableCell"]),
            Paragraph("Keep at 60% – 70% indoors to save battery while keeping the display clear.", styles["TableCell"]),
        ],
        [
            Paragraph("Theme Switch", styles["TableCellBold"]),
            Paragraph("Toggle the Light / Dark theme switch", styles["TableCell"]),
            Paragraph("Dark theme is default (optimal for indoor halls and reduced battery power draw).", styles["TableCell"]),
        ],
        [
            Paragraph("Wi-Fi Setup", styles["TableCellBold"]),
            Paragraph("Tap [Scan Networks], pick SSID, enter password", styles["TableCell"]),
            Paragraph("Connect to campus Wi-Fi so reports and schedules sync automatically with Telegram.", styles["TableCell"]),
        ],
        [
            Paragraph("Change Admin PIN", styles["TableCellBold"]),
            Paragraph("Tap [Security] -> [Change PIN]", styles["TableCell"]),
            Paragraph("Change the initial default PIN (1234) to a confidential 4-digit code known only to authorized staff.", styles["TableCell"]),
        ],
    ]
    t_sett = Table(settings_items, colWidths=[110, 155, 275])
    t_sett.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_sett)

    story.append(Spacer(1, 8))
    story.append(make_callout(
        "info",
        "Memory Card Protection",
        "The MicroSD card inserted into the bottom slot contains the entire offline student database and facial recognition models. Never remove the memory card during operation. If the card is removed, the device will display an error and halt scanning until re-inserted.",
        styles
    ))

    story.append(PageBreak())

    # CHAPTER 10: SIMPLE TROUBLESHOOTING & FAQS
    story.append(Paragraph("Chapter 10: Simple Troubleshooting & FAQs", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Here are answers to the most common questions and simple fixes for everyday classroom situations:",
        styles["Body"]
    ))

    trouble_layman = [
        [Paragraph("<b>Problem Encountered</b>", styles["TableHeader"]), Paragraph("<b>Common Cause</b>", styles["TableHeader"]), Paragraph("<b>Easy Solution for the Operator</b>", styles["TableHeader"])],
        [
            Paragraph("Device will not turn on", styles["TableCellBold"]),
            Paragraph("1. Switch is DOWN.<br/>2. Battery is drained.", styles["TableCell"]),
            Paragraph("Ensure the slide switch on the left side is pushed <b>UP</b>. If battery is dead, plug in the 5V 2-3A USB-C charger below the switch.", styles["TableCell"]),
        ],
        [
            Paragraph("Student's face shows Red ('Unknown')", styles["TableCellBold"]),
            Paragraph("1. Not enrolled.<br/>2. Changed appearance.<br/>3. Heavy sunglasses/hat.", styles["TableCell"]),
            Paragraph("Ask student to remove sunglasses or low hats. If they changed hairstyle or grew a beard, use <b>Re-enroll Face</b> in User Management.", styles["TableCell"]),
        ],
        [
            Paragraph("Screen says 'Glare detected'", styles["TableCellBold"]),
            Paragraph("Direct sunlight or lamp shining into lens.", styles["TableCell"]),
            Paragraph("Turn the terminal slightly away from sunny windows, or tap the <b>Bright</b> / <b>Outdoor</b> camera profile button on the right panel.", styles["TableCell"]),
        ],
        [
            Paragraph("Student says they didn't get counted", styles["TableCellBold"]),
            Paragraph("Scanned again within 15 minutes.", styles["TableCell"]),
            Paragraph("Explain that the 15-minute duplicate prevention stops double check-ins. Check Attendance Reports to confirm they are already marked PRESENT.", styles["TableCell"]),
        ],
        [
            Paragraph("Wi-Fi icon has an 'X' or shows disconnected", styles["TableCellBold"]),
            Paragraph("Out of Wi-Fi range or router restarted.", styles["TableCell"]),
            Paragraph("<b>Do not worry!</b> The device works 100% offline. Attendance continues normally and syncs automatically when Wi-Fi reconnects.", styles["TableCell"]),
        ],
        [
            Paragraph("Touchscreen is not responding smoothly", styles["TableCellBold"]),
            Paragraph("Fingerprints, moisture, or dust on glass.", styles["TableCell"]),
            Paragraph("Gently wipe the glass screen with a clean, dry microfiber cloth. Avoid wet paper towels or harsh window cleaners.", styles["TableCell"]),
        ],
    ]
    t_tl = Table(trouble_layman, colWidths=[125, 130, 285])
    t_tl.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_tl)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Everyday Care & Maintenance Tips", styles["H2"]))
    care_tips = [
        "<b>Screen Cleaning:</b> Always use a dry, soft microfiber cloth to clean the glass touchscreen and camera lens. Never spray water, alcohol, or liquid cleaners directly onto the unit.",
        "<b>Safe Storage:</b> When not in use, slide the power switch DOWN and store the terminal in a clean, dry office drawer or padded bag. Keep away from excessive dust or extreme heat.",
        "<b>Charging Safety:</b> Use a certified 5V 2A–3A USB Type-C power adapter. Do not leave the device charging on soft beds or enclosed cushions.",
        "<b>Memory Card Slot:</b> Keep the MicroSD memory card inserted in the bottom slot at all times.",
    ]
    for ct in care_tips:
        story.append(Paragraph(f"• {ct}", styles["Bullet"]))

    story.append(Spacer(1, 12))
    story.append(HRFlowable(width="100%", thickness=1, color=C_SECONDARY, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<font size='8' color='#64748B'>SmartAttendance™ Touchscreen Biometric Terminal — Official Administrator & Operator Guide. Document ID: SA-OP-MANUAL-2026-V2.3. Copyright © 2026 SmartAttendance Project Team. All rights reserved.</font>",
        styles["Body"]
    ))

    return story


def generate_operator_pdf(output_path):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    doc = SimpleDocTemplate(
        output_path,
        pagesize=letter,
        leftMargin=MARGIN, rightMargin=MARGIN,
        topMargin=MARGIN, bottomMargin=MARGIN,
    )
    story = build_operator_story()
    print(f"Building Admin Operator User Manual PDF (Times New Roman 12pt) at: {output_path} ...")
    doc.build(story, canvasmaker=OperatorNumberedCanvas)
    print("Admin Operator User Manual PDF build completed successfully.")


if __name__ == "__main__":
    out_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "SmartAttendance_User_Manual.pdf"
    )
    generate_operator_pdf(out_file)
