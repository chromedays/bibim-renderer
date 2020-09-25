#include "gui.h"

namespace bb {

void guiColorPicker3(const std::string &_label, Float3 &_color) {
  ImGui::ColorPicker3(_label.c_str(), (float *)&_color);
}

void guiMaterialPicker(const std::string &_label,
                       InstanceBlock &_instanceBlock) {
  ImGui::PushID(_label.c_str());
  guiColorPicker3("Albedo", _instanceBlock.Albedo);
  ImGui::SliderFloat("Metallic", &_instanceBlock.Metallic, 0, 1);
  ImGui::SliderFloat("Roughness", &_instanceBlock.Roughness, 0.1f, 1);
  ImGui::SliderFloat("AO", &_instanceBlock.AO, 0, 1);
  ImGui::PopID();
}

} // namespace bb
