module;

#include <imgui.h>

export module formlight.viewer.settings_panel;

import std;
import formlight.scene_config;
import formlight.viewer.document;

namespace formlight
{
namespace
{
// Keep unrepresentable integer text editable. In particular, a negative sample count must never wrap to UINT_MAX.
using InvalidIntegers = std::map<ImGuiID, std::string>;

bool edit_integer(const char *label, std::integral auto &value, InvalidIntegers &invalid)
{
    const auto id = ImGui::GetID(label);
    std::array<char, 64> text{};
    if (const auto it = invalid.find(id); it != invalid.end())
    {
        std::ranges::copy(it->second, text.begin());
    }
    else
    {
        std::to_chars(text.data(), text.data() + text.size() - 1, value);
    }
    const bool edited = ImGui::InputText(label, text.data(), text.size(), ImGuiInputTextFlags_CharsDecimal);
    if (edited)
    {
        const char *end = text.data() + std::char_traits<char>::length(text.data());
        auto candidate = value;
        const auto [last, error] = std::from_chars(text.data(), end, candidate);
        if (error == std::errc{} && last == end)
        {
            value = candidate;
            invalid.erase(id);
        }
        else
        {
            invalid[id] = text.data();
        }
    }
    if (invalid.contains(id))
    {
        using Limits = std::numeric_limits<std::remove_cvref_t<decltype(value)>>;
        ImGui::TextWrapped("Enter an integer from %s.", std::format("{} to {}", Limits::min(), Limits::max()).c_str());
    }
    return edited;
}

template <class T> bool edit_value(T &value, InvalidIntegers &invalid)
{
    constexpr auto id = "##value";
    if constexpr (std::is_enum_v<T>)
    {
        const auto &options = choices(value);
        const auto selected = std::ranges::find(options, value, &NamedValue<T>::value);
        bool changed = false;
        if (ImGui::BeginCombo(id, selected->name.data()))
        {
            for (const auto &[name, option] : options)
            {
                const bool current = option == value;
                if (ImGui::Selectable(name.data(), current))
                {
                    value = option;
                    changed = !current;
                }
                if (current)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    else if constexpr (std::same_as<T, bool>)
    {
        return ImGui::Checkbox(id, &value);
    }
    else if constexpr (std::same_as<T, Vec3>)
    {
        std::array components{value.x, value.y, value.z};
        if (ImGui::InputScalarN(id, ImGuiDataType_Double, components.data(), 3, nullptr, nullptr, "%.9g"))
        {
            value = {components[0], components[1], components[2]};
            return true;
        }
        return false;
    }
    else if constexpr (std::same_as<T, double>)
    {
        return ImGui::InputScalar(id, ImGuiDataType_Double, &value, nullptr, nullptr, "%.9g");
    }
    else if constexpr (std::integral<T>)
    {
        return edit_integer(id, value, invalid);
    }
    else
    {
        static_assert(false, "Missing editor for this configuration field");
    }
}

bool edit_group(const char *name, ConfigGroup auto &group, auto edit)
{
    ImGui::SeparatorText(name);
    if (!ImGui::BeginTable(name, 2, ImGuiTableFlags_SizingStretchProp))
    {
        return false;
    }
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    bool changed = false;
    group.fields(
        [&](const char *key, auto &value)
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(key);
            ImGui::TableNextColumn();
            ImGui::PushID(key);
            ImGui::SetNextItemWidth(-1);
            if (edit(value))
            {
                changed = true;
            }
            ImGui::PopID();
        });
    ImGui::EndTable();
    return changed;
}
bool edit_lighting(std::optional<LightingSettings> &lighting, auto edit)
{
    ImGui::SeparatorText("Lighting");
    if (!lighting)
    {
        if (!ImGui::Button("Add scene lighting"))
        {
            return false;
        }
        lighting.emplace().lights.emplace_back();
        return true;
    }
    ImGui::PushID("ambient");
    ImGui::TextUnformatted("Ambient RGB");
    bool changed = edit(lighting->ambient);
    ImGui::PopID();
    auto &lights = lighting->lights;
    for (auto light = lights.begin(); light != lights.end(); ++light)
    {
        const auto i = light - lights.begin();
        ImGui::PushID(static_cast<int>(i));
        const auto name = std::format("Light {}", i);
        if (edit_group(name.c_str(), *light, edit))
        {
            changed = true;
        }
        const bool remove = ImGui::SmallButton("Remove light");
        ImGui::PopID();
        if (remove)
        {
            lights.erase(light);
            return true;
        }
    }
    if (ImGui::Button("Add light"))
    {
        lights.emplace_back();
        return true;
    }
    if (lights.empty() && ImGui::Button("Use base color preview"))
    {
        lighting.reset();
        return true;
    }
    return changed;
}
} // namespace

export class SettingsPanel
{
  public:
    void draw(ViewerDocument &document, std::int64_t max_texture_size)
    {
        auto &session = document.session();
        const auto edit = [&](auto &value) { return edit_value(value, invalid_); };
        ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInput, true);
        const bool render_changed = edit_group("Render", session.render_settings(), edit);
        const bool show_lens =
            session.render_settings().mode == RenderMode::Path || session.lens_settings().defocus_angle_degrees != 0;
        const bool lens_changed = show_lens && edit_group("Lens", session.lens_settings(), edit);
        const bool show_shutter =
            session.render_settings().mode == RenderMode::Path || session.shutter_settings().close != 0;
        const bool shutter_changed = show_shutter && edit_group("Shutter", session.shutter_settings(), edit);
        const bool light_changed = edit_lighting(session.light_settings(), edit);
        ImGui::PopItemFlag();
        if (render_changed || lens_changed || shutter_changed || light_changed)
        {
            dirty_ = true;
            error_.clear();
        }
        ImGui::BeginDisabled(!dirty_ || !invalid_.empty());
        if (ImGui::Button("Apply"))
        {
            try
            {
                const auto [width, height] = session.render_settings().extent;
                if (max_texture_size > 0 && std::max(width, height) > max_texture_size)
                {
                    throw std::invalid_argument(
                        std::format("The display supports render.width and render.height up to {}.", max_texture_size));
                }
                document.apply_settings();
                dirty_ = false;
            }
            catch (const std::invalid_argument &error)
            {
                error_ = error.what();
            }
        }
        ImGui::EndDisabled();
        if (!error_.empty())
        {
            ImGui::TextWrapped("%s", error_.c_str());
        }

        auto display = session.applied_config().display;
        const auto edit_display = [&]<class T>(T &value)
        {
            if constexpr (std::same_as<T, double>)
            {
                return ImGui::SliderScalar("##value", ImGuiDataType_Double, &value, &DisplaySettings::min_exposure_ev,
                                           &DisplaySettings::max_exposure_ev, "%.2f EV", ImGuiSliderFlags_AlwaysClamp);
            }
            else
            {
                return edit(value);
            }
        };
        if (edit_group("Display", display, edit_display))
        {
            document.set_display_settings(display);
        }
    }

  private:
    bool dirty_{};
    std::string error_;
    InvalidIntegers invalid_;
};
} // namespace formlight
