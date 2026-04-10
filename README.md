# CrossPoint Reader - CJK Fork

A CJK (Korean/Japanese) font support fork of the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware for the Xteink X4 e-paper reader.

## Features

- **Pretendard JP 10/12/14pt** + **KoPub Dotum 10/12/14pt** embedded in Flash (2-bit antialiased, zero-copy)
- **Reduced character set**: Adobe KR-0 hangul (2,780) + education hanja (1,823) + Hiragana/Katakana + Latin/Cyrillic
- **WiFi file transfer**: built-in hotspot with web-based file browser (upload, delete, rename, move)
- Font selection UI — choose between 6 built-in fonts
- Flash font zero-copy optimization (~51KB RAM saved)
- Arena-based glyph bitmap cache (32KB, lazy allocation)

## Changes from Upstream

- **Font system overhaul**: Removed Bookerly font family (10/12/14/16/18pt × 4 styles), replaced with Pretendard JP Medium 10/12/14pt + KoPub Dotum Pro Medium 10/12/14pt embedded in Flash (2-bit greyscale, zero-copy)
- **SdFont/SdFontFamily**: On-demand glyph loading system with arena-based bitmap cache
- **Arena allocator**: 32KB bump-pointer arena with 512-entry open-addressing hash table replaces STL LRU cache, eliminating heap fragmentation on ESP32-C3 (229KB usable DRAM)
- **FontSelectionActivity**: UI for selecting between 6 built-in fonts
- **WiFi file transfer**: Restored from upstream with simplified hotspot-only mode (no Join Network)
- **Bug fixes**:
  - Settings Back button event leak to reader
  - Theme not switching (enum value alignment)
  - Home screen Settings unreachable (menu item count off-by-one)

## Removed from Upstream

This fork is a minimal CJK-focused build. The following upstream features have been removed to reduce firmware size and complexity:

| Category | Removed |
|---|---|
| **Built-in Fonts** | Bookerly (12–18pt), NotoSans (8–18pt), OpenDyslexic (8–14pt), Ubuntu (10–12pt) — all styles (regular/bold/italic/bolditalic). Replaced with Pretendard JP Medium 10/12/14pt + KoPub Dotum Pro Medium 10/12/14pt |
| **Font System** | FontDecompressor (compressed font loading), FontCacheManager (old cache) |
| **Multi-language** | 20 translation files removed — UI is English only |
| **OTA Update** | Over-the-air firmware update |
| **KOReader Sync** | Reading progress sync with KOReader server |
| **OPDS Catalog** | OPDS book browser and parser |
| **TXT Reader** | Plain text file reading |
| **XTC Reader** | XTC format support |
| **Calibre / WebDAV** | Calibre integration, WebDAV sync |

### What Still Works

- **EPUB reader** — core reading, pagination, chapter navigation, TOC
- **SD card file browser** — browse and open EPUB files
- **WiFi file transfer** — hotspot mode, web-based file browser with upload/delete/rename
- **Settings** — font selection (6 built-in fonts), theme, sleep timer, orientation
- **Themes** — Lyra, Lyra 3 Covers
- **Sleep / wake** — auto and manual sleep
- **Battery indicator**
- **QR code display** — WiFi connection and URL QR codes

## Limitations

- Image decompression within EPUB may fail when heap is severely fragmented (graceful fallback — image is skipped)

## How to Build

```bash
# Clone with submodules
git clone --recursive https://github.com/TrissElan/crosspoint-reader-cjk-minimized.git

# Build
cd crosspoint-reader-cjk-minimized
pio run

# Flash
pio run --target upload
```

## Generating Fonts

The embedded `.epdfont` binaries are pre-built and checked into `lib/EpdFont/builtinFonts/`.
To regenerate from source TTF/OTF (requires `freetype-py`):

```bash
cd lib/EpdFont/scripts
python generate_pretendard_font.py    # Pretendard JP 10/12/14pt
python generate_kopub_font.py         # KoPub Dotum 10/12/14pt
```

## Credits

- Original project: [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
- Referenced: [crosspoint-reader-ko](https://github.com/crosspoint-reader-ko/crosspoint-reader-ko) - font-path hash-based cache invalidation, SD font architecture patterns
- CJK fork by: TrissElan

## Support

If you find this project useful, please consider supporting it.

[![donate](https://example.com/buy-me-a-coffee.png)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=WFXTNDJ3LYB2U)

## License

This project is licensed under the same license as the original CrossPoint Reader project.
