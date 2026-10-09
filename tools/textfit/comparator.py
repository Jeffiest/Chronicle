#!/usr/bin/env python3
"""Text-fit comparison and severity calculation for Dark Cloud messages."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional

from .metrics import FontMetrics, resolve_box_limits


@dataclass
class ComparisonResult:
    """Stores measurement and overflow details for a single message across languages."""
    key: str
    ref_text: str
    trans_text: str

    # Reference metrics
    ref_max_width: float
    ref_line_widths: List[float]
    ref_line_count: int

    # Translated metrics
    trans_max_width: float
    trans_line_widths: List[float]
    trans_line_count: int

    # Box boundaries
    box_width: float
    box_lines: int
    box_type: str

    # Overflows
    width_overflow: float  # pixels past box_width
    line_overflow: int     # lines past box_lines
    width_overflow_pct: float  # percentage overflow

    # Severity score for ranking
    severity: float
    is_overflow: bool


def compare_message(
    key: str,
    ref_text: str,
    trans_text: str,
    metrics: FontMetrics,
) -> ComparisonResult:
    """Measures reference and translated text and computes overflow severity."""
    # Measure reference
    ref_max_w, ref_widths, ref_lines = metrics.measure_text(ref_text)

    # Measure translated
    trans_max_w, trans_widths, trans_lines = metrics.measure_text(trans_text)

    # Resolve box boundaries
    box_width, box_lines, box_type = resolve_box_limits(key, ref_text, metrics)

    # Calculate overflow
    w_overflow = max(0.0, trans_max_w - box_width)
    l_overflow = max(0, trans_lines - box_lines)
    w_overflow_pct = (w_overflow / box_width * 100.0) if box_width > 0 else 0.0

    # Severity formula:
    # 1. Line overflow (extra lines overflowing UI panes) has highest weight (1000 per extra line)
    # 2. Pixel width overflow adds direct pixel value
    # 3. Relative overflow percentage breaks ties
    severity = (l_overflow * 1000.0) + w_overflow + (w_overflow_pct * 1.5)

    is_overflow = (w_overflow > 0.5) or (l_overflow > 0)

    return ComparisonResult(
        key=key,
        ref_text=ref_text,
        trans_text=trans_text,
        ref_max_width=ref_max_w,
        ref_line_widths=ref_widths,
        ref_line_count=ref_lines,
        trans_max_width=trans_max_w,
        trans_line_widths=trans_widths,
        trans_line_count=trans_lines,
        box_width=box_width,
        box_lines=box_lines,
        box_type=box_type,
        width_overflow=w_overflow,
        line_overflow=l_overflow,
        width_overflow_pct=w_overflow_pct,
        severity=severity,
        is_overflow=is_overflow,
    )


def compare_all(
    pairs: Dict[str, Dict[str, Any]],
    metrics: Optional[FontMetrics] = None,
    only_overflows: bool = False,
) -> List[ComparisonResult]:
    """Compares all paired messages, returned sorted by severity descending."""
    if metrics is None:
        metrics = FontMetrics()

    results: List[ComparisonResult] = []

    for key, data in pairs.items():
        ref_text = data.get("ref", "")
        trans_text = data.get("trans", "")

        # Skip completely empty or missing translations
        if not trans_text:
            continue

        comp = compare_message(key, ref_text, trans_text, metrics)
        if not only_overflows or comp.is_overflow:
            results.append(comp)

    # Sort descending by severity (worst overflows first), then key
    results.sort(key=lambda r: (-r.severity, r.key))
    return results
