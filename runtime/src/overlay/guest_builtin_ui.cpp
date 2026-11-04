// SwitchWakerHD (this fork): see guest_builtin_ui.h.
#include "guest_builtin_ui.h"

#include <algorithm>
#include <string>

#include "../menu_i18n.h"
#include "../mods/guest_builtin.h"
#include "../platform/zh_tr.h"  // (this fork) translate names, options and notes through the zh table
#include "imgui.h"

namespace guestbuiltinui {

// the Mods tab's colours (mods_tab.h)
static const ImVec4 kMuted(0.70f, 0.78f, 0.84f, 1.0f), kWarn(1.0f, 0.80f, 0.35f, 1.0f), kBad(1.0f, 0.50f, 0.45f, 1.0f);

static void wrapped(const ImVec4& colour, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

static void option_control(guestbuiltin::Mod& m, guestbuiltin::Option& o, std::string& error) {
    using mods::json::Value;
    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 14, ImGui::GetContentRegionAvail().x * 0.6f));
    if (o.type == "bool") {
        bool v = o.value.type == Value::Bool && o.value.boolean;
        if (ImGui::Checkbox(WWHD_ZH(o.name.c_str()), &v)) guestbuiltin::set_option(m, o, Value(v), true, error);
    } else if (o.type == "number") {
        double v = o.value.type == Value::Number ? o.value.number : o.minimum;
        if (ImGui::SliderScalar(WWHD_ZH(o.name.c_str()), ImGuiDataType_Double, &v, &o.minimum, &o.maximum, "%.3g"))
            guestbuiltin::set_option(m, o, Value(v), false, error);
        if (ImGui::IsItemDeactivatedAfterEdit()) guestbuiltin::set_option(m, o, o.value, true, error);
    } else if (o.type == "enum") {
        const std::string current = o.value.string();
        if (ImGui::BeginCombo(WWHD_ZH(o.name.c_str()), WWHD_ZH(current.c_str()))) {
            for (const auto& c : o.choices)
                if (ImGui::Selectable(WWHD_ZH(c.c_str()), c == current)) guestbuiltin::set_option(m, o, Value(c), true, error);
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextUnformatted((std::string(WWHD_ZH(o.name.c_str())) + ": " + o.value.string()).c_str());  // text options: options.json
    }
    if (!o.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", WWHD_ZH(o.description.c_str()));
}

void draw() {
    auto& list = guestbuiltin::list();
    if (list.empty()) return;
    ImGui::SeparatorText(WWHD_ZH(menu_i18n::tr("Code mods in this build")));
    wrapped(kMuted, WWHD_ZH(menu_i18n::tr("Built into the game when it was made (make_sd.py --guest-mod). To add or remove one, build the "
                                          "game again.")));
    // above the options: the last one a controller reaches is at the bottom of the tab, where a line under it is
    // out of view
    if (std::any_of(list.begin(), list.end(), [](const guestbuiltin::Mod& m) { return m.changed; }))
        wrapped(kWarn, WWHD_ZH(menu_i18n::tr("Saved. The mods read their options when the game starts: close the game (HOME) and start it "
                                             "again to see the change.")));
    for (auto& m : list) {
        ImGui::PushID(m.id.c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted((std::string(WWHD_ZH(m.name.c_str())) + "  " + m.version).c_str());
        // (this fork) the reason a mod is not drawing, in the UI's language too (zh_table.inc has the
        // lines the port's own services set: "the game code has no mod hooks", the HUD PNG failures)
        if (!m.status.empty()) wrapped(kBad, std::string(WWHD_ZH(menu_i18n::tr("Not running: "))) + WWHD_ZH(m.status.c_str()));
        else if (!m.description.empty()) wrapped(kMuted, WWHD_ZH(m.description.c_str()));
        static std::string error;
        for (auto& o : m.options) {
            ImGui::PushID(o.id.c_str());
            option_control(m, o, error);
            ImGui::PopID();
        }
        if (!error.empty()) wrapped(kBad, error);
        ImGui::PopID();
    }
}

}  // namespace guestbuiltinui
