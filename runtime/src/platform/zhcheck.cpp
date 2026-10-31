#include <cstdio>
#include <cstring>
#include "zh_tr.h"
int main() {
    const char* cases[] = {
        "Stick dead zone", "Delete save backup##delete_confirm", "Mute##volume",
        "Save states", "Invert camera up/down", "Quit Wind Waker HD?",
        "not in the table at all",
    };
    for (const char* c : cases) printf("%-42s -> %s\n", c, wwhd_zh_tr(c));
    return 0;
}
