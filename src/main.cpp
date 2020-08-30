#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include "external/SDL2/SDL_main.h"
#include "external/SDL2/SDL_syswm.h"
#include "external/fmt/format.h"
#include <vector>
#include <stdio.h>
#include <assert.h>

#if BB_DEBUG
#define BB_ASSERT(exp) assert(exp)
#define BB_LOG(...) bb::print(__VA_ARGS__)
#else
#define BB_ASSERT(exp)
#define BB_LOG(...)
#endif
#define BB_VK_ASSERT(exp)                                                      \
  do {                                                                         \
    auto result = exp;                                                         \
    BB_ASSERT(result == VK_SUCCESS);                                           \
  } while (0)

namespace bb {

template <typename... Args> void print(Args... args) {
  std::string formatted = fmt::format(args...);
  formatted += "\n";
  OutputDebugStringA(formatted.c_str());
}

template <typename E, typename T> struct EnumArray {
  T Elems[(int)(E::COUNT)] = {};

  const T &operator[](E _e) const { return Elems[(int)_e]; }

  T &operator[](E _e) { return Elems[(int)_e]; }

  auto begin() { return Elems.begin(); }

  auto end() { return Elems.end(); }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

constexpr float pi32 = 3.141592f;

struct Int2 {
  int X = 0;
  int Y = 0;
};

struct Float2 {
  float X = 0.f;
  float Y = 0.f;
};

struct Float3 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;

  float lengthSq() {
    float result = X * X + Y * Y + Z * Z;
    return result;
  }

  float length() {
    float result = sqrtf(lengthSq());
    return result;
  }

  Float3 normalize() {
    float len = length();
    Float3 result = *this / len;
    return result;
  }

  Float3 operator-(const Float3 &_other) const {
    Float3 result = {X - _other.X, Y - _other.Y, Z - _other.Z};
    return result;
  }

  Float3 operator/(float _divider) const {
    Float3 result = {X / _divider, Y / _divider, Z / _divider};
    return result;
  }
};

inline float dot(const Float3 &_a, const Float3 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z;
}

inline Float3 cross(const Float3 &_a, const Float3 &_b) {
  Float3 result = {
      _a.Y * _b.Z - _a.Z * _b.Y,
      _a.Z * _b.X - _a.X * _b.Z,
      _a.X * _b.Y - _a.Y * _b.X,
  };
  return result;
}

struct Float4 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;
  float W = 0.f;
};

inline float dot(const Float4 &_a, const Float4 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z + _a.W * _b.W;
}

struct Mat4 {
  float M[4][4] = {};

  Float4 row(int n) const {
    BB_ASSERT(n >= 0 && n < 4);
    return {M[0][n], M[1][n], M[2][n], M[3][n]};
  }

  Float4 column(int n) const {
    BB_ASSERT(n >= 0 && n < 4);
    return {M[n][0], M[n][1], M[n][2], M[n][3]};
  }

  static Mat4 identity() {
    return {{
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    }};
  }

  static Mat4 lookAt(const Float3 &eye, const Float3 &target,
                     const Float3 &upAxis = {0, 1, 0}) {
    Float3 forward = (eye - target).normalize();
    Float3 right = cross(upAxis, forward).normalize();
    Float3 up = cross(forward, right).normalize();

    // clang-format off
    return {{
      {right.X, up.X, forward.X, 0},
      {right.Y, up.Y, forward.Y, 0},
      {right.Z, up.Z, forward.Z, 0},
      {-dot(eye, right), -dot(eye, up), -dot(eye, forward), 1},
    }};
    // clang-format on
  }

// TODO(ilgwon): Need to be adjusted
#if 0
  static Mat4 ortho(float left, float right, float top, float bottom,
                    float nearZ, float farZ) {
#define NDC_MIN_Z 0
#define NDC_MAX_Z 1
    // clang-format off
    Mat4 result = {{
        {2.f / (right - left), 0, 0, 0},
        {0, 2.f / (top - bottom), 0, 0},
        {0, 0, (NDC_MAX_Z - NDC_MIN_Z) / (farZ - nearZ), 0},
        {(left + right) / (left - right), (bottom + top) / (bottom - top),
         farZ * (NDC_MAX_Z - NDC_MIN_Z) / (farZ - nearZ), 1},
    }};
    // clang-format on
    return result;
  }

  static Mat4 orthoCenter(float width, float height, float nearZ, float farZ) {
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    Mat4 result =
        ortho(-halfWidth, halfWidth, halfHeight, -halfHeight, nearZ, farZ);
    return result;
  }
#endif
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
  Float4 rows[4] = {a.row(0), a.row(1), a.row(2), a.row(3)};
  Float4 columns[4] = {b.column(0), b.column(1), b.column(2), b.column(3)};
  Mat4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.M[i][j] = dot(rows[i], columns[j]);
    }
  }
  return result;
}

} // namespace bb

int main(int _argc, char **_argv) {
  BB_VK_ASSERT(volkInitialize());

  SDL_Init(SDL_INIT_VIDEO);
  int width = 1280;
  int height = 720;
  SDL_Window *window =
      SDL_CreateWindow("Bibim Renderer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width, height, 0);

  SDL_SysWMinfo sysinfo = {};
  SDL_VERSION(&sysinfo.version);
  SDL_GetWindowWMInfo(window, &sysinfo);

  VkInstance instance;
  VkApplicationInfo appinfo = {};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "Bibim Renderer";
  appinfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appinfo.pEngineName = "Bibim Renderer";
  appinfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appinfo.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo instanceCreateInfo = {};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pApplicationInfo = &appinfo;

  std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

  bool enableValidationLayers =
#ifdef BB_DEBUG
      true;
#else
      false;
#endif

  uint32_t numLayers;
  vkEnumerateInstanceLayerProperties(&numLayers, nullptr);
  std::vector<VkLayerProperties> layerProperties(numLayers);
  vkEnumerateInstanceLayerProperties(&numLayers, layerProperties.data());
  bool canEnableLayers = true;
  for (const char *layerName : validationLayers) {
    bool foundLayer = false;
    for (VkLayerProperties &properties : layerProperties) {
      if (strcmp(properties.layerName, layerName) == 0) {
        foundLayer = true;
        break;
      }
    }

    if (!foundLayer) {
      canEnableLayers = false;
      break;
    }
  }

  if (enableValidationLayers && canEnableLayers) {
    instanceCreateInfo.enabledLayerCount = (uint32_t)validationLayers.size();
    instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();
  }

  BB_VK_ASSERT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

  volkLoadInstance(instance);

  bool running = true;
  SDL_Event e = {};
  while (running) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        running = false;
      }
    }

    // update(1.f / 60.f);
    // draw();
  }

  vkDestroyInstance(instance, nullptr);

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}