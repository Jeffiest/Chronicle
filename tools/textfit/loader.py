#!/usr/bin/env python3
"""Message loader and language pairing for Dark Cloud.

Pairs every message ID/string across languages against the reference language (English).
Loads from:
1. port/lang/<language>.json (the port's translated Options strings)
2. Extracted retail data (dcdata extract): loose .mes files and packs (.pak, .pac)
3. Pre-exported JSON files (e.g. lang-export/*.json or dark-cloud-spanish-english-json)
"""

from __future__ import annotations

import json
import os
import struct
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

# Language definitions matching port/src/localize.cpp
LANGUAGE_CODES: Dict[str, int] = {
    "ja_jp": 0, "japanese": 0, "ja": 0,
    "en_us": 1, "us": 1,
    "en_gb": 2, "english": 2, "en": 2, "ref": 2,
    "fr_fr": 3, "francais": 3, "french": 3, "fr": 3,
    "de_de": 4, "deutsch": 4, "german": 4, "de": 4,
    "it_it": 5, "italiano": 5, "italian": 5, "it": 5,
    "es_es": 6, "espanol": 6, "spanish": 6, "es": 6,
}

CANONICAL_LANG_NAMES: Dict[int, str] = {
    0: "ja_jp",
    1: "en_us",
    2: "en_gb",
    3: "fr_fr",
    4: "de_de",
    5: "it_it",
    6: "es_es",
}

LANGUAGE_DIRS: List[Tuple[str, int]] = [
    ("a_jpn", 0),
    ("a_usa", 1),
    ("a_eng", 2),
    ("a_fre", 3),
    ("a_ger", 4),
    ("a_ita", 5),
    ("a_spa", 6),
]

# Character grid and extras from port/src/gametext.cpp
GRID = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'=\"!?#&+-*/%()@|<>{}[]:,.$0123456789"
GRID_FIRST = -0x2DF  # -735

EXTRAS: Dict[int, str] = {
    -0x287: "œ", -0x286: "¡", -0x285: "¿", -0x284: "Ä", -0x283: "Ç", -0x282: "È",
    -0x281: "É", -0x280: "Ö", -0x27F: "Ü", -0x27E: "ß", -0x27D: "à", -0x27C: "á",
    -0x27B: "â", -0x27A: "ä", -0x279: "ç", -0x278: "è", -0x277: "é", -0x276: "ê",
    -0x275: "ë", -0x274: "ì", -0x273: "í", -0x272: "î", -0x271: "ï", -0x270: "ñ",
    -0x26F: "ò", -0x26E: "ó", -0x26D: "ô", -0x26C: "ö", -0x26B: "ù", -0x26A: "ú",
    -0x269: "û", -0x268: "ü", -0x267: "Ú", -0x266: "Á", -0x265: "Œ", -0x264: "Ó",
    -0x260: "À", -0x25F: "Â", -0x25E: "Ï", -0x25D: "Í", -0x25C: "Ì", -0x25B: "Î",
    -0x25A: "Ù", -0x259: "Û", -0x258: "Ë", -0x257: "Ê", -0x256: "Ò", -0x255: "Ô",
    -0x254: "Ñ",
}

NAMED_CODES: Dict[int, str] = {
    -0xFD: "page",
    -0x400: "/color",
    -0x3FF: "white",
    -0x3FE: "yellow",
    -0x3FD: "cyan",
    -0x3FC: "green",
    -0x3FB: "dark",
    -0x3FA: "gold",
    -0x3F9: "grey",
    -0x301: "highlight",
    -0x401: "value",
    -0x300: "select",
    -0x2FF: "start",
    -0x2FE: "L1",
    -0x2FD: "R1",
    -0x2FC: "L2",
    -0x2FB: "R2",
    -0x2FA: "circle",
    -0x2F9: "triangle",
    -0x2F8: "cross",
    -0x2F7: "square",
    -0x2F6: "dpad",
    -0x2F5: "dpad-updown",
    -0x2F4: "dpad-sides",
    -0x2F3: "red-slash",
    -0x2F2: "heart",
    -0x2F1: "note",
    -0x2F0: "red-x",
    -0x2EF: "yellow-arrow",
    -0x2EE: "icon-sword",
    -0x2ED: "icon-orb",
    -0x2EC: "icon-georama",
    -0x2EB: "icon-jar",
    -0x2EA: "orange-arrow",
    -0x2E9: "bait",
    -0x2E8: "word-button",
    -0x2E7: "monster",
    -0x2E6: "alert",
    -0x2E5: "up",
    -0x2E4: "right",
    -0x2E3: "down",
    -0x2E2: "left",
    -0x2E1: "hand",
}

# Default English Options strings from port/src/options/screen.cpp & rows.cpp
DEFAULT_ENGLISH_OPTIONS: Dict[str, str] = {
    "options.shortcuts": "{square} Reset tab\n{triangle} Undo changes\n{circle} Close",
    "options.help.save_failed": "Save failed.\nChanges are applied.\nClose to retry writing\nconfig.json.",
    "options.help.display_kept": "Screen mode could not\nbe set and was kept.\nTry another mode or\nresolution.",
    "options.help.turn_page": "Turn tab",
    "options.help.exit": "{cross} Close\n{square} Tab defaults\n{triangle} Undo all\n{circle} Close (all rows)",
    "options.help.display_now": "Screen mode could not\nbe set. Now is\n%1",
    "options.display.fullscreen_now": "fullscreen.",
    "options.display.window_now": "a window of\n%1 x %2.",
    "options.page.game": "Game",
    "options.page.display": "Display",
    "options.page.audio": "Audio",
    "options.page.controls": "Controls",
}


def normalize_lang_name(name: str) -> str:
    """Normalizes any language string to its canonical form (e.g. 'spanish' -> 'es_es')."""
    lower = name.strip().lower()
    if lower in LANGUAGE_CODES:
        return CANONICAL_LANG_NAMES[LANGUAGE_CODES[lower]]
    return lower


def decode_control_name(code: int) -> str:
    """Decodes a single PS2 message code to its token name."""
    if code in NAMED_CODES:
        return NAMED_CODES[code]

    # Families
    if -0x200 <= code <= -0x101:
        return f"wait {code - (-0x200)}"
    if -0x3FF <= code <= -0x302:
        return f"color {code - (-0x3FF) + 2}"
    if -0x700 <= code <= -0x601:
        return f"gap {code - (-0x700)}"
    if -0x800 <= code <= -0x701:
        return f"spacing {code - (-0x800)}"
    if -0x900 <= code <= -0x801:
        return f"justify {code - (-0x900)}"
    if -0xA00 <= code <= -0x901:
        return f"bubble {code - (-0xA00)}"
    if -0x300 <= code <= -0x2E0:
        return f"icon {code - (-0x300)}"
    if -0x506 <= code <= -0x501:
        return f"name{-0x500 - code}"
    if -0x40D <= code <= -0x406:
        return f"value{-0x406 - code + 1}"

    # Insert stretch
    if -0x405 <= code <= -0x402:
        return f"insert{-0x402 - code + 1}"
    if -0x413 <= code <= -0x40E:
        return f"insert{4 + (-0x40E - code) + 1}"

    return str(code)


def decode_retail_mes(data: bytes) -> Dict[int, str]:
    """Decodes a binary .mes file into a dict of {message_id: text}."""
    if len(data) < 4:
        return {}

    words = struct.unpack(f"<{len(data) // 2}h", data[:(len(data) // 2) * 2])
    count = words[0]
    if count <= 0 or 2 + count * 2 > len(words):
        return {}

    base = 1 + count
    messages: Dict[int, str] = {}

    for i in range(count):
        mid = words[2 + i * 2]
        offset = words[3 + i * 2]
        start = base + offset

        if start < 0 or start >= len(words):
            continue

        chars: List[str] = []
        at = start
        while at < len(words) and words[at] != -0xFF:  # MES_CODE_END
            code = words[at]
            at += 1

            if code == -0xFE:  # MES_CODE_SPACE
                chars.append(" ")
            elif code == -0x100:  # MES_CODE_NEWLINE
                chars.append("\n")
            elif GRID_FIRST <= code < GRID_FIRST + len(GRID):
                ch = GRID[code - GRID_FIRST]
                chars.append("{{" if ch == "{" else ch)
            elif code in EXTRAS:
                chars.append(EXTRAS[code])
            else:
                chars.append("{" + decode_control_name(code) + "}")

        if at >= len(words):
            continue  # Reached EOF without MES_CODE_END termination, skip

        text = "".join(chars)
        # Skip dummy/padding entries containing only {0}
        if text.startswith("{0}") and set(text) == {"{", "0", "}"}:
            continue

        messages[mid] = text

    return messages


def unpack_pack_file(data: bytes) -> List[Tuple[str, bytes]]:
    """Unpacks all .mes entries from a .pak or .pac archive."""
    found: List[Tuple[str, bytes]] = []
    at = 0
    while at + 76 <= len(data):
        if data[at] == 0:
            break
        name_bytes = data[at:at + 64]
        name = name_bytes.split(b"\x00", 1)[0].decode("latin-1", "replace")
        offset, size, next_entry = struct.unpack("<iii", data[at + 64:at + 76])
        if offset < 0 or size < 0 or next_entry <= 0 or at + offset + size > len(data):
            break
        if name.lower().endswith(".mes"):
            found.append((name, data[at + offset:at + offset + size]))
        at += next_entry
    return found


def localize_message_key(path: str) -> str:
    """Normalizes a file path to its canonical dotted message key prefix."""
    key = path.replace("\\", "/").lower()
    if ":" in key:
        key = key.split(":", 1)[1]
    key = key.lstrip("/")

    # Remove per-language menu folders (a_eng/, a_spa/, etc.)
    for d, _ in LANGUAGE_DIRS:
        folder = d + "/"
        if key.startswith(folder):
            key = key[len(folder):]
        key = key.replace("/" + folder, "/")

    # Strip .mes and trailing language suffix (_0 to _6)
    if key.endswith(".mes"):
        key = key[:-4]
        if len(key) >= 2 and key[-2] == "_" and "0" <= key[-1] <= "6":
            key = key[:-2]

    return key.replace("/", ".")


def detect_file_language(path: str) -> int:
    """Detects the language code (0-6) of a file, or -1 if shared/unspecified."""
    lower = path.replace("\\", "/").lower()
    for d, code in LANGUAGE_DIRS:
        if f"/{d}/" in lower or lower.startswith(f"{d}/"):
            return code

    stem = Path(lower).stem
    if len(stem) >= 2 and stem[-2] == "_" and "0" <= stem[-1] <= "6":
        return int(stem[-1])

    return -1


class MessageLoader:
    """Discovers, decodes, and pairs message strings across languages."""

    def __init__(
        self,
        data_dir: Optional[Path] = None,
        port_lang_dir: Optional[Path] = None,
        json_dir: Optional[Path] = None,
    ):
        self.data_dir = data_dir
        self.port_lang_dir = port_lang_dir
        self.json_dir = json_dir

        # Auto-detect default directories if not provided
        self._auto_detect_paths()

        # {lang_code: {full_key: text}}
        self.languages: Dict[int, Dict[str, str]] = {c: {} for c in range(7)}
        self.shared: Dict[str, str] = {}

    def _auto_detect_paths(self) -> None:
        workspace_root = Path(__file__).resolve().parent.parent.parent

        if self.data_dir is None:
            candidates = [
                Path(r"E:\Chronicle-clean-root\data"),
                workspace_root.parent / "Chronicle-clean-root" / "data",
                workspace_root / "data",
            ]
            for cand in candidates:
                if cand.exists() and (cand / "languages.json").exists():
                    self.data_dir = cand
                    break

        if self.port_lang_dir is None:
            cand = workspace_root / "port" / "lang"
            if cand.exists():
                self.port_lang_dir = cand

        if self.json_dir is None:
            candidates = [
                Path(r"E:\Chronicle-project-Claude\prs\zip_temp\game-text"),
                Path(r"E:\Chronicle-project-Claude\prs\zip_temp"),
                workspace_root / "lang-export",
            ]
            for cand in candidates:
                if cand.exists():
                    self.json_dir = cand
                    break

    def load_all(self) -> None:
        """Loads strings from all available sources and merges them."""
        # 1. Base English options defaults
        for k, v in DEFAULT_ENGLISH_OPTIONS.items():
            self.languages[2][k] = v

        # 2. Extracted retail game data (.mes and .pak/.pac)
        if self.data_dir and self.data_dir.exists():
            self._load_from_data_dir(self.data_dir)

        # 3. Pre-exported JSON files
        if self.json_dir and self.json_dir.exists():
            self._load_from_json_dir(self.json_dir)

        # 4. Port language overrides (port/lang/*.json wins for Options screen)
        if self.port_lang_dir and self.port_lang_dir.exists():
            self._load_from_port_lang(self.port_lang_dir)

    def _load_from_data_dir(self, data_dir: Path) -> None:
        """Walks the data directory and extracts all messages."""
        for root_str, _, files in os.walk(data_dir):
            root_path = Path(root_str)
            for file_name in files:
                lower = file_name.lower()
                full_path = root_path / file_name
                rel_path = full_path.relative_to(data_dir).as_posix()

                if lower.endswith(".mes"):
                    try:
                        data = full_path.read_bytes()
                        msgs = decode_retail_mes(data)
                        lang_code = detect_file_language(rel_path)
                        key_prefix = localize_message_key(rel_path)
                        for mid, text in msgs.items():
                            full_key = f"{key_prefix}.{mid}"
                            if lang_code >= 0:
                                self.languages[lang_code][full_key] = text
                            else:
                                self.shared[full_key] = text
                    except Exception:
                        pass

                elif lower.endswith(".pak") or lower.endswith(".pac"):
                    try:
                        data = full_path.read_bytes()
                        entries = unpack_pack_file(data)
                        for entry_name, mes_data in entries:
                            entry_rel = f"{rel_path}/{entry_name}"
                            msgs = decode_retail_mes(mes_data)
                            lang_code = detect_file_language(entry_rel)
                            key_prefix = localize_message_key(entry_rel)
                            for mid, text in msgs.items():
                                full_key = f"{key_prefix}.{mid}"
                                if lang_code >= 0:
                                    self.languages[lang_code][full_key] = text
                                else:
                                    self.shared[full_key] = text
                    except Exception:
                        pass

    def _load_from_json_dir(self, json_dir: Path) -> None:
        """Loads any language JSON files found in a directory."""
        for json_file in json_dir.glob("*.json"):
            name = json_file.stem.lower()
            if name in LANGUAGE_CODES:
                lang_code = LANGUAGE_CODES[name]
                try:
                    data = json.loads(json_file.read_text(encoding="utf-8"))
                    if isinstance(data, dict):
                        for k, v in data.items():
                            if isinstance(v, str):
                                self.languages[lang_code][k] = v
                except Exception:
                    pass

    def _load_from_port_lang(self, port_lang_dir: Path) -> None:
        """Loads port/lang/*.json strings."""
        for json_file in port_lang_dir.glob("*.json"):
            name = json_file.stem.lower()
            if name in LANGUAGE_CODES:
                lang_code = LANGUAGE_CODES[name]
                try:
                    data = json.loads(json_file.read_text(encoding="utf-8"))
                    if isinstance(data, dict):
                        for k, v in data.items():
                            if isinstance(v, str):
                                self.languages[lang_code][k] = v
                except Exception:
                    pass

    def get_paired_messages(
        self,
        target_lang: str,
        ref_lang: str = "en_gb",
    ) -> Dict[str, Dict[str, Any]]:
        """Returns paired messages {key: {'ref': en_text, 'trans': target_text, 'key': key}}."""
        target_code = LANGUAGE_CODES.get(normalize_lang_name(target_lang), 6)
        ref_code = LANGUAGE_CODES.get(normalize_lang_name(ref_lang), 2)

        target_dict = dict(self.shared)
        target_dict.update(self.languages[target_code])

        ref_dict = dict(self.shared)
        ref_dict.update(self.languages[ref_code])

        all_keys = sorted(set(ref_dict.keys()) | set(target_dict.keys()))
        pairs: Dict[str, Dict[str, Any]] = {}

        for k in all_keys:
            ref_text = ref_dict.get(k)
            trans_text = target_dict.get(k)
            # Only include entries where translated text is present and differs or requires evaluation
            if trans_text is not None:
                pairs[k] = {
                    "key": k,
                    "ref": ref_text or "",
                    "trans": trans_text,
                }

        return pairs
