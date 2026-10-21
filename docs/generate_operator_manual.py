"""
SmartAttendance Admin Operator & User Manual Generator (Layman Edition).
Generates an intuitive, non-technical, publication-grade manual exclusively focused on
daily operational procedures, administrative workflows, and practical user guidance:
- Overview of device parts and controls
- Powering on, battery levels, and charging instructions
- Initial Setup Wizard (Setting Admin PIN, Date/Time, Department info)
- Daily attendance scanning operations for lectures and events
- Enrolling students & staff (On-device touch screen and Smartphone Wi-Fi portal)
- Managing registered users, updating appearances, and student search
- Scheduling classes via the Telegram Bot (@MyUniAttendance_Bot)
- Viewing attendance records and exporting reports to Excel CSV
- Sound volume, screen brightness, and Wi-Fi connection
- Everyday troubleshooting, screen cleaning, and FAQs
Contains ZERO developer/technician jargon (no registers, baud rates, GPIOs, C++ code, or schematics).
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

# Friendly, clean, modern corporate palette
C_PRIMARY = colors.HexColor("#1E3A8A")    # Classic Royal Navy
C_SECONDARY = colors.HexColor("#0284C7")  # Friendly Sky Blue
C_DARK = colors.HexColor("#0F172A")       # Soft Charcoal
C_BODY = colors.HexColor("#334155")       # Readable Slate Body
C_MUTED = colors.HexColor("#64748B")      # Muted Gray
C_BG_LIGHT = colors.HexColor("#F8FAFC")   # Crisp Soft Background
C_BORDER = colors.HexColor("#E2E8F0")     # Light Divider
C_CARD_BG = colors.HexColor("#F1F5F9")    # Step Card Background

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
        self.setFont("Helvetica", 8.5)
        self.setFillColor(C_MUTED)

        # Header
        header_text = "SmartAttendance™ Biometric Terminal — Administrator & Operator Guide"
        self.drawString(MARGIN, PAGE_HEIGHT - 25, header_text)
        self.setStrokeColor(C_BORDER)
        self.setLineWidth(0.5)
        self.line(MARGIN, PAGE_HEIGHT - 28, PAGE_WIDTH - MARGIN, PAGE_HEIGHT - 28)

        # Footer
        footer_left = "Daily Usage, Enrollment, Scheduling, Reports & Charging Instructions"
        footer_right = f"Page {self._pageNumber} of {total_pages}"
        self.drawString(MARGIN, 20, footer_left)
        self.drawRightString(PAGE_WIDTH - MARGIN, 20, footer_right)
        self.line(MARGIN, 28, PAGE_WIDTH - MARGIN, 28)

        self.restoreState()


def get_styles():
    base = getSampleStyleSheet()
    return {
        "CoverTag": ParagraphStyle(
            "CoverTag", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=10, leading=14, textColor=C_SECONDARY, spaceAfter=6,
        ),
        "CoverTitle": ParagraphStyle(
            "CoverTitle", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=26, leading=32, textColor=C_PRIMARY, spaceAfter=8,
        ),
        "CoverSubtitle": ParagraphStyle(
            "CoverSubtitle", parent=base["Normal"], fontName="Helvetica",
            fontSize=13, leading=18, textColor=C_DARK, spaceAfter=14,
        ),
        "CoverMeta": ParagraphStyle(
            "CoverMeta", parent=base["Normal"], fontName="Helvetica",
            fontSize=8.5, leading=13, textColor=C_BODY,
        ),
        "CoverMetaBold": ParagraphStyle(
            "CoverMetaBold", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=8.5, leading=13, textColor=C_PRIMARY,
        ),
        "H1": ParagraphStyle(
            "H1", parent=base["Heading1"], fontName="Helvetica-Bold",
            fontSize=15, leading=19, textColor=C_PRIMARY, spaceBefore=11, spaceAfter=5, keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2", parent=base["Heading2"], fontName="Helvetica-Bold",
            fontSize=11.5, leading=15, textColor=C_SECONDARY, spaceBefore=8, spaceAfter=4, keepWithNext=True,
        ),
        "H3": ParagraphStyle(
            "H3", parent=base["Heading3"], fontName="Helvetica-Bold",
            fontSize=9.5, leading=13, textColor=C_DARK, spaceBefore=6, spaceAfter=3, keepWithNext=True,
        ),
        "Body": ParagraphStyle(
            "Body", parent=base["Normal"], fontName="Helvetica",
            fontSize=8.8, leading=13, textColor=C_BODY, spaceAfter=4.5,
        ),
        "Bullet": ParagraphStyle(
            "Bullet", parent=base["Normal"], fontName="Helvetica",
            fontSize=8.8, leading=13, textColor=C_BODY, leftIndent=12, firstLineIndent=-9, spaceAfter=3,
        ),
        "TableHeader": ParagraphStyle(
            "TableHeader", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=8.5, leading=12, textColor=colors.white,
        ),
        "TableCell": ParagraphStyle(
            "TableCell", parent=base["Normal"], fontName="Helvetica",
            fontSize=8, leading=11.5, textColor=C_BODY,
        ),
        "TableCellBold": ParagraphStyle(
            "TableCellBold", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=8, leading=11.5, textColor=C_DARK,
        ),
        "CalloutBold": ParagraphStyle(
            "CalloutBold", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=8.5, leading=12, textColor=C_PRIMARY,
        ),
        "CalloutText": ParagraphStyle(
            "CalloutText", parent=base["Normal"], fontName="Helvetica",
            fontSize=8.3, leading=12, textColor=C_DARK,
        ),
        "TOCItem": ParagraphStyle(
            "TOCItem", parent=base["Normal"], fontName="Helvetica",
            fontSize=8.5, leading=13, textColor=C_DARK,
        ),
        "TOCItemBold": ParagraphStyle(
            "TOCItemBold", parent=base["Normal"], fontName="Helvetica-Bold",
            fontSize=9, leading=13, textColor=C_PRIMARY,
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
        ("PADDING", (0, 0), (-1, -1), 5),
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

    story.append(HRFlowable(width="100%", thickness=3, color=C_SECONDARY, spaceBefore=2, spaceAfter=14))

    highlights = [
        "<b>Simple Touchscreen Operation:</b> 7-inch color display designed for intuitive everyday classroom use.",
        "<b>Fast & Hygienic Attendance:</b> Automatic facial recognition in under 1 second without touching the device.",
        "<b>Works Anywhere:</b> Operates 100% offline without internet; optional Wi-Fi syncs records seamlessly.",
        "<b>Quick Student Registration:</b> Register students directly on screen or from your smartphone via Wi-Fi.",
        "<b>Automated Class Scheduling:</b> Lecturers schedule classes easily through the official Telegram Bot.",
        "<b>Portable Battery Power:</b> Built-in rechargeable battery provides 4 to 6 hours of continuous scanning.",
        "<b>Instant Reports:</b> Export attendance directly to an Excel-compatible CSV file on the memory card.",
        "<b>Telegram Push Alerts:</b> Students receive an immediate confirmation message when marked present.",
    ]
    cov_p = [Paragraph(f"• {h}", styles["Body"]) for h in highlights]
    t_cov = Table([[cov_p]], colWidths=[USABLE_WIDTH])
    t_cov.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), C_CARD_BG),
        ("BOX", (0, 0), (-1, -1), 1, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 9),
    ]))
    story.append(t_cov)
    story.append(Spacer(1, 20))

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
            Paragraph("<b>Document Version:</b>", styles["CoverMetaBold"]),
            Paragraph("2.1.0 (Operator Edition)", styles["CoverMeta"]),
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
    story.append(Spacer(1, 15))

    story.append(make_callout(
        "tip",
        "About This Manual",
        "This guide is written specifically for day-to-day operators. It explains how to set up the device, enroll students, run attendance sessions, schedule classes, and charge the unit without complicated engineering terms. If you need hardware repair or circuit pinout details, consult the separate Technician Manual.",
        styles
    ))

    story.append(PageBreak())

    # TABLE OF CONTENTS
    story.append(Paragraph("Table of Contents", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1.5, color=C_PRIMARY, spaceBefore=2, spaceAfter=8))

    chapters = [
        ("Chapter 1: Getting to Know Your Terminal", "Device parts, touchscreen controls, camera lens, ports, and buttons"),
        ("Chapter 2: Powering On, Battery Levels & Charging", "Turning on/off, understanding battery icons, charging times, battery care"),
        ("Chapter 3: First-Time Setup Wizard", "Setting your 4-digit Admin PIN, department name, clock, and Wi-Fi connection"),
        ("Chapter 4: Daily Attendance Scanning in the Classroom", "Taking attendance, screen colors (green/amber/red), chime, duplicate prevention"),
        ("Chapter 5: Enrolling New Students & Staff", "On-screen registration, smartphone Wi-Fi portal, lighting tips for best quality"),
        ("Chapter 6: Managing Registered Students", "Searching student roster, updating face scans for new looks, removing users"),
        ("Chapter 7: Scheduling Lectures via Telegram", "Using the @MyUniAttendance_Bot 5-step wizard to schedule classes automatically"),
        ("Chapter 8: Viewing & Exporting Attendance Reports", "Checking today's roll on screen, saving Excel-compatible CSV files to memory card"),
        ("Chapter 9: System Settings & Personalization", "Adjusting screen brightness, sound chime volume, Wi-Fi passwords, changing PIN"),
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
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("LINEBELOW", (0, 0), (-1, -1), 0.5, C_BG_LIGHT),
    ]))
    story.append(t_table)
    story.append(Spacer(1, 12))

    story.append(Paragraph("Quick Overview: How the System Works", styles["H2"]))
    story.append(Paragraph(
        "The SmartAttendance terminal is an all-in-one smart device designed to make taking attendance fast, accurate, and completely paperless. Here is how simple the daily routine is:",
        styles["Body"]
    ))
    daily_overview = [
        "<b>1. Bring to Class:</b> Place the device on the lecturer's desk or mount it by the classroom entrance.",
        "<b>2. Automatic Session:</b> If the class was scheduled via Telegram, the device automatically shows the active course name on screen. If not, tap the screen to select your course.",
        "<b>3. Students Walk By:</b> Students pause for just one second in front of the screen. A green box appears with a friendly chime confirming their name.",
        "<b>4. Instant Records:</b> Attendance is saved immediately into the memory card. Students receive a Telegram alert on their phones, and the lecturer gets a complete attendance sheet after class.",
    ]
    for d in daily_overview:
        story.append(Paragraph(f"• {d}", styles["Bullet"]))

    story.append(PageBreak())

    # CHAPTER 1 & 2: DEVICE PARTS, POWER & CHARGING
    story.append(Paragraph("Chapter 1: Getting to Know Your Terminal", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))
    story.append(Paragraph(
        "Before your first session, familiarize yourself with the exterior parts and controls of the terminal:",
        styles["Body"]
    ))

    parts = [
        [Paragraph("<b>Component</b>", styles["TableHeader"]), Paragraph("<b>Location</b>", styles["TableHeader"]), Paragraph("<b>Function in Daily Usage</b>", styles["TableHeader"])],
        [
            Paragraph("7.0″ Touchscreen", styles["TableCellBold"]),
            Paragraph("Front Face", styles["TableCell"]),
            Paragraph("High-definition color display. Shows the live camera view, student names, menus, and attendance counters.", styles["TableCell"]),
        ],
        [
            Paragraph("Camera Lens", styles["TableCellBold"]),
            Paragraph("Top / Side Ribbon", styles["TableCell"]),
            Paragraph("Scans student faces automatically. Keep lens clean and free of fingerprints.", styles["TableCell"]),
        ],
        [
            Paragraph("Status Bar", styles["TableCellBold"]),
            Paragraph("Top Edge of Screen", styles["TableCell"]),
            Paragraph("Displays current time, active course name, Wi-Fi connection icon, and battery percentage.", styles["TableCell"]),
        ],
        [
            Paragraph("Power Button", styles["TableCellBold"]),
            Paragraph("Side Edge", styles["TableCell"]),
            Paragraph("Press and hold for 2 seconds to turn the device ON or OFF.", styles["TableCell"]),
        ],
        [
            Paragraph("USB-C Charging Port", styles["TableCellBold"]),
            Paragraph("Side Edge", styles["TableCell"]),
            Paragraph("Connect the included 5V/3A wall charger to recharge the built-in battery or provide continuous power.", styles["TableCell"]),
        ],
        [
            Paragraph("Memory Card Slot", styles["TableCellBold"]),
            Paragraph("Rear / Side Slot", styles["TableCell"]),
            Paragraph("Holds the MicroSD memory card storing student records, attendance logs, and exported CSV files.", styles["TableCell"]),
        ],
        [
            Paragraph("Internal Speaker", styles["TableCellBold"]),
            Paragraph("Rear Housing", styles["TableCell"]),
            Paragraph("Plays a pleasant chime when attendance is successfully recorded or an alert if an error occurs.", styles["TableCell"]),
        ],
    ]
    t_parts = Table(parts, colWidths=[110, 100, 330])
    t_parts.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), C_PRIMARY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, C_BG_LIGHT]),
        ("GRID", (0, 0), (-1, -1), 0.5, C_BORDER),
        ("PADDING", (0, 0), (-1, -1), 3.5),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t_parts)

    story.append(Spacer(1, 8))
    story.append(Paragraph("Chapter 2: Powering On, Battery Levels & Charging", styles["H1"]))
    story.append(HRFlowable(width="100%", thickness=1, color=C_PRIMARY, spaceBefore=2, spaceAfter=6))

    power_steps = [
        "<b>Turning On:</b> Press and hold the power button for <b>2 seconds</b>. The screen lights up with the SmartAttendance logo and enters the main scanner screen in about 5 seconds.",
        "<b>Turning Off:</b> Open the Menu, tap <b>[Power Off]</b>, and confirm. Alternatively, press and hold the power button for 3 seconds.",
        "<b>Charging the Device:</b> Plug the provided USB-C power adapter into any standard wall outlet (100V – 240V) and connect the cable to the terminal. A full recharge takes approximately <b>2.5 to 3.5 hours</b>.",
        "<b>Continuous Power:</b> You can use the device continuously while it is plugged into the charger during long lecture days without harming the battery.",
    ]
    for ps in power_steps:
        story.append(Paragraph(f"• {ps}", styles["Bullet"]))

    story.append(Spacer(1, 4))
    story.append(Paragraph("Understanding the Battery Indicator", styles["H2"]))

    batt_table = [
        [Paragraph("<b>Screen Icon</b>", styles["TableHeader"]), Paragraph("<b>Color</b>", styles["TableHeader"]), Paragraph("<b>Battery Level</b>", styles["TableHeader"]), Paragraph("<b>What You Should Do</b>", styles["TableHeader"])],
        [
            Paragraph("Lightning Bolt", styles["TableCellBold"]),
            Paragraph("<font color='#0284C7'><b>Cyan Blue</b></font>", styles["TableCell"]),
            Paragraph("Charging in progress", styles["TableCell"]),
            Paragraph("Device is connected to power and charging safely. You can continue taking attendance.", styles["TableCell"]),
        ],
        [
            Paragraph("Solid Battery (4 Bars)", styles["TableCellBold"]),
            Paragraph("White / Green", styles["TableCell"]),
            Paragraph("90% to 100% Full", styles["TableCell"]),
            Paragraph("Battery is fully charged. Excellent for portable classroom use (4 to 6 hours runtime).", styles["TableCell"]),
        ],
        [
            Paragraph("Half Battery (2 Bars)", styles["TableCellBold"]),
            Paragraph("White", styles["TableCell"]),
            Paragraph("30% to 59% Medium", styles["TableCell"]),
            Paragraph("Sufficient battery remaining for 1 to 2 typical lecture sessions.", styles["TableCell"]),
        ],
        [
            Paragraph("Low Battery (1 Bar)", styles["TableCellBold"]),
            Paragraph("<font color='#D97706'><b>Amber</b></font>", styles["TableCell"]),
            Paragraph("10% to 15% Low", styles["TableCell"]),
            Paragraph("A low battery notification appears. Plug the device into its charger as soon as possible.", styles["TableCell"]),
        ],
        [
            Paragraph("Empty Battery", styles["TableCellBold"]),
            Paragraph("<font color='#EF4444'><b>Flashing Red</b></font>", styles["TableCell"]),
            Paragraph("Below 10% Critical", styles["TableCell"]),
            Paragraph("Connect charger immediately. <b>At 5%, the device safely saves all data and turns off automatically.</b>", styles["TableCell"]),
        ],
    ]
    t_batt = Table(batt_table, colWidths=[95, 80, 115, 250])
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
        "When you turn on a brand-new device for the first time, a simple 4-step wizard guides you through initial setup:",
        styles["Body"]
    ))

    wizard_steps = [
        "<b>Step 1: Set Your Master Admin PIN:</b> Choose a memorable <b>4-digit PIN</b> (default initial code is <code>1234</code>). This PIN protects student management, reports, and settings from unauthorized tampering. Do not share this PIN with students.",
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
        "During class, the device runs in automatic scanning mode. Taking attendance is completely contactless:",
        styles["Body"]
    ))

    scan_steps = [
        "<b>1. Student Positioning:</b> The student approaches the terminal and pauses about <b>2 to 3 feet</b> (arm's length) in front of the screen, looking directly at the camera.",
        "<b>2. Automatic Recognition (< 1 Second):</b> The device instantly finds their face and verifies their identity against the registered student roster.",
        "<b>3. Understanding Screen Reticle Colors:</b><br/>"
        "• <font color='#10B981'><b>GREEN BOX + CHIME:</b></font> <b>Success!</b> The screen displays: <code>\"Welcome, [Student Name]! Present at 09:15 AM\"</code>.<br/>"
        "• <font color='#F59E0B'><b>AMBER BOX:</b></font> <b>Verifying:</b> The student should hold still for a moment.<br/>"
        "• <font color='#EF4444'><b>RED BOX:</b></font> <b>Unrecognized:</b> The face is not registered in the system, or the student needs to adjust their posture.",
        "<b>4. Helpful On-Screen Guidance Prompts:</b><br/>"
        "• <i>\"Please close mouth & look straight\"</i> — student is laughing, talking, or yawning.<br/>"
        "• <i>\"Glare detected — adjust angle from light\"</i> — sunlight or a bright lamp is shining directly behind or onto the face.<br/>"
        "• <i>\"Blurry image — please hold still\"</i> — student is walking past too quickly.",
        "<b>5. Duplicate Scan Prevention:</b> If a student scans their face again during the same lecture within 15 minutes, the screen says <code>\"Already Recorded\"</code>. They will not be counted twice.",
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
        "<b>Opening the File on Your Computer:</b> Remove the MicroSD card (or connect via USB) and open the file in Microsoft Excel, Google Sheets, or WPS Office. The sheet shows Student Name, Matric Number, Course Code, Exact Verification Time, and Status (PRESENT).",
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
            Paragraph("Sound Volume", styles["TableCellBold"]),
            Paragraph("Drag Volume slider; tap [Test Sound]", styles["TableCell"]),
            Paragraph("Set louder (80% – 100%) in noisy lecture theatres so students clearly hear the confirmation chime.", styles["TableCell"]),
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
        "Screen Auto-Dim Feature",
        "To maximize battery runtime during long exams or seminars, the screen will gently dim after 2 minutes of inactivity. When a student approaches the camera, it instantly brightens to full view.",
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
            Paragraph("Battery is completely drained.", styles["TableCell"]),
            Paragraph("Plug in the included 5V/3A USB-C wall charger. Wait 5 minutes, then press and hold the power button for 2 seconds.", styles["TableCell"]),
        ],
        [
            Paragraph("Student's face shows Red Box ('Unknown')", styles["TableCellBold"]),
            Paragraph("1. Not enrolled.<br/>2. Changed appearance.<br/>3. Heavy sunglasses/hat.", styles["TableCell"]),
            Paragraph("Ask student to remove sunglasses or low hats. If they changed hairstyle or grew a beard, use <b>Re-enroll Face</b> in User Management.", styles["TableCell"]),
        ],
        [
            Paragraph("Screen says 'Glare detected'", styles["TableCellBold"]),
            Paragraph("Direct sunlight or lamp shining into lens.", styles["TableCell"]),
            Paragraph("Turn the terminal slightly away from sunny windows or bare ceiling spotlights.", styles["TableCell"]),
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
        "<b>Screen Cleaning:</b> Always use a dry, soft microfiber cloth to clean the glass touchscreen. Never spray water, alcohol, or liquid cleaners directly onto the unit.",
        "<b>Safe Storage:</b> When not in use, store the terminal in a clean, dry office drawer or protective padded bag. Keep away from excessive dust, direct sunlight, or extreme heat.",
        "<b>Charging Safety:</b> Only use the provided 5V/3A USB-C power adapter. Do not leave the device charging on soft beds or enclosed cushions.",
    ]
    for ct in care_tips:
        story.append(Paragraph(f"• {ct}", styles["Bullet"]))

    story.append(Spacer(1, 12))
    story.append(HRFlowable(width="100%", thickness=1, color=C_SECONDARY, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "<font size='7.5' color='#64748B'>SmartAttendance™ Touchscreen Biometric Terminal — Official Administrator & Operator Guide. Document ID: SA-OP-MANUAL-2026-V2. Copyright © 2026 SmartAttendance Project Team. All rights reserved.</font>",
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
    print(f"Building Admin Operator User Manual PDF at: {output_path} ...")
    doc.build(story, canvasmaker=OperatorNumberedCanvas)
    print("Admin Operator User Manual PDF build completed successfully.")


if __name__ == "__main__":
    out_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "SmartAttendance_User_Manual.pdf"
    )
    generate_operator_pdf(out_file)
