#include "camera.h"

namespace bb {

Mat4 FreeLookCamera::getViewMatrix() const {
  return Mat4::lookAt(Pos, Pos + getLook());
}

Float3 FreeLookCamera::getRight() const {
  Float3 up = {0, 1, 0};
  return cross(up, getLook()).normalize();
};

Float3 FreeLookCamera::getLook() const {
  float yawRadian = degToRad(Yaw);
  float pitchRadian = degToRad(Pitch);
  float cosPitch = cosf(pitchRadian);
  return {-sinf(yawRadian) * cosPitch, sinf(pitchRadian),
          cosf(yawRadian) * cosPitch};
}

} // namespace bb