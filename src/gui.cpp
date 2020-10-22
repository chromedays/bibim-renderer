#include "gui.h"
#include "external/imgui/imgui_impl_vulkan.h"

namespace bb {

void guiColorPicker3(const std::string &_label, Float3 &_color) {
  ImGui::ColorPicker3(_label.c_str(), (float *)&_color);
}

GUI createGUI(const GUIInitParams &_params) {
  GUI gui = {};
  gui.MaterialSet = _params.MaterialSet;

  for (int i = 0; i < (int)PBRMapType::COUNT; ++i) {
    PBRMapType mapType = (PBRMapType)i;
    const Image &image = gui.MaterialSet->DefaultMaterial.Maps[mapType];
    if (image.Handle != VK_NULL_HANDLE) {
      gui.DefaultMaterialTextureId[mapType] =
          ImGui_ImplVulkan_AddTexture(_params.MaterialImageSampler, image.View,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  for (const PBRMaterial &material : gui.MaterialSet->Materials) {
    EnumArray<PBRMapType, ImTextureID> textureIds;
    for (int i = 0; i < (int)PBRMapType::COUNT; ++i) {
      PBRMapType mapType = (PBRMapType)i;
      const Image &image = material.Maps[mapType];
      if (image.Handle != VK_NULL_HANDLE) {
        textureIds[mapType] = ImGui_ImplVulkan_AddTexture(
            _params.MaterialImageSampler, image.View,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      } else {
        textureIds[mapType] = gui.DefaultMaterialTextureId[mapType];
      }
    }

    gui.MaterialTextureIds.push_back(textureIds);
  }

  return gui;
}

void updateGUI(GUI &_gui) {

  if (ImGui::Begin("Material Selector")) {
    for (int i = 0; i < _gui.MaterialTextureIds.size(); ++i) {

      if (ImGui::Selectable(_gui.MaterialSet->Materials[i].Name.c_str(),
                            _gui.SelectedMaterialIndex == i)) {
        _gui.SelectedMaterialIndex = i;
      }
    }
  }
  ImGui::End();

  int numCols = 3;
  int col = 0;

  if (ImGui::Begin("Current Material")) {
    const auto &textureIds =
        _gui.MaterialTextureIds[_gui.SelectedMaterialIndex];

    for (auto textureId : textureIds) {
      ImGui::Image(textureId, {50, 50});
      ++col;
      if (col < numCols) {
        ImGui::SameLine();
      } else {
        col = 0;
      }
    }
  }
  ImGui::End();
}

} // namespace bb
