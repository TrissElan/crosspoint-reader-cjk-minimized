// Embedded fonts (2-bit) in Flash via .incbin assembly.
// Zero-copy: intervals read directly from Flash, no RAM allocation.
#pragma once

#define PRETENDARD_10_FONT_ID (-800000010)
#define PRETENDARD_12_FONT_ID (-800000012)
#define PRETENDARD_14_FONT_ID (-800000014)

#define KOPUB_10_FONT_ID (-800100010)
#define KOPUB_12_FONT_ID (-800100012)
#define KOPUB_14_FONT_ID (-800100014)

// Aliases for UI contexts
#define UI_12_FONT_ID  PRETENDARD_12_FONT_ID
#define SMALL_FONT_ID  PRETENDARD_10_FONT_ID
