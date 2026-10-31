// English -> 简体中文 for the fork's own interface (zh_tr.h). Generated table: zh_table.inc.
#include "zh_tr.h"

#include <cstdlib>
#include <cstring>

namespace {

struct Entry {
    const char* en;
    const char* zh;
};

const Entry kTable[] = {
#include "zh_table.inc"
};

constexpr size_t kCount = sizeof kTable / sizeof kTable[0];

// a plain binary search: the table is sorted by strcmp (gen_zh_table.py)
const Entry* find(const char* s) {
    size_t lo = 0, hi = kCount;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(kTable[mid].en, s);
        if (c == 0) return &kTable[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

bool english_forced() {
    static const bool forced = [] {
        const char* v = getenv("WWHD_UI_EN");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return forced;
}

}  // namespace

bool wwhd_zh_forced_english() { return english_forced(); }

const char* wwhd_zh_tr(const char* s) {
    if (!s || !*s) return s;
    if (english_forced()) return s;
    if (const Entry* e = find(s)) return e->zh;
    return s;
}
