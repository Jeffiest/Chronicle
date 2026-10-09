#!/usr/bin/env python3
"""Unit tests and self-check for Dark Cloud textfit tool."""

import struct
import sys
import unittest
from pathlib import Path

# Ensure workspace root is in sys.path
workspace_root = Path(__file__).resolve().parent.parent.parent
if str(workspace_root) not in sys.path:
    sys.path.insert(0, str(workspace_root))

from tools.textfit.metrics import FontMetrics, resolve_box_limits
from tools.textfit.comparator import compare_message, compare_all
from tools.textfit.loader import decode_retail_mes, localize_message_key, detect_file_language
from tools.textfit.render import render_preview_svg, parse_line_segments
from tools.textfit.report import generate_csv_report, generate_html_report


class TestFontMetrics(unittest.TestCase):
    def setUp(self):
        self.metrics = FontMetrics(char_width=11.0)

    def test_plain_characters(self):
        # 3 characters at 11px each = 33px
        w = self.metrics.measure_line("ABC")
        self.assertEqual(w, 33.0)

        # Space is 11px
        w_space = self.metrics.measure_line("A B")
        self.assertEqual(w_space, 33.0)

    def test_button_token_expansion(self):
        # {cross} is 2 cells = 22px
        adv = self.metrics.get_token_advance("cross")
        self.assertEqual(adv, 22.0)

        # {square} is 2 cells = 22px
        adv_sq = self.metrics.get_token_advance("square")
        self.assertEqual(adv_sq, 22.0)

        # {select} is 8 cells = 88px
        adv_sel = self.metrics.get_token_advance("select")
        self.assertEqual(adv_sel, 88.0)

        # Combined line: {cross} OK = 22 + 11 (space) + 22 ("OK") = 55px
        line_w = self.metrics.measure_line("{cross} OK")
        self.assertEqual(line_w, 55.0)

    def test_formatting_tokens_zero_width(self):
        self.assertEqual(self.metrics.get_token_advance("/color"), 0.0)
        self.assertEqual(self.metrics.get_token_advance("white"), 0.0)
        self.assertEqual(self.metrics.get_token_advance("cyan"), 0.0)
        self.assertEqual(self.metrics.get_token_advance("wait 10"), 0.0)
        self.assertEqual(self.metrics.get_token_advance("bubble 2"), 0.0)

        # Formatting tags inside text should not add width
        w1 = self.metrics.measure_line("Hello")
        w2 = self.metrics.measure_line("{cyan}Hello{/color}")
        self.assertEqual(w1, w2)

    def test_gap_token(self):
        # {gap 14} adds exactly 14px
        self.assertEqual(self.metrics.get_token_advance("gap 14"), 14.0)

    def test_escaped_braces(self):
        # {{ escapes to single { (11px)
        w = self.metrics.measure_line("{{")
        self.assertEqual(w, 11.0)

    def test_multiline_and_pages(self):
        text = "Line 1\nLine 2 is longer{page}Page 2"
        max_w, line_widths, count = self.metrics.measure_text(text)
        self.assertEqual(count, 3)
        self.assertEqual(line_widths[0], 66.0)   # 6 chars * 11 = 66
        self.assertEqual(line_widths[1], 176.0)  # 16 chars * 11 = 176
        self.assertEqual(line_widths[2], 66.0)   # 6 chars * 11 = 66
        self.assertEqual(max_w, 176.0)


class TestBoxLimits(unittest.TestCase):
    def test_known_options_boxes(self):
        w, l, desc = resolve_box_limits("options.game.clock.label")
        self.assertEqual(w, 236.0)
        self.assertEqual(l, 1)

        w, l, desc = resolve_box_limits("options.game.clock.choice.0")
        self.assertEqual(w, 180.0)
        self.assertEqual(l, 1)

        w, l, desc = resolve_box_limits("options.shortcuts")
        self.assertEqual(w, 300.0)
        self.assertEqual(l, 3)

        w, l, desc = resolve_box_limits("options.help.exit")
        self.assertEqual(w, 270.0)
        self.assertEqual(l, 4)

    def test_known_dungeon_boxes(self):
        w, l, desc = resolve_box_limits("dun.message.ww_mes.dunmsd00.12")
        self.assertEqual(w, 165.0)
        self.assertEqual(l, 3)

    def test_fallback_reference_box(self):
        # Reference string has 2 lines, max line length 10 chars = 110px
        ref = "Short\n0123456789"
        w, l, desc = resolve_box_limits("custom.dialogue.01", ref_text=ref)
        self.assertEqual(w, 110.0)
        self.assertEqual(l, 2)


class TestComparator(unittest.TestCase):
    def setUp(self):
        self.metrics = FontMetrics(char_width=11.0)

    def test_string_fitting_safely(self):
        # Label limit is 236px, 1 line
        res = compare_message("options.game.clock.label", "Clock", "Reloj", self.metrics)
        self.assertFalse(res.is_overflow)
        self.assertEqual(res.width_overflow, 0.0)
        self.assertEqual(res.line_overflow, 0)
        self.assertEqual(res.severity, 0.0)

    def test_string_width_overflow(self):
        # Choice limit is 180px (approx 16 characters). 25 characters = 275px
        long_choice = "A Very Extremely Long Choice Text"
        res = compare_message("options.game.clock.choice.0", "Yes", long_choice, self.metrics)
        self.assertTrue(res.is_overflow)
        self.assertGreater(res.width_overflow, 0.0)
        self.assertEqual(res.line_overflow, 0)
        self.assertGreater(res.severity, 0.0)

    def test_string_line_overflow(self):
        # Label limit is 1 line. Translation has 2 lines
        two_lines = "Guardar\nCursor"
        res = compare_message("options.game.save.label", "Save Cursor", two_lines, self.metrics)
        self.assertTrue(res.is_overflow)
        self.assertEqual(res.line_overflow, 1)
        self.assertGreaterEqual(res.severity, 1000.0)

    def test_sorting_by_severity(self):
        pairs = {
            "fit": {"ref": "Yes", "trans": "Si"},
            "small_over": {"ref": "Clock", "trans": "Reloj de la pared y mesa"},  # width overflow on label
            "line_over": {"ref": "Clock", "trans": "Reloj\nDos"},  # line overflow on label
        }
        res = compare_all(pairs, self.metrics)
        # line_over has severity > 1000, should be first
        self.assertEqual(res[0].key, "line_over")
        self.assertEqual(res[1].key, "small_over")
        self.assertEqual(res[2].key, "fit")


class TestLoader(unittest.TestCase):
    def test_path_key_normalization(self):
        k1 = localize_message_key("commenu/a_spa/option.pac/allmenu.mes")
        self.assertEqual(k1, "commenu.option.pac.allmenu")

        k2 = localize_message_key("dun/message/ww_mes/dunmsd00_6.mes")
        self.assertEqual(k2, "dun.message.ww_mes.dunmsd00")

    def test_detect_file_language(self):
        self.assertEqual(detect_file_language("commenu/a_spa/option.pac"), 6)
        self.assertEqual(detect_file_language("commenu/a_eng/option.pac"), 2)
        self.assertEqual(detect_file_language("dun/message/ww_mes/dunmsd00_3.mes"), 3)
        self.assertEqual(detect_file_language("common/dungeon.mes"), -1)

    def test_decode_retail_mes_binary(self):
        # Create a mock .mes file with 1 message (id=10): "OK"
        # Word 0: count = 1
        # Word 1: pad = 0
        # Word 2: mid = 10
        # Word 3: offset = 2 (relative to base = 1 + count = 2, so start = 4)
        # Text starts at word 4:
        # 'O' = -0x2DF + 14 = -721
        # 'K' = -0x2DF + 10 = -725
        # MES_CODE_END = -255
        header = struct.pack("<hhhh", 1, 0, 10, 2)
        body = struct.pack("<hhh", -721, -725, -255)
        raw = header + body
        msgs = decode_retail_mes(raw)
        self.assertIn(10, msgs)
        self.assertEqual(msgs[10], "OK")


class TestRenderAndReport(unittest.TestCase):
    def setUp(self):
        self.metrics = FontMetrics(char_width=11.0)

    def test_render_preview_svg(self):
        svg = render_preview_svg(
            trans_text="{square} Predeterm.\n{circle} Cerrar",
            box_width=200.0,
            box_lines=2,
            ref_text="{square} Default\n{circle} Close",
        )
        self.assertIn("<svg", svg)
        self.assertIn("</svg>", svg)
        self.assertIn("Predeterm.", svg)
        self.assertIn("Default", svg)

    def test_generate_csv_and_html(self):
        res = [
            compare_message("options.shortcuts", "{square} Reset", "{square} Predeterm.", self.metrics),
            compare_message("options.game.clock.label", "Clock", "Reloj", self.metrics),
        ]
        csv_out = generate_csv_report(res)
        self.assertIn("options.shortcuts", csv_out)
        self.assertIn("options.game.clock.label", csv_out)

        html_out = generate_html_report(res, target_lang="es_es", ref_lang="en_gb")
        self.assertIn("Dark Cloud Text-Fit", html_out)
        self.assertIn("ES_ES", html_out)
        self.assertIn("options.shortcuts", html_out)


if __name__ == "__main__":
    unittest.main()
