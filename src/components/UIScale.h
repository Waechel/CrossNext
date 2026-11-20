#pragma once
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight). One fixed tier for every board — the
// user-facing UI-scale setting was removed.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

// 0 Small (10/12, the stock tier) .. 3 Extra large (16/18). Defined in
// CrossPointSettings.cpp; always 0 on boards without the larger UI fonts.
int uiTextSizeTier();

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = UI_12_FONT_ID;
#if FREEINK_DEVICE_METALIO_PLUS
  switch (uiTextSizeTier()) {
    case 1: spec.smallFontId = UI_12_FONT_ID; spec.bodyFontId = UI_14_FONT_ID; break;
    case 2: spec.smallFontId = UI_14_FONT_ID; spec.bodyFontId = UI_16_FONT_ID; break;
    case 3: spec.smallFontId = UI_16_FONT_ID; spec.bodyFontId = UI_18_FONT_ID; break;
    default: break;
  }
  spec.titleFontId = spec.bodyFontId;
  return spec;
#endif
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = UI_12_FONT_ID;
  return spec;
}
