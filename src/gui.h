#pragma once
#include "render.h"
#include "external/imgui/imgui.h"

namespace bb {

struct GUI {
  const PBRMaterialSet *MaterialSet;
  EnumArray<PBRMapType, ImTextureID> DefaultMaterialTextureId;
  std::vector<EnumArray<PBRMapType, ImTextureID>> MaterialTextureIds;

  int SelectedMaterialIndex;
};

template <typename... Args> void guiTextFmt(Args... _args) {
  std::string formatted = fmt::format(_args...);
  ImGui::Text(formatted.c_str());
}

void guiColorPicker3(const std::string &_label, Float3 &_color);

struct GUIInitParams {
  VkSampler MaterialImageSampler;
  const PBRMaterialSet *MaterialSet;
};

GUI createGUI(const GUIInitParams &_params);
void updateGUI(GUI &_gui);

} // namespace bb