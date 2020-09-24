#pragma once
#include "vector_math.h"
#include "external/volk.h"
#include "external/assimp/vector3.h"

namespace bb {

VkExtent2D int2ToExtent2D(Int2 _v);
VkExtent3D int2ToExtent3D(Int2 _v);
Float3 aiVector3DToFloat3(const aiVector3D &_aiVec3);
Float2 aiVector3DToFloat2(const aiVector3D &_aiVec3);

} // namespace bb