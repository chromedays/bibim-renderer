#include "gui.h"

namespace bb {

void guiColorPicker3(const std::string &_label, Float3 &_color) {
  ImGui::ColorPicker3(_label.c_str(), (float *)&_color);
}

} // namespace bb
