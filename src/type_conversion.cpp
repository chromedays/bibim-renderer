#include "type_conversion.h"

namespace bb {

VkExtent2D int2ToExtent2D(Int2 _v) {
  BB_ASSERT(_v.X > 0 && _v.Y > 0);
  return {(uint32_t)_v.X, (uint32_t)_v.Y};
}

VkExtent3D int2ToExtent3D(Int2 _v) {
  BB_ASSERT(_v.X > 0 && _v.Y > 0);
  return {(uint32_t)_v.X, (uint32_t)_v.Y, 1};
}

Float3 aiVector3DToFloat3(const aiVector3D &_aiVec3) {
  Float3 result = {_aiVec3.x, _aiVec3.y, _aiVec3.z};
  return result;
}

} // namespace bb