#pragma once
#include "render.h"
#include "external/imgui/imgui.h"

namespace bb {

template <typename... Args> void guiTextFmt(Args... _args) {
  std::string formatted = fmt::format(_args...);
  ImGui::Text(formatted.c_str());
}

void guiColorPicker3(const std::string &_label, Float3 &_color);
} // namespace bb