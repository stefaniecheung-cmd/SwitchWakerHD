// Chinese texts for the fork's own on-screen interface (this build only).
//
// The settings overlay, its tabs and the "Prepare graphics" card are written in English throughout.
// Rather than rewriting every call site, the Switch build routes ImGui's texts and widget labels
// (IMGUI_ZH_TR in imconfig.h) and the two text tables of ui_text_switch.cpp through tr(): a table of
// English -> 简体中文, and anything not in it stays as it is, so the UI is never worse than before.
// WWHD_UI_EN=1 in the environment keeps the English interface for one start.
#pragma once

// the same symbol imconfig.h declares for IMGUI_ZH_TR()
const char* wwhd_zh_tr(const char* s);

// true when WWHD_UI_EN=1 asked for the English interface (the overlay then keeps its Latin font)
bool wwhd_zh_forced_english();

#ifdef WWHD_ZH_UI
#define WWHD_ZH(s) (::wwhd_zh_tr(s))
#else
#define WWHD_ZH(s) (s)
#endif
