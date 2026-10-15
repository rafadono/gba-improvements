// Enhanced in-game settings menu renderer for recomp-ui (ImGui).
//
// OPTIONAL source of gba-improvements: it is NOT part of the gba_improvements library target (the
// library neither links nor includes recomp-ui or ImGui). gbaimp_add_host() (cmake/GbaimpHost.cmake)
// compiles it into the host executable and test target when recomp-ui is present, in place of
// recomp-ui's stock src/runtime_ui_imgui.cpp. It defines recomp_runtime_ui_render_imgui().
//
// Needs, from recomp-ui (include paths are added by recomp_target_launcher_ui()):
//   recomp_runtime_ui_internal.h   RecompRuntimeUi, RecompRuntimeUiItem, RECOMP_RUNTIME_UI_* constants and
//                                  the recomp_runtime_ui_* functions used below
//   launcher_theme.h               LauncherTheme, LngColor, launcher_theme_by_name()
//   imgui.h                        Dear ImGui (1.89.7+: ImGuiSelectableFlags_AllowOverlap, ImGuiKey_NamedKey_*)
// Features: draggable window (double-click the header to centre it), touch-friendly layout, keybind
// capture rows, INT -/+ steppers and TEXT edit fields.

// Modern in-game presentation for the renderer-independent runtime UI model.
// The host owns the ImGui context/frame/submission. This file only draws.

#include "recomp_runtime_ui_internal.h"
#include "launcher_theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

ImVec4 col(const LngColor &c, float alpha = 1.0f) {
    return ImVec4(c.r, c.g, c.b, c.a * alpha);
}

ImU32 u32(const LngColor &c, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(col(c, alpha));
}

void value_text(RecompRuntimeUi *ui, const RecompRuntimeUiItem *item,
                char *out, size_t out_size) {
    if (item->type == RECOMP_RUNTIME_UI_ACTION) {
        std::snprintf(out, out_size, "OPEN");
        return;
    }
    if (item->type == RECOMP_RUNTIME_UI_TEXT) {
        char buf[128];
        recomp_runtime_ui_current_text(ui, item, buf, sizeof(buf));
        std::snprintf(out, out_size, "%s", buf[0] ? buf : "(not set)");
        return;
    }
    int value = 0;
    if (!recomp_runtime_ui_current_value(ui, item, &value)) {
        std::snprintf(out, out_size, "Unavailable");
    } else if (item->type == RECOMP_RUNTIME_UI_BOOL) {
        std::snprintf(out, out_size, "%s", value ? "On" : "Off");
    } else if (item->type == RECOMP_RUNTIME_UI_CHOICE && item->choices) {
        size_t index = item->choice_count;
        if (item->choice_values) {
            for (size_t i = 0; i < item->choice_count; ++i)
                if (item->choice_values[i] == value) { index = i; break; }
        } else if (value >= item->minimum &&
                   static_cast<size_t>(value - item->minimum) < item->choice_count) {
            index = static_cast<size_t>(value - item->minimum);
        }
        if (index < item->choice_count)
            std::snprintf(out, out_size, "%s", item->choices[index]);
        else
            std::snprintf(out, out_size, "Unavailable");
    } else if (item->key &&
               std::strcmp(item->key,
                           RECOMP_RUNTIME_UI_KEY_GYRO_SENSITIVITY) == 0) {
        std::snprintf(out, out_size, "%.2fx",
                      static_cast<float>(value) / 100.0f);
    } else {
        std::snprintf(out, out_size, "%d", value);
    }
}

void draw_sections(RecompRuntimeUi *ui, const LauncherTheme &theme,
                   bool enter_on_click) {
    for (size_t index = 0; index < ui->section_count; ++index) {
        ImGui::PushID(static_cast<int>(index));
        const bool selected = index == ui->section_index;
        if (ImGui::Selectable(ui->sections[index], selected, 0,
                              ImVec2(0.0f, theme.row_height))) {
            ui->section_index = index;
            if (enter_on_click) recomp_runtime_ui_enter_section(ui, index);
        }
        if (ImGui::IsItemHovered()) ui->section_index = index;
        ImGui::PopID();
    }
}

void draw_items(RecompRuntimeUi *ui, const LauncherTheme &theme,
                bool touch_friendly) {
    const size_t count = recomp_runtime_ui_section_item_count(
        ui, ui->section_index);
    for (size_t index = 0; index < count; ++index) {
        const RecompRuntimeUiItem *item = recomp_runtime_ui_section_item(
            ui, ui->section_index, index);
        if (!item) continue;

        const bool enabled = recomp_runtime_ui_item_enabled(ui, item) != 0;
        const bool selected = ui->in_section && index == ui->row_index;
        char value[128];
        value_text(ui, item, value, sizeof(value));

        ImGui::PushID(static_cast<int>(index));
        if (!enabled) ImGui::BeginDisabled();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float row_h = item->description && item->description[0]
                                ? theme.row_height + theme.spacing_sm
                                : theme.row_height;
        const float step_button_w =
            touch_friendly ? std::max(76.0f, row_h * 0.90f) : 38.0f;
        const float step_gap = touch_friendly ? 12.0f : 6.0f;
        const float step_controls_w =
            item->type == RECOMP_RUNTIME_UI_INT
                ? step_button_w * 2.0f + step_gap + theme.spacing_sm
                : 0.0f;
        // INT rows carry -/+ buttons and TEXT rows an edit field, both drawn on
        // top of the row selectable; without AllowOverlap the selectable would
        // swallow the clicks meant for them.
        const ImGuiSelectableFlags row_flags =
            (item->type == RECOMP_RUNTIME_UI_INT ||
             item->type == RECOMP_RUNTIME_UI_TEXT)
                ? ImGuiSelectableFlags_AllowOverlap : 0;
        if (ImGui::Selectable("##setting", selected, row_flags,
                              ImVec2(0.0f, row_h))) {
            ui->row_index = index;
            ui->in_section = 1;
            if (item->type != RECOMP_RUNTIME_UI_INT)
                recomp_runtime_ui_adjust_current(ui, 1, 1, 0);
        }
        if (ImGui::IsItemHovered()) {
            ui->row_index = index;
            ui->in_section = 1;
        }

        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImVec2 end = ImGui::GetItemRectMax();
        const ImVec2 next = ImGui::GetCursorScreenPos();
        const ImU32 label_color = u32(enabled ? theme.text : theme.text_muted,
                                      enabled ? 1.0f : 0.65f);
        draw->AddText(ImVec2(start.x + theme.spacing_md,
                            start.y + theme.spacing_sm),
                      label_color, item->label ? item->label : "");
        const bool stepped_value = item->type == RECOMP_RUNTIME_UI_INT;
        const bool editing_this = item->type == RECOMP_RUNTIME_UI_TEXT &&
                                  ui->editing_text && selected;
        const bool is_keybind = item->key && std::strncmp(item->key, "input.key_", 10) == 0;
        if (!editing_this) {
            const ImVec2 value_size = ImGui::CalcTextSize(value);
            if (is_keybind) {
                const float badge_pad_x = 8.0f;
                const float badge_pad_y = 2.0f;
                const ImVec2 badge_min(end.x - theme.spacing_md - value_size.x - badge_pad_x * 2.0f,
                                       start.y + theme.spacing_sm - badge_pad_y);
                const ImVec2 badge_max(end.x - theme.spacing_md,
                                       start.y + theme.spacing_sm + value_size.y + badge_pad_y);
                draw->AddRectFilled(badge_min, badge_max,
                                    u32(selected && enabled ? theme.panel_hovered : theme.control, 0.90f),
                                    theme.radius_sm);
                draw->AddRect(badge_min, badge_max,
                              u32(selected && enabled ? theme.accent2 : theme.border, 0.85f),
                              theme.radius_sm, 0, 1.0f);
                draw->AddText(ImVec2(badge_min.x + badge_pad_x, start.y + theme.spacing_sm),
                              u32(selected && enabled ? theme.accent2 : theme.text),
                              value);
            } else {
                draw->AddText(ImVec2(end.x - theme.spacing_md - value_size.x -
                                        step_controls_w,
                                    start.y + theme.spacing_sm),
                              u32(selected && enabled ? theme.accent2
                                                      : theme.text_muted),
                              value);
            }
        }
        if (item->description && item->description[0]) {
            const ImVec2 clip_min(start.x + theme.spacing_md, start.y);
            const ImVec2 clip_max(end.x - theme.spacing_md, end.y + 4.0f);
            draw->PushClipRect(clip_min, clip_max, true);
            draw->AddText(ImVec2(start.x + theme.spacing_md,
                                start.y + theme.spacing_sm +
                                    ImGui::GetTextLineHeight() + 2.0f),
                          u32(theme.text_muted), item->description);
            draw->PopClipRect();
        }
        if (editing_this && enabled) {
            if (is_keybind) {
                static int s_listen_start_frame = 0;
                static const RecompRuntimeUiItem* s_listening_item = nullptr;
                if (s_listening_item != item) {
                    s_listening_item = item;
                    s_listen_start_frame = ImGui::GetFrameCount();
                }

                const char* prompt = "[ Press Key / Esc: Cancel ]";
                const ImVec2 prompt_sz = ImGui::CalcTextSize(prompt);
                const float badge_w = prompt_sz.x + theme.spacing_md * 2.0f;
                const float badge_h = std::max(ImGui::GetFrameHeight(), row_h * 0.72f);
                const ImVec2 badge_min(end.x - theme.spacing_md - badge_w,
                                       start.y + (row_h - badge_h) * 0.5f);
                const ImVec2 badge_max(end.x - theme.spacing_md,
                                       badge_min.y + badge_h);

                draw->AddRectFilled(badge_min, badge_max, u32(theme.accent2, 0.30f), theme.radius_sm);
                draw->AddRect(badge_min, badge_max, u32(theme.accent2), theme.radius_sm, 0, 1.5f);
                draw->AddText(ImVec2(badge_min.x + theme.spacing_md,
                                     badge_min.y + (badge_h - prompt_sz.y) * 0.5f),
                              u32(theme.accent2), prompt);

                if (ImGui::GetFrameCount() > s_listen_start_frame) {
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                            if (k == ImGuiKey_Escape) {
                                ui->editing_text = 0;
                                s_listening_item = nullptr;
                                break;
                            }
                            const char* kname = ImGui::GetKeyName(static_cast<ImGuiKey>(k));
                            if (kname && kname[0]) {
                                recomp_runtime_ui_commit_text(ui, item, kname);
                                ui->editing_text = 0;
                                s_listening_item = nullptr;
                                break;
                            }
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered()) {
                        ui->editing_text = 0;
                        s_listening_item = nullptr;
                    }
                }
            } else {
                const float field_w = touch_friendly
                    ? std::clamp((end.x - start.x) * 0.42f, 240.0f, 520.0f)
                    : 200.0f;
                const float field_h = touch_friendly
                    ? std::max(ImGui::GetFrameHeight(), row_h * 0.72f)
                    : ImGui::GetFrameHeight();
                ImGui::SetCursorScreenPos(
                    ImVec2(end.x - theme.spacing_md - field_w,
                           start.y + (row_h - field_h) * 0.5f));
                ImGui::SetNextItemWidth(field_w);
                // Nothing is active on the first frame of an edit, which is exactly
                // when focus should land in the field.
                if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
                const bool submitted = ImGui::InputText(
                    "##edit", ui->edit_buffer, sizeof(ui->edit_buffer),
                    ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll);
                const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                if (submitted) {
                    recomp_runtime_ui_commit_text(ui, item, ui->edit_buffer);
                    ui->editing_text = 0;
                } else if (cancelled) {
                    ui->editing_text = 0;
                } else if (!ImGui::IsItemActive() && ImGui::IsAnyItemActive()) {
                    // Focus moved elsewhere (a click on another row): treat it as
                    // abandoning the edit rather than silently committing.
                    ui->editing_text = 0;
                }
            }
            ImGui::SetCursorScreenPos(next);
        }
        if (stepped_value && enabled) {
            const float button_h = touch_friendly
                ? std::max(ImGui::GetFrameHeight(), row_h * 0.72f)
                : ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(
                ImVec2(end.x - theme.spacing_sm -
                           step_button_w * 2.0f - step_gap,
                       start.y + (row_h - button_h) * 0.5f));
            if (ImGui::Button("-", ImVec2(step_button_w, button_h))) {
                ui->row_index = index;
                ui->in_section = 1;
                recomp_runtime_ui_adjust_current(ui, -1, 1, 0);
            }
            ImGui::SameLine(0.0f, step_gap);
            if (ImGui::Button("+", ImVec2(step_button_w, button_h))) {
                ui->row_index = index;
                ui->in_section = 1;
                recomp_runtime_ui_adjust_current(ui, 1, 1, 0);
            }
            ImGui::SetCursorScreenPos(next);
        }
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void push_runtime_style(const LauncherTheme &theme, bool touch_friendly) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.radius_lg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.radius_sm);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.radius_sm);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(theme.spacing_lg, theme.spacing_lg));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(theme.spacing_sm, theme.spacing_sm));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ScrollbarSize,
        touch_friendly ? std::max(30.0f, theme.spacing_lg * 1.5f)
                       : ImGui::GetStyle().ScrollbarSize);
    ImGui::PushStyleVar(
        ImGuiStyleVar_GrabMinSize,
        touch_friendly ? std::max(30.0f, theme.spacing_lg * 1.5f)
                       : ImGui::GetStyle().GrabMinSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(theme.background, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme.panel, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme.border));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, col(theme.text_muted));
    ImGui::PushStyleColor(ImGuiCol_Header, col(theme.control_hovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col(theme.panel_hovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, col(theme.accent_dim));
    ImGui::PushStyleColor(ImGuiCol_Button, col(theme.control));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme.control_hovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme.accent_dim));
}

void pop_runtime_style() {
    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(7);
}

// Keeps part of the window header on screen so the menu can always be grabbed again.
ImVec2 clamp_window_pos(ImVec2 pos, ImVec2 size, ImVec2 display) {
    const float keep_x = std::min(160.0f, size.x);
    const float keep_y = 40.0f;
    const float min_x = keep_x - size.x;
    const float max_x = std::max(display.x - keep_x, min_x);
    pos.x = std::clamp(pos.x, min_x, max_x);
    pos.y = std::clamp(pos.y, 0.0f, std::max(display.y - keep_y, 0.0f));
    return pos;
}

} // namespace

extern "C" void recomp_runtime_ui_render_imgui(RecompRuntimeUi *ui) {
    if (!ui || !ui->open || ImGui::GetCurrentContext() == nullptr) return;

    LauncherTheme theme = launcher_theme_by_name(ui->config.theme);
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f) return;
    const bool touch_friendly =
        (ui->config.presentation_flags &
         RECOMP_RUNTIME_UI_PRESENTATION_TOUCH_FRIENDLY) != 0;

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 150));

    const float short_axis = std::min(display.x, display.y);
    const float touch_row_height =
        std::clamp(short_axis * 0.085f, 92.0f, 124.0f);
    const float metric_scale =
        touch_friendly ? touch_row_height / theme.row_height : 1.0f;
    if (touch_friendly) {
        theme.row_height *= metric_scale;
        theme.spacing_sm *= metric_scale;
        theme.spacing_md *= metric_scale;
        theme.spacing_lg *= metric_scale;
        theme.radius_sm *= metric_scale;
        theme.radius_lg *= metric_scale;
    }
    const float margin = touch_friendly
        ? std::max(20.0f, short_axis * 0.025f)
        : 24.0f;
    const float max_width = touch_friendly ? display.x : (display.x >= 960.0f ? 840.0f : 780.0f);
    const float max_height = touch_friendly ? display.y : 680.0f;
    const float width = display.x - margin * 2.0f < max_width
                            ? display.x - margin * 2.0f
                            : max_width;
    const float height = display.y - margin * 2.0f < max_height
                             ? display.y - margin * 2.0f
                             : max_height;
    const bool wide = width >= (touch_friendly ? 900.0f : 640.0f);

    // The window is centered until the user drags its header; the chosen position is kept
    // while the process runs so the menu can be parked where it does not hide the game.
    // A window/display resize or touch-friendly mode goes back to the centered layout.
    static ImVec2 s_window_pos = ImVec2(0.0f, 0.0f);
    static ImVec2 s_window_display = ImVec2(0.0f, 0.0f);
    static bool s_window_moved = false;
    const bool display_changed =
        s_window_display.x != display.x || s_window_display.y != display.y;
    if (display_changed || touch_friendly) s_window_moved = false;
    s_window_display = display;
    if (!s_window_moved) {
        s_window_pos = ImVec2((display.x - width) * 0.5f, (display.y - height) * 0.5f);
    }
    s_window_pos = clamp_window_pos(s_window_pos, ImVec2(width, height), display);
    ImGui::SetNextWindowPos(s_window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

    push_runtime_style(theme, touch_friendly);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##recomp-runtime-ui", nullptr, flags)) {
        if (touch_friendly) {
            // Scale toward a phone-readable physical size regardless of any
            // host-wide DPI scale already applied to the ImGui context.
            const float target_font_px =
                std::clamp(short_axis * 0.036f, 34.0f, 48.0f);
            const float base_font_px =
                ImGui::GetFont()->FontSize * ImGui::GetFont()->Scale *
                io.FontGlobalScale;
            const float font_scale =
                std::clamp(target_font_px / base_font_px, 1.0f, 4.0f);
            ImGui::SetWindowFontScale(font_scale);
        }
        // Drag handle over the header row: drag with the mouse to move the menu,
        // double-click to center it again. Not offered in touch-friendly mode, where the
        // menu fills the screen, and skipped by gamepad/keyboard navigation.
        const ImVec2 header_cursor = ImGui::GetCursorPos();
        if (!touch_friendly) {
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            ImGui::InvisibleButton("##move-handle",
                                   ImVec2(ImGui::GetContentRegionAvail().x,
                                          ImGui::GetTextLineHeightWithSpacing()));
            ImGui::PopItemFlag();
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                ImGui::SetTooltip("Drag to move - double-click to center");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) s_window_moved = false;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                s_window_pos.x += io.MouseDelta.x;
                s_window_pos.y += io.MouseDelta.y;
                s_window_moved = true;
            }
            ImGui::SetCursorPos(header_cursor);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme.accent2));
        ImGui::TextUnformatted(ui->config.title ? ui->config.title : "Settings");
        ImGui::PopStyleColor();
        if (ui->config.subtitle && ui->config.subtitle[0]) {
            ImGui::SameLine();
            const float subtitle_w = ImGui::CalcTextSize(ui->config.subtitle).x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() -
                                 ImGui::GetStyle().WindowPadding.x - subtitle_w);
            ImGui::TextDisabled("%s", ui->config.subtitle);
        }
        ImGui::Separator();

        const float close_h = touch_friendly
            ? std::max(ImGui::GetFrameHeight(), theme.row_height * 0.82f)
            : 0.0f;
        const float footer_h = touch_friendly
            ? close_h + theme.spacing_sm * 2.0f
            : ImGui::GetTextLineHeightWithSpacing() + theme.spacing_lg + 2.0f;
        const float content_h = ImGui::GetContentRegionAvail().y - footer_h;
        if (wide) {
            const float sidebar_w = touch_friendly
                ? std::clamp(width * 0.20f, 280.0f, 520.0f)
                : 190.0f;
            ImGui::BeginChild("##sections", ImVec2(sidebar_w, content_h), true);
            draw_sections(ui, theme, false);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##items", ImVec2(0.0f, content_h), true);
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme.accent2));
            ImGui::TextUnformatted(ui->sections[ui->section_index]);
            ImGui::PopStyleColor();
            ImGui::Separator();
            draw_items(ui, theme, touch_friendly);
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("##content", ImVec2(0.0f, content_h), true);
            if (ui->in_section) {
                if (ImGui::SmallButton("< Back"))
                    recomp_runtime_ui_leave_section(ui);
                ImGui::SameLine();
                ImGui::TextUnformatted(ui->sections[ui->section_index]);
                ImGui::Separator();
                draw_items(ui, theme, touch_friendly);
            } else {
                draw_sections(ui, theme, true);
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (touch_friendly) {
            ImGui::TextDisabled("Tap a section or setting");
        } else {
            const char *accept = ui->config.accept_label
                                     ? ui->config.accept_label : "Select";
            const char *back = ui->config.back_label
                                   ? ui->config.back_label : "Back";
            ImGui::TextDisabled(
                "%s  Select    %s  Back    D-pad / Arrows  Navigate",
                accept, back);
        }
        if (ui->status_frames) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme.accent2));
            ImGui::TextUnformatted(ui->status);
            ImGui::PopStyleColor();
            --ui->status_frames;
        }
        ImGui::SameLine();
        const char *close_label = "Resume";
        const float close_w = touch_friendly
            ? std::max(320.0f, ImGui::CalcTextSize(close_label).x +
                                   theme.spacing_lg * 2.0f)
            : ImGui::CalcTextSize(close_label).x + theme.spacing_lg * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() -
                             ImGui::GetStyle().WindowPadding.x - close_w);
        if (ImGui::Button(close_label, ImVec2(close_w, close_h)))
            recomp_runtime_ui_close(ui);
    }
    ImGui::End();
    pop_runtime_style();
}
