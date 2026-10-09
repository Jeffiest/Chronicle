#!/usr/bin/env python3
"""HTML and CSV report generator for Dark Cloud text-fit comparison."""

from __future__ import annotations

import csv
import html
import io
from pathlib import Path
from typing import List, Dict, Any, Optional

from .comparator import ComparisonResult
from .render import render_preview_svg, load_font_base64


CSS_STYLES = """
:root {
    --bg-main: #0b0f19;
    --bg-card: #111827;
    --bg-card-hover: #1e293b;
    --border-color: #1f2937;
    --border-highlight: #374151;
    --text-main: #f3f4f6;
    --text-muted: #9ca3af;
    --primary: #3b82f6;
    --danger: #ef4444;
    --warning: #f59e0b;
    --success: #10b981;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    background: var(--bg-main);
    color: var(--text-main);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    line-height: 1.5;
    padding: 24px;
}
.header {
    margin-bottom: 24px;
    padding-bottom: 16px;
    border-bottom: 1px solid var(--border-color);
}
.header h1 {
    font-size: 26px;
    font-weight: 700;
    display: flex;
    align-items: center;
    gap: 12px;
}
.header p {
    color: var(--text-muted);
    font-size: 14px;
    margin-top: 4px;
}
.badge-lang {
    background: #1e3a8a;
    color: #93c5fd;
    padding: 4px 10px;
    border-radius: 6px;
    font-size: 14px;
    font-weight: 600;
}

/* Metrics grid */
.stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 16px;
    margin-bottom: 24px;
}
.stat-card {
    background: var(--bg-card);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 16px;
}
.stat-label {
    font-size: 12px;
    font-weight: 600;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}
.stat-val {
    font-size: 28px;
    font-weight: 700;
    margin-top: 4px;
}
.stat-sub {
    font-size: 12px;
    color: var(--text-muted);
    margin-top: 2px;
}
.text-green { color: var(--success); }
.text-red { color: var(--danger); }
.text-orange { color: var(--warning); }

/* Language summary row */
.lang-summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 12px;
    margin-bottom: 24px;
}
.lang-summary-card {
    background: #0f172a;
    border: 1px solid var(--border-color);
    border-radius: 6px;
    padding: 12px;
}
.lang-summary-card.active-lang {
    border-color: var(--primary);
    background: #1e293b;
}
.lang-title {
    font-size: 13px;
    font-weight: 700;
}
.lang-stat-val {
    font-size: 16px;
    font-weight: 700;
    margin-top: 2px;
}
.lang-stat-sub {
    font-size: 11px;
    color: var(--text-muted);
}

/* Controls */
.controls-bar {
    background: var(--bg-card);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 12px 16px;
    margin-bottom: 20px;
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
    align-items: center;
    justify-content: space-between;
}
.filter-buttons {
    display: flex;
    gap: 8px;
}
.filter-btn {
    background: #1f2937;
    color: var(--text-muted);
    border: 1px solid var(--border-highlight);
    padding: 6px 12px;
    border-radius: 6px;
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s;
}
.filter-btn:hover {
    background: #374151;
    color: #fff;
}
.filter-btn.active {
    background: var(--primary);
    color: #fff;
    border-color: var(--primary);
}
.search-input {
    background: #0b0f19;
    border: 1px solid var(--border-highlight);
    color: #fff;
    padding: 6px 14px;
    border-radius: 6px;
    font-size: 13px;
    width: 260px;
}
.search-input:focus {
    outline: none;
    border-color: var(--primary);
}

/* Table */
.table-wrap {
    overflow-x: auto;
    border: 1px solid var(--border-color);
    border-radius: 8px;
    background: var(--bg-card);
}
table {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
}
th {
    background: #0f172a;
    color: var(--text-muted);
    font-weight: 600;
    text-align: left;
    padding: 12px 14px;
    border-bottom: 1px solid var(--border-color);
    white-space: nowrap;
}
td {
    padding: 12px 14px;
    border-bottom: 1px solid var(--border-color);
    vertical-align: top;
}
tr:hover td {
    background: var(--bg-card-hover);
}
.col-rank { width: 40px; color: var(--text-muted); text-align: center; }
.col-key { width: 220px; }
.col-status { width: 140px; }
.col-dims { width: 150px; font-size: 12px; color: var(--text-muted); line-height: 1.6; }
.col-text { width: 320px; }
.col-preview { width: 380px; min-width: 320px; }

.key-name {
    font-family: monospace;
    font-weight: 600;
    color: #60a5fa;
    word-break: break-all;
    display: block;
}
.box-desc {
    display: block;
    font-size: 11px;
    color: var(--text-muted);
    margin-top: 3px;
}
.sev-score {
    font-size: 11px;
    color: var(--text-muted);
    margin-top: 4px;
}

/* Badges */
.badge {
    display: inline-block;
    padding: 3px 8px;
    border-radius: 4px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.3px;
}
.badge-ok { background: rgba(16, 185, 129, 0.2); color: #34d399; }
.badge-warning { background: rgba(245, 158, 11, 0.2); color: #fbbf24; }
.badge-danger { background: rgba(239, 68, 68, 0.2); color: #f87171; }

/* Text blocks */
.text-block {
    margin-bottom: 8px;
    padding: 6px 10px;
    border-radius: 4px;
    background: #0b0f19;
    border-left: 3px solid #374151;
}
.ref-block { border-left-color: #64748b; }
.trans-block { border-left-color: #3b82f6; }
.block-label {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-muted);
    text-transform: uppercase;
    display: block;
    margin-bottom: 2px;
}
.font-dc {
    font-family: 'DarkCloudCompendium', monospace;
    font-size: 14px;
    line-height: 1.3;
}

/* Footer */
.footer {
    margin-top: 24px;
    text-align: center;
    font-size: 12px;
    color: var(--text-muted);
}
"""

JS_SCRIPT = """
const filterBtns = document.querySelectorAll('.filter-btn');
const searchBox = document.getElementById('searchBox');
const rows = document.querySelectorAll('#tableBody tr');

function applyFilters() {
    const activeFilter = document.querySelector('.filter-btn.active').dataset.filter;
    const query = searchBox.value.toLowerCase().trim();

    rows.forEach(row => {
        const rowType = row.dataset.type;
        const text = row.innerText.toLowerCase();

        let matchesFilter = false;
        if (activeFilter === 'all') matchesFilter = true;
        else if (activeFilter === 'overflow') matchesFilter = (rowType !== 'fit');
        else if (activeFilter === 'line') matchesFilter = (rowType === 'line' || rowType === 'both');
        else if (activeFilter === 'width') matchesFilter = (rowType === 'width' || rowType === 'both');

        const matchesSearch = !query || text.includes(query);
        row.style.display = (matchesFilter && matchesSearch) ? '' : 'none';
    });
}

filterBtns.forEach(btn => {
    btn.addEventListener('click', () => {
        filterBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        applyFilters();
    });
});

searchBox.addEventListener('input', applyFilters);
"""


def generate_csv_report(results: List[ComparisonResult]) -> str:
    """Generates CSV text for the comparison results."""
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow([
        "key",
        "box_type",
        "box_width",
        "box_lines",
        "ref_text",
        "ref_max_width",
        "ref_lines",
        "trans_text",
        "trans_max_width",
        "trans_lines",
        "width_overflow",
        "line_overflow",
        "overflow_pct",
        "severity",
        "status",
    ])

    for r in results:
        status = "FIT"
        if r.line_overflow > 0 and r.width_overflow > 0.5:
            status = "LINE+WIDTH_OVERFLOW"
        elif r.line_overflow > 0:
            status = "LINE_OVERFLOW"
        elif r.width_overflow > 0.5:
            status = "WIDTH_OVERFLOW"

        writer.writerow([
            r.key,
            r.box_type,
            f"{r.box_width:.1f}",
            r.box_lines,
            r.ref_text,
            f"{r.ref_max_width:.1f}",
            r.ref_line_count,
            r.trans_text,
            f"{r.trans_max_width:.1f}",
            r.trans_line_count,
            f"{r.width_overflow:.1f}",
            r.line_overflow,
            f"{r.width_overflow_pct:.1f}%",
            f"{r.severity:.1f}",
            status,
        ])

    return out.getvalue()


def generate_html_report(
    results: List[ComparisonResult],
    target_lang: str,
    ref_lang: str = "en_gb",
    font_path: Optional[Path] = None,
    summary_by_lang: Optional[Dict[str, Dict[str, Any]]] = None,
    max_table_rows: int = 500,
) -> str:
    """Builds a complete, self-contained HTML report with embedded font and inline SVGs."""
    font_b64 = load_font_base64(font_path)

    total_count = len(results)
    overflow_results = [r for r in results if r.is_overflow]
    overflow_count = len(overflow_results)
    fit_count = total_count - overflow_count

    line_overflow_count = sum(1 for r in results if r.line_overflow > 0)
    width_overflow_count = sum(1 for r in results if r.width_overflow > 0.5)
    max_w_overflow = max((r.width_overflow for r in results), default=0.0)

    fit_pct = (fit_count / total_count * 100.0) if total_count > 0 else 100.0
    over_pct = (overflow_count / total_count * 100.0) if total_count > 0 else 0.0

    # Build language summary cards if provided
    lang_summary_html = ""
    if summary_by_lang:
        cards: List[str] = []
        for l_name, stats in summary_by_lang.items():
            l_tot = stats.get("total", 0)
            l_over = stats.get("overflow", 0)
            l_line = stats.get("line_overflow", 0)
            active_class = " active-lang" if l_name == target_lang else ""
            cards.append(f"""
                <div class="lang-summary-card{active_class}">
                    <div class="lang-title">{html.escape(l_name.upper())}</div>
                    <div class="lang-stat-val text-red">{l_over:,} overflows</div>
                    <div class="lang-stat-sub">{l_line:,} line overflows / {l_tot:,} total</div>
                </div>
            """)
        lang_summary_html = f"""
            <div class="lang-summary-grid">
                {''.join(cards)}
            </div>
        """

    # Build table rows (show worst overflows first)
    rows_html: List[str] = []
    display_results = results[:max_table_rows]

    for idx, r in enumerate(display_results):
        badge_class = "badge-ok"
        badge_text = "FIT"
        if r.line_overflow > 0 and r.width_overflow > 0.5:
            badge_class = "badge-danger"
            badge_text = f"OVERFLOW (+{r.line_overflow}L, +{r.width_overflow:.0f}px)"
        elif r.line_overflow > 0:
            badge_class = "badge-danger"
            badge_text = f"LINE OVERFLOW (+{r.line_overflow}L)"
        elif r.width_overflow > 0.5:
            badge_class = "badge-warning"
            badge_text = f"WIDTH OVERFLOW (+{r.width_overflow:.0f}px, +{r.width_overflow_pct:.0f}%)"

        ref_escaped = html.escape(r.ref_text).replace("\n", "<br>")
        trans_escaped = html.escape(r.trans_text).replace("\n", "<br>")

        svg_preview = render_preview_svg(
            trans_text=r.trans_text,
            box_width=r.box_width,
            box_lines=r.box_lines,
            ref_text=r.ref_text,
        )

        row_class = "overflow-row" if r.is_overflow else "fit-row"
        data_type = "both" if (r.line_overflow > 0 and r.width_overflow > 0.5) else \
                    "line" if r.line_overflow > 0 else \
                    "width" if r.width_overflow > 0.5 else "fit"

        rows_html.append(f"""
            <tr class="{row_class}" data-type="{data_type}">
                <td class="col-rank">{idx + 1}</td>
                <td class="col-key">
                    <span class="key-name">{html.escape(r.key)}</span>
                    <span class="box-desc">{html.escape(r.box_type)}</span>
                </td>
                <td class="col-status">
                    <span class="badge {badge_class}">{badge_text}</span>
                    <div class="sev-score">Severity: {r.severity:.1f}</div>
                </td>
                <td class="col-dims">
                    <div><b>Trans:</b> {r.trans_max_width:.0f}px / {r.trans_line_count}L</div>
                    <div><b>Box:</b> {r.box_width:.0f}px / {r.box_lines}L</div>
                    <div><b>Ref:</b> {r.ref_max_width:.0f}px / {r.ref_line_count}L</div>
                </td>
                <td class="col-text">
                    <div class="text-block ref-block">
                        <span class="block-label">English:</span>
                        <div class="text-content font-dc">{ref_escaped}</div>
                    </div>
                    <div class="text-block trans-block">
                        <span class="block-label">{html.escape(target_lang.upper())}:</span>
                        <div class="text-content font-dc">{trans_escaped}</div>
                    </div>
                </td>
                <td class="col-preview">
                    {svg_preview}
                </td>
            </tr>
        """)

    font_face_css = ""
    if font_b64:
        font_face_css = f"""
            @font-face {{
                font-family: 'DarkCloudCompendium';
                src: url('data:font/truetype;charset=utf-8;base64,{font_b64}') format('truetype');
                font-weight: normal;
                font-style: normal;
            }}
        """

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>Dark Cloud Text-Fit Report - {html.escape(target_lang.upper())}</title>
    <style>
        {font_face_css}
        {CSS_STYLES}
    </style>
</head>
<body>
    <div class="header">
        <h1>
            Dark Cloud Text-Fit Comparison
            <span class="badge-lang">{html.escape(target_lang.upper())} vs {html.escape(ref_lang.upper())}</span>
        </h1>
        <p>Evaluates measured width and line counts with the authentic Dark Cloud font against original boxes.</p>
    </div>

    <div class="stats-grid">
        <div class="stat-card">
            <div class="stat-label">Total Messages</div>
            <div class="stat-val">{total_count:,}</div>
            <div class="stat-sub">Compared keys</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Fitting Within Box</div>
            <div class="stat-val text-green">{fit_count:,}</div>
            <div class="stat-sub">{fit_pct:.1f}% fit safely</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Total Overflows</div>
            <div class="stat-val text-red">{overflow_count:,}</div>
            <div class="stat-sub">{over_pct:.1f}% exceed box</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Line Count Exceeded</div>
            <div class="stat-val text-orange">{line_overflow_count:,}</div>
            <div class="stat-sub">More lines than box/ref</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Max Pixel Overflow</div>
            <div class="stat-val text-red">+{max_w_overflow:.0f}px</div>
            <div class="stat-sub">Worst width overflow</div>
        </div>
    </div>

    {lang_summary_html}

    <div class="controls-bar">
        <div class="filter-buttons">
            <button class="filter-btn active" data-filter="all">All ({total_count:,})</button>
            <button class="filter-btn" data-filter="overflow">Overflows Only ({overflow_count:,})</button>
            <button class="filter-btn" data-filter="line">Line Overflows ({line_overflow_count:,})</button>
            <button class="filter-btn" data-filter="width">Width Overflows ({width_overflow_count:,})</button>
        </div>
        <input type="text" class="search-input" id="searchBox" placeholder="Filter by key or string...">
    </div>

    <div class="table-wrap">
        <table>
            <thead>
                <tr>
                    <th class="col-rank">#</th>
                    <th class="col-key">Message Key / Box</th>
                    <th class="col-status">Status & Severity</th>
                    <th class="col-dims">Dimensions</th>
                    <th class="col-text">Side-by-Side Strings</th>
                    <th class="col-preview">Font Render Preview</th>
                </tr>
            </thead>
            <tbody id="tableBody">
                {''.join(rows_html)}
            </tbody>
        </table>
    </div>

    <div class="footer">
        Generated by Chronicle textfit tool &bull; Font: Dark Cloud Compendium
    </div>

    <script>
        {JS_SCRIPT}
    </script>
</body>
</html>
"""
