#!/usr/bin/env python3
"""Dark Cloud text-fit comparison tool.

Compares the game's original English text with translated text (from port/lang/*.json,
extracted game message data, and language JSON files) and evaluates whether each
string fits where the original did: same box, same font, no overflow, evenly spaced,
line count not larger.

Usage:
    python tools/textfit/textfit.py --lang es_es [--ref english] --out report.html
    python tools/textfit/textfit.py --all-langs
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, Any, List

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from tools.textfit.metrics import FontMetrics
    from tools.textfit.loader import MessageLoader, normalize_lang_name, CANONICAL_LANG_NAMES
    from tools.textfit.comparator import compare_all, ComparisonResult
    from tools.textfit.report import generate_html_report, generate_csv_report
else:
    from .metrics import FontMetrics
    from .loader import MessageLoader, normalize_lang_name, CANONICAL_LANG_NAMES
    from .comparator import compare_all, ComparisonResult
    from .report import generate_html_report, generate_csv_report


def print_summary_table(target_lang: str, results: List[ComparisonResult], ref_lang: str = "en_gb") -> None:
    """Prints a clean ASCII summary table to stdout."""
    total = len(results)
    overflows = [r for r in results if r.is_overflow]
    line_overflows = sum(1 for r in results if r.line_overflow > 0)
    width_overflows = sum(1 for r in results if r.width_overflow > 0.5)
    max_overflow = max((r.width_overflow for r in results), default=0.0)
    fit_count = total - len(overflows)
    fit_pct = (fit_count / total * 100.0) if total > 0 else 100.0

    print(f"\n=======================================================")
    print(f" Text-Fit Summary: {target_lang.upper()} vs {ref_lang.upper()}")
    print(f"=======================================================")
    print(f" Total Messages Compared : {total:,}")
    print(f" Fitting Within Box      : {fit_count:,} ({fit_pct:.1f}%)")
    print(f" Total Overflows         : {len(overflows):,} ({100.0 - fit_pct:.1f}%)")
    print(f"   Line Count Exceeded   : {line_overflows:,}")
    print(f"   Width Exceeded        : {width_overflows:,}")
    print(f" Max Width Overflow      : +{max_overflow:.1f} px")
    print(f"=======================================================\n")


def run_single_lang(
    loader: MessageLoader,
    metrics: FontMetrics,
    target_lang: str,
    ref_lang: str,
    out_html: Optional[Path] = None,
    out_csv: Optional[Path] = None,
    font_path: Optional[Path] = None,
    summary_by_lang: Optional[Dict[str, Dict[str, Any]]] = None,
    max_rows: int = 1000,
) -> List[ComparisonResult]:
    """Runs comparison for a single language against the reference."""
    pairs = loader.get_paired_messages(target_lang, ref_lang)
    results = compare_all(pairs, metrics)

    print_summary_table(target_lang, results, ref_lang)

    if out_html:
        out_html.parent.mkdir(parents=True, exist_ok=True)
        html_content = generate_html_report(
            results=results,
            target_lang=target_lang,
            ref_lang=ref_lang,
            font_path=font_path,
            summary_by_lang=summary_by_lang,
            max_table_rows=max_rows,
        )
        out_html.write_text(html_content, encoding="utf-8")
        print(f"Wrote HTML report to: {out_html}")

    if out_csv:
        out_csv.parent.mkdir(parents=True, exist_ok=True)
        csv_content = generate_csv_report(results)
        out_csv.write_text(csv_content, encoding="utf-8")
        print(f"Wrote CSV report to: {out_csv}")

    return results


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Dark Cloud text-fit comparison tool.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--lang",
        default="es_es",
        help="Target language to evaluate (es_es, fr_fr, de_de, it_it, etc.)",
    )
    parser.add_argument(
        "--ref",
        default="english",
        help="Reference language (default: english / en_gb)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        help="Output HTML report path (e.g. report.html)",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="Output CSV report path (default: <out>.csv if --out specified)",
    )
    parser.add_argument(
        "--data",
        type=Path,
        help="Path to extracted game data folder (default: auto-detected)",
    )
    parser.add_argument(
        "--json-dir",
        type=Path,
        help="Path to pre-exported language JSON folder",
    )
    parser.add_argument(
        "--port-lang-dir",
        type=Path,
        help="Path to port/lang folder",
    )
    parser.add_argument(
        "--font",
        type=Path,
        help="Path to DarkCloudCompendium.ttf font file",
    )
    parser.add_argument(
        "--all-langs",
        action="store_true",
        help="Evaluate all 4 translated languages (es_es, fr_fr, de_de, it_it) against English",
    )
    parser.add_argument(
        "--max-rows",
        type=int,
        default=1000,
        help="Maximum table rows to embed into HTML report",
    )

    args = parser.parse_args(argv)

    ref_lang = normalize_lang_name(args.ref)
    target_lang = normalize_lang_name(args.lang)

    # Initialize font metrics
    metrics = FontMetrics(font_path=args.font)

    # Initialize and load messages
    print("Loading messages from game data and language files...")
    loader = MessageLoader(
        data_dir=args.data,
        port_lang_dir=args.port_lang_dir,
        json_dir=args.json_dir,
    )
    loader.load_all()

    target_languages = ["fr_fr", "de_de", "it_it", "es_es"] if args.all_langs else [target_lang]

    # Pre-calculate summary stats across all target languages
    summary_by_lang: Dict[str, Dict[str, Any]] = {}
    for l_name in ["fr_fr", "de_de", "it_it", "es_es"]:
        pairs = loader.get_paired_messages(l_name, ref_lang)
        res = compare_all(pairs, metrics)
        overflows = [r for r in res if r.is_overflow]
        summary_by_lang[l_name] = {
            "total": len(res),
            "overflow": len(overflows),
            "line_overflow": sum(1 for r in res if r.line_overflow > 0),
            "width_overflow": sum(1 for r in res if r.width_overflow > 0.5),
        }

    # If --csv was not explicitly set but --out was, default CSV beside HTML
    out_csv = args.csv
    if out_csv is None and args.out:
        out_csv = args.out.with_suffix(".csv")

    for l_name in target_languages:
        out_h = args.out if not args.all_langs else (args.out.parent / f"report_{l_name}.html" if args.out else None)
        out_c = out_csv if not args.all_langs else (args.out.parent / f"report_{l_name}.csv" if args.out else None)
        run_single_lang(
            loader=loader,
            metrics=metrics,
            target_lang=l_name,
            ref_lang=ref_lang,
            out_html=out_h,
            out_csv=out_c,
            font_path=args.font,
            summary_by_lang=summary_by_lang,
            max_rows=args.max_rows,
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
