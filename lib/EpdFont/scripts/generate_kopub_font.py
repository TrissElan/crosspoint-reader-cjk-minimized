#!/usr/bin/env python3
"""
Generate KoPubWorld Dotum Medium 2-bit epdfont files (12pt, 14pt) for Flash embedding.

Same glyph coverage as Pretendard (see generate_pretendard_font.py).

Usage:
    python generate_kopub_font.py          # all sizes
    python generate_kopub_font.py 14       # single size

Output:
    ../builtinFonts/kopub_{size}.epdfont
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate_pretendard_font import INTERVALS  # noqa: E402
from ttf_to_epdfont import convert_ttf_to_epdfont  # noqa: E402

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_FILE = os.path.join(
    SCRIPT_DIR, "..", "builtinFonts", "source", "KoPubWorld Dotum_Pro Medium.otf"
)
BUILTIN_DIR = os.path.join(SCRIPT_DIR, "..", "builtinFonts")

SIZES = [10, 12, 14]


def main():
    if not os.path.exists(FONT_FILE):
        print(f"ERROR: Font file not found: {FONT_FILE}", file=sys.stderr)
        sys.exit(1)

    sizes = SIZES
    if len(sys.argv) > 1:
        sizes = [int(s) for s in sys.argv[1:]]

    total_bytes = 0
    for size in sizes:
        output = os.path.join(BUILTIN_DIR, f"kopub_{size}.epdfont")
        print(f"Font   : {os.path.basename(FONT_FILE)}")
        print(f"Size   : {size}pt")
        print(f"Mode   : 2-bit greyscale")
        print(f"Output : {output}")
        print()

        t0 = time.time()
        convert_ttf_to_epdfont(
            font_files=[FONT_FILE],
            font_name="KoPubDotum",
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
