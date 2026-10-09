#!/usr/bin/env python3
"""Renders visual previews of text strings in Dark Cloud's font and box size (inline SVG).

Produces crisp, authentic previews displaying box bounds, overflow zones,
and side-by-side or stacked text comparison with button glyph badges.
"""

from __future__ import annotations

import base64
import html
import re
from pathlib import Path
from typing import Optional, List, Tuple

from .metrics import TOKEN_RE, GAIJI_CELL_COUNTS

# Button badge colors matching modern and retro PlayStation schemes
BUTTON_BADGES = {
    "cross": ("#2563eb", "✕"),       # Blue Cross
    "circle": ("#dc2626", "○"),      # Red Circle
    "square": ("#db2777", "□"),      # Pink Square
    "triangle": ("#16a34a", "△"),    # Green Triangle
    "l1": ("#475569", "L1"),
    "r1": ("#475569", "R1"),
    "l2": ("#334155", "L2"),
    "r2": ("#334155", "R2"),
    "select": ("#475569", "SELECT"),
    "start": ("#475569", "START"),
    "dpad": ("#334155", "✚"),
    "dpad-updown": ("#334155", "⬍"),
    "dpad-sides": ("#334155", "⬄"),
    "up": ("#334155", "▲"),
    "down": ("#334155", "▼"),
    "left": ("#334155", "◀"),
    "right": ("#334155", "▶"),
    "alert": ("#eab308", "!"),
    "heart": ("#ef4444", "♥"),
    "hand": ("#f59e0b", "☞"),
}


def load_font_base64(font_path: Optional[Path] = None) -> str:
    """Reads DarkCloudCompendium.ttf and returns base64 string for embedding."""
    if font_path is None:
        font_path = Path(__file__).resolve().parent.parent / "font" / "DarkCloudCompendium.ttf"
    if font_path.exists():
        return base64.b64encode(font_path.read_bytes()).decode("ascii")
    return ""


def parse_line_segments(line: str, char_width: float = 11.0) -> List[Tuple[str, str, float]]:
    """Breaks a line into segments: (type, content, width_px).

    type can be:
    - 'text': plain text string
    - 'button': button token (e.g. cross, square)
    - 'gap': gap spacing
    - 'format': zero-width formatting code (color, etc.)
    """
    segments: List[Tuple[str, str, float]] = []
    pos = 0

    for match in TOKEN_RE.finditer(line):
        start, end = match.span()
        if start > pos:
            text = line[pos:start]
            segments.append(("text", text, len(text) * char_width))

        matched = match.group(0)
        if matched == "{{":
            segments.append(("text", "{", char_width))
        else:
            token = match.group(1).strip().lower()
            if token in BUTTON_BADGES or token in GAIJI_CELL_COUNTS:
                cells = GAIJI_CELL_COUNTS.get(token, 2)
                segments.append(("button", token, cells * char_width))
            elif token.startswith("gap "):
                try:
                    px = float(token.split()[1])
                    segments.append(("gap", token, px))
                except Exception:
                    pass
            else:
                segments.append(("format", token, 0.0))

        pos = end

    if pos < len(line):
        text = line[pos:]
        segments.append(("text", text, len(text) * char_width))

    return segments


def render_preview_svg(
    trans_text: str,
    box_width: float,
    box_lines: int,
    ref_text: Optional[str] = None,
    char_width: float = 11.0,
    line_height: float = 22.0,
    max_preview_width: float = 460.0,
) -> str:
    """Generates an inline SVG showing text within its box bounds and highlighting overflows."""
    pad_x = 16.0
    pad_y = 12.0

    trans_lines_raw = trans_text.replace("{page}", "\n").split("\n") if trans_text else [""]
    ref_lines_raw = ref_text.replace("{page}", "\n").split("\n") if ref_text else []

    # Calculate actual width required by the preview
    max_measured_w = box_width
    for l in trans_lines_raw:
        segs = parse_line_segments(l, char_width)
        w = sum(s[2] for s in segs)
        if w > max_measured_w:
            max_measured_w = w

    svg_inner_w = max(box_width, max_measured_w)
    overflow_amount = max(0.0, max_measured_w - box_width)
    total_svg_w = max(box_width + pad_x * 2, svg_inner_w + pad_x * 2 + 10.0)

    # Height: English reference lines (if shown) + Translation lines
    trans_count = len(trans_lines_raw)
    show_ref = bool(ref_lines_raw and ref_text != trans_text)
    ref_count = len(ref_lines_raw) if show_ref else 0

    ref_block_h = (ref_count * line_height + 8.0) if show_ref else 0.0
    trans_block_h = trans_count * line_height
    box_limit_h = box_lines * line_height

    total_svg_h = pad_y * 2 + ref_block_h + max(trans_block_h, box_limit_h) + 14.0

    svg_parts: List[str] = []
    svg_parts.append(
        f'<svg class="textfit-svg" viewBox="0 0 {total_svg_w:.1f} {total_svg_h:.1f}" '
        f'width="100%" height="{total_svg_h:.1f}" '
        f'xmlns="http://www.w3.org/2000/svg">'
    )

    # Background frame
    svg_parts.append(
        f'<rect x="2" y="2" width="{total_svg_w - 4:.1f}" height="{total_svg_h - 4:.1f}" '
        f'rx="5" fill="#0d131f" stroke="#25324d" stroke-width="1.5"/>'
    )

    # Box constraint outline
    box_rect_x = pad_x
    box_rect_y = pad_y + ref_block_h
    svg_parts.append(
        f'<rect x="{box_rect_x:.1f}" y="{box_rect_y:.1f}" '
        f'width="{box_width:.1f}" height="{box_limit_h:.1f}" '
        f'rx="3" fill="#141c2c" stroke="#3b82f6" stroke-width="1" stroke-dasharray="3,3" opacity="0.6"/>'
    )

    # Overflow zone highlight if width exceeds box_width
    if overflow_amount > 0:
        over_x = box_rect_x + box_width
        over_w = total_svg_w - over_x - 4.0
        svg_parts.append(
            f'<rect x="{over_x:.1f}" y="{box_rect_y:.1f}" '
            f'width="{over_w:.1f}" height="{max(trans_block_h, box_limit_h):.1f}" '
            f'fill="rgba(239, 68, 68, 0.15)"/>'
        )
        # Limit guide line
        svg_parts.append(
            f'<line x1="{over_x:.1f}" y1="{box_rect_y - 4:.1f}" x2="{over_x:.1f}" '
            f'y2="{box_rect_y + max(trans_block_h, box_limit_h) + 4:.1f}" '
            f'stroke="#ef4444" stroke-width="1.5" stroke-dasharray="4,2"/>'
        )

    # Reference English Lines (dimmed, for side-by-side comparison)
    curr_y = pad_y + line_height * 0.75
    if show_ref:
        svg_parts.append(
            f'<text x="{pad_x:.1f}" y="{curr_y - 4:.1f}" fill="#64748b" '
            f'font-size="9" font-weight="bold" font-family="sans-serif">ORIGINAL (EN)</text>'
        )
        curr_y += 10.0
        for line in ref_lines_raw:
            curr_x = pad_x
            segs = parse_line_segments(line, char_width)
            for seg_type, content, seg_w in segs:
                if seg_type == "text":
                    escaped = html.escape(content)
                    svg_parts.append(
                        f'<text x="{curr_x:.1f}" y="{curr_y:.1f}" fill="#94a3b8" '
                        f'font-family="DarkCloudCompendium, monospace" font-size="16">{escaped}</text>'
                    )
                elif seg_type == "button":
                    badge_color, badge_sym = BUTTON_BADGES.get(content, ("#475569", content.upper()[:2]))
                    svg_parts.append(
                        f'<rect x="{curr_x:.1f}" y="{curr_y - 12:.1f}" width="{seg_w - 2:.1f}" height="14" '
                        f'rx="3" fill="{badge_color}" opacity="0.6"/>'
                        f'<text x="{curr_x + (seg_w - 2)/2:.1f}" y="{curr_y - 1:.1f}" text-anchor="middle" '
                        f'fill="#ffffff" font-size="9" font-weight="bold" font-family="sans-serif">{badge_sym}</text>'
                    )
                curr_x += seg_w
            curr_y += line_height
        curr_y += 6.0

    # Translated Lines
    svg_parts.append(
        f'<text x="{pad_x:.1f}" y="{curr_y - 4:.1f}" fill="#93c5fd" '
        f'font-size="9" font-weight="bold" font-family="sans-serif">TRANSLATION</text>'
    )
    curr_y += 10.0

    for line_idx, line in enumerate(trans_lines_raw):
        curr_x = pad_x
        segs = parse_line_segments(line, char_width)
        is_line_overflow = line_idx >= box_lines

        for seg_type, content, seg_w in segs:
            seg_end = curr_x + seg_w
            is_w_overflow = (seg_end > box_rect_x + box_width + 0.5)

            if seg_type == "text":
                escaped = html.escape(content)
                text_color = "#f87171" if (is_w_overflow or is_line_overflow) else "#ffffff"
                svg_parts.append(
                    f'<text x="{curr_x:.1f}" y="{curr_y:.1f}" fill="{text_color}" '
                    f'font-family="DarkCloudCompendium, monospace" font-size="16">{escaped}</text>'
                )
            elif seg_type == "button":
                badge_color, badge_sym = BUTTON_BADGES.get(content, ("#475569", content.upper()[:2]))
                svg_parts.append(
                    f'<rect x="{curr_x:.1f}" y="{curr_y - 13:.1f}" width="{seg_w - 2:.1f}" height="15" '
                    f'rx="3" fill="{badge_color}"/>'
                    f'<text x="{curr_x + (seg_w - 2)/2:.1f}" y="{curr_y - 1:.1f}" text-anchor="middle" '
                    f'fill="#ffffff" font-size="10" font-weight="bold" font-family="sans-serif">{badge_sym}</text>'
                )
            curr_x += seg_w

        curr_y += line_height

    # Horizontal guide for line count limit if exceeded
    if trans_count > box_lines:
        line_over_y = box_rect_y + box_limit_h
        svg_parts.append(
            f'<line x1="{pad_x - 4:.1f}" y1="{line_over_y:.1f}" x2="{total_svg_w - pad_x + 4:.1f}" '
            f'y2="{line_over_y:.1f}" stroke="#f97316" stroke-width="1.5" stroke-dasharray="3,3"/>'
            f'<text x="{total_svg_w - pad_x:.1f}" y="{line_over_y - 3:.1f}" text-anchor="end" fill="#f97316" '
            f'font-size="9" font-weight="bold" font-family="sans-serif">LINE LIMIT ({box_lines})</text>'
        )

    svg_parts.append('</svg>')
    return "".join(svg_parts)
