#pragma once
#include "vector_math.h"

namespace bb {

struct FreeLookCamera {
  Float3 Pos;
  float Yaw;
  float Pitch;

  Mat4 getViewMatrix() const;
  Float3 getRight() const;
  Float3 getLook() const;
};

} // namespace bb