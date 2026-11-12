// (this fork) The settings menu's texts in Spanish, Brazilian Portuguese, Italian, German or French (menu_i18n.h).
#include "menu_i18n.h"

#ifdef WWHD_ZH_UI
#include "platform/zh_tr.h"  // (this fork) the Chinese table, consulted before the other languages
#endif

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace menu_i18n {

extern const Row kRows[];  // menu_i18n_table.cpp
extern const int kRowCount;

namespace {

std::atomic<int> g_language{kEnglish};
std::atomic<long (*)()> g_source{nullptr};
std::mutex g_mu;

const std::unordered_map<std::string_view, const Row*>& table() {  // under g_mu
    static const std::unordered_map<std::string_view, const Row*> t = [] {
        std::unordered_map<std::string_view, const Row*> m;
        m.reserve(size_t(kRowCount));
        for (int i = 0; i < kRowCount; i++) m.emplace(kRows[i].en, &kRows[i]);
        return m;
    }();
    return t;
}

const char* column(const Row& r, int lang) {
    const char* s = lang == kSpanish ? r.es : lang == kPortuguese ? r.pt : lang == kItalian ? r.it
                  : lang == kGerman  ? r.de : r.fr;
    return s && *s ? s : nullptr;
}

}  // namespace

void set_language_source(long (*code)()) { g_source.store(code); }

void set_language(long code) {
    const Language l = code == 5 ? kSpanish : code == 9 ? kPortuguese : code == 4 ? kItalian : code == 3 ? kGerman
                     : code == 2 ? kFrench : kEnglish;
    g_language.store(l, std::memory_order_relaxed);
}

Language language() {
    if (long (*source)() = g_source.exchange(nullptr)) set_language(source());
    return Language(g_language.load(std::memory_order_relaxed));
}

const char* tr(const char* english) {
    if (g_source.load(std::memory_order_relaxed)) language();
#ifdef WWHD_ZH_UI
    // (this fork) Chinese first: zh_table.inc holds the menu's English texts, format strings included
    // ("Install %s"), so the translation happens before textf() fills the conversions in. A text that is
    // not in the table, or WWHD_UI_EN=1, falls through to the languages below.
    if (const char* zh = wwhd_zh_tr(english); zh != english) return zh;
#endif
    const int lang = g_language.load(std::memory_order_relaxed);
    if (lang == kEnglish || !english || !*english) return english;
    std::lock_guard<std::mutex> lk(g_mu);
    const std::string_view text(english);
    const size_t id = text.find("##");
    const auto& t = table();
    const auto it = t.find(id == std::string_view::npos ? text : text.substr(0, id));
    const char* translated = it == t.end() ? nullptr : column(*it->second, lang);
    if (!translated) return english;
    if (id == std::string_view::npos) return translated;
    // "Label##id": the translated label with the same id, kept for the next frames (node addresses stay put)
    static std::unordered_map<std::string, std::string> joined[6];
    auto [pos, added] = joined[lang].try_emplace(std::string(text));
    if (added) pos->second = std::string(translated) + std::string(text.substr(id));
    return pos->second.c_str();
}

const Row* rows(int* count) {
    if (count) *count = kRowCount;
    return kRows;
}

}  // namespace menu_i18n
