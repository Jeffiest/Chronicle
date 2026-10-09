#!/usr/bin/env python3
"""Font metrics, token expansion, and box boundary calculations for Dark Cloud.

Measures strings using the game font (DarkCloudCompendium.ttf) and PS2 font/gaiji
metrics from port/src/gametext.cpp, ps2/src/clsmes.cpp, and ps2/src/gameutil.cpp.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Optional, Tuple, List, Dict

try:
    from PIL import ImageFont
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

# Default font cell dimensions from PS2 and port sources:
# ClsMes preset system / CommonMenuMes / dialogue: 11x20 (char_width=11, char_height=20)
# Shop / DngMesStb: 12x24 (char_width=12, char_height=24)
DEFAULT_CHAR_WIDTH = 11.0
DEFAULT_CHAR_HEIGHT = 20.0

# Gaiji cell counts from GaijiDataTbl (ps2/src/gameutil.cpp lines 1809-1860):
# Button and symbol tokens advance by (char_width * cells)
GAIJI_CELL_COUNTS: Dict[str, int] = {
    # 8-cell buttons
    "select": 8,
    "start": 8,
    # 2-cell gamepad face buttons & shoulders
    "cross": 2,
    "circle": 2,
    "square": 2,
    "triangle": 2,
    "l1": 2,
    "r1": 2,
    "l2": 2,
    "r2": 2,
    # 2-cell dpad & arrows
    "dpad": 2,
    "dpad-updown": 2,
    "dpad-sides": 2,
    "up": 2,
    "right": 2,
    "down": 2,
    "left": 2,
    "yellow-arrow": 2,
    "orange-arrow": 2,
    # 2-cell icons and symbols
    "red-slash": 2,
    "heart": 2,
    "note": 2,
    "red-x": 2,
    "alert": 2,
    "icon-sword": 2,
    "icon-orb": 2,
    "icon-georama": 2,
    "icon-jar": 2,
    # Variable-cell special icons
    "hand": 3,
    "monster": 4,
    "bait": 5,
    "word-button": 5,
}

# Formatting codes that alter color/timing/spacing without adding horizontal advance
ZERO_WIDTH_TOKENS = {
    "page", "/color", "white", "yellow", "cyan", "green", "dark", "gold", "grey",
    "highlight",
}

# Regular expressions for token parsing
TOKEN_RE = re.compile(r"\{\{|\{([^}]+)\}")
GAP_RE = re.compile(r"^gap\s+(\d+)$", re.IGNORECASE)
SPACING_RE = re.compile(r"^spacing\s+(\d+)$", re.IGNORECASE)
JUSTIFY_RE = re.compile(r"^justify\s+(\d+)$", re.IGNORECASE)
BUBBLE_RE = re.compile(r"^bubble\s+(\d+)$", re.IGNORECASE)
COLOR_RE = re.compile(r"^color\s+(\d+)$", re.IGNORECASE)
WAIT_RE = re.compile(r"^wait\s+(\d+)$", re.IGNORECASE)
ICON_RE = re.compile(r"^icon\s+(\d+)$", re.IGNORECASE)
NAME_RE = re.compile(r"^name(\d+)$", re.IGNORECASE)
VALUE_RE = re.compile(r"^value(\d*)$", re.IGNORECASE)
INSERT_RE = re.compile(r"^insert(\d+)$", re.IGNORECASE)


class FontMetrics:
    """Measures Dark Cloud text lines and expands tokens with authentic advance widths."""

    def __init__(self, font_path: Optional[Path] = None, char_width: float = DEFAULT_CHAR_WIDTH):
        self.char_width = char_width
        self.font_path = font_path
        self._font = None

        if font_path is None:
            # Locate tools/font/DarkCloudCompendium.ttf relative to this file
            default_font = Path(__file__).resolve().parent.parent / "font" / "DarkCloudCompendium.ttf"
            if default_font.exists():
                self.font_path = default_font

        if HAS_PIL and self.font_path and self.font_path.exists():
            try:
                # Size 18 in DarkCloudCompendium.ttf matches 11.0 advance width exactly
                font_pt = 18 if round(char_width) == 11 else int(round(char_width * 1.64))
                self._font = ImageFont.truetype(str(self.font_path), font_pt)
            except Exception:
                self._font = None

    def get_token_advance(self, token: str) -> float:
        """Returns the horizontal advance width of a control token in pixels."""
        lower = token.strip().lower()

        if lower in ZERO_WIDTH_TOKENS:
            return 0.0

        if lower in GAIJI_CELL_COUNTS:
            return GAIJI_CELL_COUNTS[lower] * self.char_width

        # Gap adds literal pixel width (e.g. {gap 8} -> 8px)
        gap_match = GAP_RE.match(lower)
        if gap_match:
            return float(gap_match.group(1))

        # Color/formatting/bubble/timing tokens advance 0
        if (COLOR_RE.match(lower) or WAIT_RE.match(lower) or BUBBLE_RE.match(lower)
                or SPACING_RE.match(lower) or JUSTIFY_RE.match(lower)):
            return 0.0

        # Generic icon code default: 2 cells
        if ICON_RE.match(lower):
            return 2.0 * self.char_width

        # Character name placeholder: ~4 characters average ("Toan")
        if NAME_RE.match(lower):
            return 4.0 * self.char_width

        # Value placeholder: ~2 digits average ("99")
        if VALUE_RE.match(lower):
            return 2.0 * self.char_width

        # Insert message placeholder: ~4 characters average
        if INSERT_RE.match(lower):
            return 4.0 * self.char_width

        # Numeric escape {-253} etc.
        try:
            num = int(lower)
            if -0x300 <= num < -0x2DF:
                return 2.0 * self.char_width
            if -0x400 <= num <= -0x301 or -0x200 <= num <= -0x101:
                return 0.0
            if -0x700 <= num < -0x600:
                return float(num + 0x700)
        except ValueError:
            pass

        # Fallback for unrecognized token: measure as text
        return len(token) * self.char_width

    def get_char_advance(self, ch: str) -> float:
        """Returns the advance width of a single character in pixels."""
        if ch == '\n' or ch == '\r':
            return 0.0
        if ch == ' ' or ch == '\xa0':
            return self.char_width
        if self._font is not None:
            try:
                # In DarkCloudCompendium.ttf, accented letters (e.g. é) have blank contours
                # but valid cell advances, or fall back to base letter advance
                adv = self._font.getlength(ch)
                if adv > 0:
                    return adv
            except Exception:
                pass
        return self.char_width

    def measure_line(self, line: str) -> float:
        """Measures the pixel advance width of a single line of text."""
        total_width = 0.0
        pos = 0

        for match in TOKEN_RE.finditer(line):
            start, end = match.span()
            # Measure any plain characters before the token
            if start > pos:
                for ch in line[pos:start]:
                    total_width += self.get_char_advance(ch)

            matched_text = match.group(0)
            if matched_text == "{{":
                total_width += self.get_char_advance("{")
            else:
                token_name = match.group(1)
                total_width += self.get_token_advance(token_name)

            pos = end

        # Measure any trailing characters
        if pos < len(line):
            for ch in line[pos:]:
                total_width += self.get_char_advance(ch)

        return total_width

    def measure_text(self, text: str) -> Tuple[float, List[float], int]:
        """Measures text, returning (max_line_width, line_widths, line_count).

        Treats {page} as breaking text blocks; each line in the text is measured.
        """
        if not text:
            return 0.0, [0.0], 1

        # Replace {page} with newline so page-delimited lines are evaluated
        normalized = text.replace("{page}", "\n")
        lines = normalized.split("\n")
        line_widths = [self.measure_line(line) for line in lines]
        max_width = max(line_widths) if line_widths else 0.0
        return max_width, line_widths, len(lines)


# Known box dimension rules derived from:
# - port/src/options/screen.hpp & rows.cpp (Options UI dimensions)
# - ps2/src/dun/gameloop.cpp & dngmessageman.cpp (Dungeon message boxes)
# - ps2/src/menu_draw.cpp (Common menu message boxes)
KNOWN_BOX_RULES: List[Tuple[re.Pattern, float, int, str]] = [
    # Options Screen settings
    (re.compile(r"^options\..*\.label$"), 236.0, 1, "Options Row Label (236px, 1 line)"),
    (re.compile(r"^options\..*\.choice\.\d+$"), 180.0, 1, "Options Setting Choice (180px, 1 line)"),
    (re.compile(r"^options\..*\.value\..*$"), 180.0, 1, "Options Setting Value (180px, 1 line)"),
    (re.compile(r"^options\..*\.help$"), 270.0, 4, "Options Help Pane (270px, 4 lines)"),
    (re.compile(r"^options\.help\..*$"), 270.0, 4, "Options Help Pane (270px, 4 lines)"),
    (re.compile(r"^options\.shortcuts$"), 300.0, 3, "Options Shortcuts Footer (300px, 3 lines)"),
    (re.compile(r"^options\.page\..*$"), 100.0, 1, "Options Tab Title (100px, 1 line)"),
    (re.compile(r"^options\.display\..*_now$"), 270.0, 3, "Options Display Message (270px, 3 lines)"),

    # Dungeon System & Dialogue Boxes (DngMes1, DngMes2: 15 columns x 3 rows x 11px)
    (re.compile(r"^dun\.message\.ww_mes\.dunmsd\w*"), 165.0, 3, "Dungeon System Box (165px, 3 lines)"),
    (re.compile(r"^dun\.message\.old_data\.steeb\w*"), 180.0, 4, "Steve Message Box (180px, 4 lines)"),
    (re.compile(r"^dun\.message\.old_data\.dungeon"), 165.0, 3, "Dungeon Notice Box (165px, 3 lines)"),

    # Common Menus (CommonMenuMes3: 29 columns x 4 rows x 11px = 319px)
    (re.compile(r"^commenu\..*allmenu\.\d+$"), 319.0, 4, "Menu Help Box (319px, 4 lines)"),
    (re.compile(r"^commenu\..*itemshop\.\d+$"), 360.0, 3, "Item Shop Dialog (360px, 3 lines)"),
    (re.compile(r"^commenu\..*manual\.\d+$"), 319.0, 4, "Game Manual Box (319px, 4 lines)"),
    (re.compile(r"^commenu\..*nameregi\.\d+$"), 250.0, 3, "Name Entry Box (250px, 3 lines)"),
]


def resolve_box_limits(
    key: str,
    ref_text: Optional[str] = None,
    metrics: Optional[FontMetrics] = None,
) -> Tuple[float, int, str]:
    """Determines the (width_limit, line_limit, box_type) for a message key.

    If a specific UI box rule matches the key, returns its fixed dimensions.
    Otherwise, uses the reference English string's measured width and line count
    as the constraint where the original text fit.
    """
    for pattern, width, lines, box_type in KNOWN_BOX_RULES:
        if pattern.match(key):
            return width, lines, box_type

    if metrics is None:
        metrics = FontMetrics()

    # Dynamic speech bubble / script message:
    # Limit is the reference English string's layout footprint
    if ref_text:
        ref_max_w, _, ref_lines = metrics.measure_text(ref_text)
        width_limit = max(ref_max_w, 40.0)
        line_limit = max(ref_lines, 1)
        return width_limit, line_limit, f"Original English Box ({int(width_limit)}px, {line_limit} lines)"

    # Default fallback when no reference exists
    return 320.0, 4, "Standard Dialogue Bubble (320px, 4 lines)"
