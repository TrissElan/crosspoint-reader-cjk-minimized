#!/usr/bin/env python3
"""
Generate Pretendard JP 2-bit epdfont files (12pt, 14pt) for Flash embedding.

Covers:
  - Basic Latin, Latin-1, Latin Extended-A, Cyrillic, Punctuation, Arrows, Math
  - Hangul Syllables: Adobe KR-0 Supplement 0 (2,780 from kr0_hangul.txt)
  - Hangul Compatibility Jamo (consonants only: ㄱ-ㅎ, ㄲㄸㅃㅆㅉ = 19 chars)
  - CJK Symbols and Punctuation
  - Hiragana + Katakana
  - CJK Unified Ideographs: 한문교육용 기초한자 (~1,800 from edu_hanja_1800.txt)

Usage:
    python generate_pretendard_font.py          # all sizes
    python generate_pretendard_font.py 14       # single size
    python generate_pretendard_font.py 12       # single size

Output:
    ../builtinFonts/pretendard_{size}.epdfont
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ttf_to_epdfont import convert_ttf_to_epdfont  # noqa: E402

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_FILE = os.path.join(
    SCRIPT_DIR, "..", "builtinFonts", "source", "PretendardJP-Medium.otf"
)
BUILTIN_DIR = os.path.join(SCRIPT_DIR, "..", "builtinFonts")

SIZES = [10, 12, 14]


def load_codepoints(filename):
    """Load codepoints from a file (one 0xXXXX or U+XXXX per line) and return sorted list."""
    path = os.path.join(SCRIPT_DIR, filename)
    codepoints = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("0x") or line.startswith("0X"):
                codepoints.append(int(line, 16))
            elif line.startswith("U+"):
                codepoints.append(int(line[2:], 16))
    return sorted(codepoints)


def codepoints_to_intervals(codepoints):
    """Convert sorted codepoint list to "0xSTART,0xEND" interval strings."""
    if not codepoints:
        return []
    intervals = []
    start = codepoints[0]
    end = codepoints[0]
    for cp in codepoints[1:]:
        if cp == end + 1:
            end = cp
        else:
            intervals.append(f"0x{start:04X},0x{end:04X}")
            start = cp
            end = cp
    intervals.append(f"0x{start:04X},0x{end:04X}")
    return intervals


# Load codepoint files
KR0_HANGUL = load_codepoints("kr0_hangul.txt")
EDU_HANJA = load_codepoints("edu_hanja_1800.txt")

# Additional Unicode intervals beyond the converter's defaults.
# The converter already includes Basic Latin / Latin-1 / Latin Extended-A /
# Cyrillic / Punctuation / Arrows / Math / Currency by default.
INTERVALS = [
    "0x3000,0x303F",   # CJK Symbols and Punctuation
    "0x3040,0x309F",   # Hiragana
    "0x30A0,0x30FF",   # Katakana
    "0x31F0,0x31FF",   # Katakana Phonetic Extensions
    "0x3131,0x314E",   # Hangul Compatibility Jamo (consonants area, 30 codepoints)
] + codepoints_to_intervals(KR0_HANGUL) + codepoints_to_intervals(EDU_HANJA)


def main():
    if not os.path.exists(FONT_FILE):
        print(f"ERROR: Font file not found: {FONT_FILE}", file=sys.stderr)
        sys.exit(1)

    sizes = SIZES
    if len(sys.argv) > 1:
        sizes = [int(s) for s in sys.argv[1:]]

    total_bytes = 0
    for size in sizes:
        output = os.path.join(BUILTIN_DIR, f"pretendard_{size}.epdfont")
        print(f"Font   : {os.path.basename(FONT_FILE)}")
        print(f"Size   : {size}pt")
        print(f"Mode   : 2-bit greyscale")
        print(f"Output : {output}")
        print()

        t0 = time.time()
        convert_ttf_to_epdfont(
            font_files=[FONT_FILE],
            font_name="Pretendard",
            size=size,
            output_path=output,
            additional_intervals=INTERVALS,
            is_2bit=True,
        )
        elapsed = time.time() - t0

        if os.path.exists(output):
            fsize = os.path.getsize(output)
            total_bytes += fsize
            print(f"Done in {elapsed:.1f}s — {fsize:,} bytes ({fsize/1024/1024:.2f} MB)\n")
        else:
            print(f"ERROR: Output file was not created!", file=sys.stderr)
            sys.exit(1)

    if len(sizes) > 1:
        print(f"Total: {total_bytes:,} bytes ({total_bytes/1024/1024:.2f} MB)")


if __name__ == "__main__":
    main()
