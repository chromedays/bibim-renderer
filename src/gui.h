#pragma once
#include "external/imgui/imgui.h"

// ImGui extension functions
namespace ImGui {
template <typename... Args> void TextFmt(Args... args) {
  std::string formatted = fmt::format(args...);
  ImGui::Text(formatted.c_str());
}
} // namespace ImGui
