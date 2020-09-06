#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include "external/SDL2/SDL_main.h"
#include "external/SDL2/SDL_syswm.h"
#include "external/SDL2/SDL_vulkan.h"
#include "external/fmt/format.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include "external/stb_image.h"
#include <chrono>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <array>
#include <optional>
#include <numeric>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#if BB_DEBUG
#if BB_WINDOWS
#define BB_ASSERT(exp)                                                         \
  do {                                                                         \
    if (!(exp)) {                                                              \
      BB_LOG_ERROR("ASSERT TRIGGERED: {}", #exp);                              \
      __debugbreak();                                                          \
    }                                                                          \
  } while (0)
#else
#define BB_ASSERT(exp) assert(exp)
#endif
#define BB_LOG_INFO(...) bb::log(bb::LogLevel::Info, __VA_ARGS__)
#define BB_LOG_WARNING(...) bb::log(bb::LogLevel::Warning, __VA_ARGS__)
#define BB_LOG_ERROR(...) bb::log(bb::LogLevel::Error, __VA_ARGS__)
#else
#define BB_ASSERT(exp)
#define BB_LOG_INFO(...)
#define BB_LOG_WARNING(...)
#define BB_LOG_ERROR(...)
#endif
#define BB_VK_ASSERT(exp)                                                      \
  do {                                                                         \
    auto result##__LINE__ = exp;                                               \
    BB_ASSERT(result##__LINE__ == VK_SUCCESS);                                 \
  } while (0)

namespace bb {

template <typename T> uint32_t size_bytes32(const T &_container) {
  return (uint32_t)(sizeof(typename T::value_type) * _container.size());
}

enum class LogLevel { Info, Warning, Error };

template <typename... Args> void print(Args... args) {
  std::string formatted = fmt::format(args...);
  formatted += "\n";
  OutputDebugStringA(formatted.c_str());
}

template <typename... Args> void log(LogLevel level, Args... args) {
  switch (level) {
  case LogLevel::Info:
    OutputDebugStringA("[Info]:    ");
    break;
  case LogLevel::Warning:
    OutputDebugStringA("[Warning]: ");
    break;
  case LogLevel::Error:
    OutputDebugStringA("[Error]:   ");
    break;
  }

  print(args...);
}

template <typename E, typename T> struct EnumArray {
  T Elems[(int)(E::COUNT)] = {};

  const T &operator[](E _e) const { return Elems[(int)_e]; }

  T &operator[](E _e) { return Elems[(int)_e]; }

  T *begin() { return Elems; }

  T *end() { return Elems + (size_t)(E::COUNT); }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

using Time = std::chrono::time_point<std::chrono::high_resolution_clock>;

Time getCurrentTime() { return std::chrono::high_resolution_clock::now(); }

static_assert(sizeof(Time) <= sizeof(Time *));
float getElapsedTimeInSeconds(Time _start, Time _end) {
  float result = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(
                             _end - _start)
                             .count()) /
                 1000.f;
  return result;
}

constexpr float pi32 = 3.141592f;

float degToRad(float _degrees) { return _degrees * pi32 / 180.f; }

float radToDeg(float _radians) { return _radians * 180.f / pi32; }

struct Int2 {
  int X = 0;
  int Y = 0;

  VkExtent2D toExtent2D() {
    BB_ASSERT(X > 0 && Y > 0);
    return {(uint32_t)X, (uint32_t)Y};
  }

  VkExtent3D toExtent3D() {
    BB_ASSERT(X > 0 && Y > 0);
    return {(uint32_t)X, (uint32_t)Y, 1};
  }
};

struct Float2 {
  float X = 0.f;
  float Y = 0.f;
};

struct Float3 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;

  static Float3 fromAssimpVector3(const aiVector3D &_aiVec3) {
    Float3 result = {_aiVec3.x, _aiVec3.y, _aiVec3.z};
    return result;
  }

  float lengthSq() const {
    float result = X * X + Y * Y + Z * Z;
    return result;
  }

  float length() const {
    float result = sqrtf(lengthSq());
    return result;
  }

  Float3 normalize() const {
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

  Float4 row(int _n) const {
    BB_ASSERT(_n >= 0 && _n < 4);
    return {M[0][_n], M[1][_n], M[2][_n], M[3][_n]};
  }

  Float4 column(int _n) const {
    BB_ASSERT(_n >= 0 && _n < 4);
    return {M[_n][0], M[_n][1], M[_n][2], M[_n][3]};
  }

  static Mat4 identity() {
    return {{
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    }};
  }

  static Mat4 translate(const Float3 &_delta) {
    // clang-format off
    return {{
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {_delta.X, _delta.Y, _delta.Z, 1},
    }};
    // clang-format on
  }

  static Mat4 scale(const Float3 &_scale) {
    // clang-format off
    return {{
      {_scale.X, 0, 0, 0},
      {0, _scale.Y, 0, 0},
      {0, 0, _scale.Z, 0},
      {0, 0, 0, 1},
    }};
    // clang-format on
  }

  static Mat4 rotateX(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {1, 0,   0,  0},
      {0, cr,  sr, 0},
      {0, -sr, cr, 0},
      {0, 0,   0,  1},
    }};
    // clang-format on
  };

  static Mat4 rotateY(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {cr,  0, sr, 0},
      {0,   1, 0,  0},
      {-sr, 0, cr, 0},
      {0,   0, 0,  1},
    }};
    // clang-format on
  }

  static Mat4 rotateZ(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {cr,  sr, 0, 0},
      {-sr, cr, 0, 0},
      {0,   0,  1, 0},
      {0,   0,  0, 1},
    }};
    // clang-format on
  }

  static Mat4 lookAt(const Float3 &_eye, const Float3 &_target,
                     const Float3 &_upAxis = {0, 1, 0}) {
    Float3 forward = (_target - _eye).normalize();
    Float3 right = cross(_upAxis, forward).normalize();
    Float3 up = cross(forward, right).normalize();

    // clang-format off
    return {{
      {right.X, up.X, forward.X, 0},
      {right.Y, up.Y, forward.Y, 0},
      {right.Z, up.Z, forward.Z, 0},
      {-dot(_eye, right), -dot(_eye, up), -dot(_eye, forward), 1},
    }};
    // clang-format on
  }

  static Mat4 perspective(float _fovDegrees, float aspectRatio, float nearZ,
                          float farZ) {
    float d = tan(degToRad(_fovDegrees) * 0.5f);
    float fSubN = farZ - nearZ;
    // clang-format off
    Mat4 result = {{
      {d / aspectRatio, 0, 0,                    0},
      {0,               -d, 0,                    0},
      {0,               0, -nearZ / fSubN,       1},
      {0,               0, nearZ * farZ / fSubN, 0},
    }};
    // clang-format on
    return result;
  }
};

inline Mat4 operator*(const Mat4 &_a, const Mat4 &_b) {
  Float4 rows[4] = {_a.row(0), _a.row(1), _a.row(2), _a.row(3)};
  Float4 columns[4] = {_b.column(0), _b.column(1), _b.column(2), _b.column(3)};
  Mat4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.M[i][j] = dot(rows[i], columns[j]);
    }
  }
  return result;
}

struct UniformBlock {
  Mat4 ModelMat;
  Mat4 ViewMat;
  Mat4 ProjMat;
};

struct Vertex {
  Float3 Pos;
  Float3 Color;
  Float2 UV;

  static VkVertexInputBindingDescription getBindingDesc() {
    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDesc;
  }

  static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescs() {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescs = {};
    attributeDescs[0].binding = 0;
    attributeDescs[0].location = 0;
    attributeDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[0].offset = offsetof(Vertex, Pos);
    attributeDescs[1].binding = 0;
    attributeDescs[1].location = 1;
    attributeDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[1].offset = offsetof(Vertex, Color);
    attributeDescs[2].binding = 0;
    attributeDescs[2].location = 2;
    attributeDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[2].offset = offsetof(Vertex, UV);

    return attributeDescs;
  }
};

VKAPI_ATTR VkBool32 VKAPI_CALL
vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT _severity,
                    VkDebugUtilsMessageTypeFlagsEXT _type,
                    const VkDebugUtilsMessengerCallbackDataEXT *_callbackData,
                    void *_userData) {
  printf("%s\n", _callbackData->pMessage);
  switch (_severity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    BB_LOG_INFO("Vulkan validation: {}", _callbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    BB_LOG_WARNING("Vulkan validation: {}", _callbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    BB_LOG_ERROR("Vulkan validation: {}", _callbackData->pMessage);
    break;
  default:
    break;
  }

  BB_ASSERT(_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);

  return VK_FALSE;
}

struct QueueFamilyIndices {
  std::optional<uint32_t> Graphics;
  std::optional<uint32_t> Transfer0;
  std::optional<uint32_t> Present;
  std::optional<uint32_t> Compute;

  bool isCompleted() const {
    return Graphics.has_value() && Transfer0.has_value() &&
           Present.has_value() && Compute.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR Capabilities;
  std::vector<VkSurfaceFormatKHR> Formats;
  std::vector<VkPresentModeKHR> PresentModes;

  VkSurfaceFormatKHR chooseSurfaceFormat() const {
    for (const VkSurfaceFormatKHR &format : Formats) {
      if ((format.format == VK_FORMAT_R8G8B8A8_SRGB ||
           format.format == VK_FORMAT_B8G8R8A8_SRGB) &&
          format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }

    return Formats[0];
  }

  // TODO(JJJ): Mailbox mod have some issue.
  VkPresentModeKHR choosePresentMode() const {
    // for (VkPresentModeKHR presentMode : PresentModes) {
    //   if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
    //     return presentMode;
    //   }
    // }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D chooseExtent(uint32_t _width, uint32_t _height) const {
    if (Capabilities.currentExtent.width != UINT32_MAX) {
      return Capabilities.currentExtent;
    } else {
      VkExtent2D extent;
      extent.width = std::clamp(_width, Capabilities.minImageExtent.width,
                                Capabilities.maxImageExtent.width);
      extent.height = std::clamp(_height, Capabilities.minImageExtent.height,
                                 Capabilities.maxImageExtent.height);
      return extent;
    }
  }
};

QueueFamilyIndices getQueueFamily(VkPhysicalDevice _physicalDevice,
                                  VkSurfaceKHR _surface) {
  QueueFamilyIndices result = {};

  uint32_t numQueueFamilyProperties = 0;
  std::vector<VkQueueFamilyProperties> queueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice,
                                           &numQueueFamilyProperties, nullptr);
  queueFamilyProperties.resize(numQueueFamilyProperties);
  vkGetPhysicalDeviceQueueFamilyProperties(
      _physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  for (uint32_t i = 0; i < numQueueFamilyProperties; i++) {
    // Make queues exclusive per task.
    if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        !result.Graphics.has_value()) {
      result.Graphics = i;
    } else if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
               !result.Transfer0.has_value()) {
      result.Transfer0 = i;
    } else if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
               !result.Compute.has_value()) {
      result.Compute = i;
    } else if (!result.Present.has_value()) {
      VkBool32 supportPresent = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, i, _surface,
                                           &supportPresent);
      if (supportPresent) {
        result.Present = i;
      }
    }

    if (result.isCompleted())
      break;
  }

  // Failed to retrieve unique queue family index per queue type - get
  // duplicated queue family index with others
  if (!result.isCompleted()) {
    for (uint32_t i = 0; i < numQueueFamilyProperties; i++) {
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          !result.Graphics.has_value()) {
        result.Graphics = i;
      }
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
          !result.Transfer0.has_value()) {
        result.Transfer0 = i;
      }
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
          !result.Compute.has_value()) {
        result.Compute = i;
      }
      if (!result.Present.has_value()) {
        VkBool32 supportPresent = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, i, _surface,
                                             &supportPresent);
        if (supportPresent) {
          result.Present = i;
        }
      }
    }
  }

  return result;
}

bool checkPhysicalDevice(VkPhysicalDevice _physicalDevice,
                         VkSurfaceKHR _surface,
                         const std::vector<const char *> &_deviceExtensions,
                         VkPhysicalDeviceFeatures *_outDeviceFeatures,
                         QueueFamilyIndices *_outQueueFamilyIndices,
                         SwapChainSupportDetails *_outSwapChainSupportDetails) {
  VkPhysicalDeviceProperties deviceProperties = {};
  VkPhysicalDeviceFeatures deviceFeatures = {};

  vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
  vkGetPhysicalDeviceFeatures(_physicalDevice, &deviceFeatures);

  QueueFamilyIndices queueFamilyIndices =
      getQueueFamily(_physicalDevice, _surface);

  // Check if all required extensions are supported
  uint32_t numExtensions;
  std::vector<VkExtensionProperties> extensionProperties;
  vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numExtensions,
                                       nullptr);
  extensionProperties.resize(numExtensions);
  vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numExtensions,
                                       extensionProperties.data());
  bool areAllExtensionsSupported = true;
  for (const char *extensionName : _deviceExtensions) {
    bool found = false;

    for (const VkExtensionProperties &properties : extensionProperties) {
      if (strcmp(extensionName, properties.extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      areAllExtensionsSupported = false;
      break;
    }
  }

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface,
                                            &surfaceCapabilities);
  uint32_t numSurfaceFormats;
  std::vector<VkSurfaceFormatKHR> surfaceFormats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface,
                                       &numSurfaceFormats, nullptr);
  surfaceFormats.resize(numSurfaceFormats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(
      _physicalDevice, _surface, &numSurfaceFormats, surfaceFormats.data());

  uint32_t numSurfacePresentModes;
  std::vector<VkPresentModeKHR> surfacePresentModes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface,
                                            &numSurfacePresentModes, nullptr);
  surfacePresentModes.resize(numSurfacePresentModes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface,
                                            &numSurfacePresentModes,
                                            surfacePresentModes.data());
  bool isSwapChainAdequate =
      (numSurfaceFormats > 0) && (numSurfacePresentModes > 0);
  if (_outSwapChainSupportDetails) {
    *_outSwapChainSupportDetails = {};
    _outSwapChainSupportDetails->Capabilities = surfaceCapabilities;
    _outSwapChainSupportDetails->Formats = std::move(surfaceFormats);
    _outSwapChainSupportDetails->PresentModes = std::move(surfacePresentModes);
  }

  bool isProperType =
      (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
      (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
  bool isFeatureComplete =
      deviceFeatures.geometryShader && deviceFeatures.tessellationShader &&
      deviceFeatures.fillModeNonSolid && deviceFeatures.depthClamp &&
      deviceFeatures.samplerAnisotropy;
  bool isQueueComplete = queueFamilyIndices.isCompleted();

  if (_outDeviceFeatures) {
    *_outDeviceFeatures = deviceFeatures;
  }
  if (_outQueueFamilyIndices) {
    *_outQueueFamilyIndices = queueFamilyIndices;
  }

  return areAllExtensionsSupported && isSwapChainAdequate && isProperType &&
         isFeatureComplete && isQueueComplete;
}

uint32_t findMemoryType(VkPhysicalDevice _physicalDevice, uint32_t _typeFilter,
                        VkMemoryPropertyFlags _properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if ((_typeFilter & (1 << i)) &&
        ((memProperties.memoryTypes[i].propertyFlags & _properties) ==
         _properties)) {
      return i;
    }
  }

  BB_ASSERT(false);
  return 0;
}

struct Buffer {
  VkBuffer Handle;
  VkDeviceMemory Memory;
  uint32_t Size;
};

struct SwapChain {
  VkSwapchainKHR Handle;
  VkFormat ColorFormat;
  VkFormat DepthFormat;
  VkExtent2D Extent;

  uint32_t NumColorImages;
  std::vector<VkImage> ColorImages;
  std::vector<VkImageView> ColorImageViews;
  VkImage DepthImage;
  VkDeviceMemory DepthImageMemory;
  VkImageView DepthImageView;
};

SwapChain createSwapChain(
    VkDevice _device, VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
    const SwapChainSupportDetails &_swapChainSupportDetails, uint32_t _width,
    uint32_t _height, const QueueFamilyIndices &_queueFamilyIndices,
    const SwapChain *_oldSwapChain = nullptr) {
  VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
  swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapChainCreateInfo.surface = _surface;
  swapChainCreateInfo.minImageCount =
      _swapChainSupportDetails.Capabilities.minImageCount + 1;
  if (_swapChainSupportDetails.Capabilities.maxImageCount > 0 &&
      swapChainCreateInfo.minImageCount >
          _swapChainSupportDetails.Capabilities.maxImageCount) {
    swapChainCreateInfo.minImageCount =
        _swapChainSupportDetails.Capabilities.maxImageCount;
  }
  VkSurfaceFormatKHR surfaceFormat =
      _swapChainSupportDetails.chooseSurfaceFormat();
  swapChainCreateInfo.imageFormat = surfaceFormat.format;
  swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
  swapChainCreateInfo.imageExtent =
      _swapChainSupportDetails.chooseExtent(_width, _height);
  swapChainCreateInfo.imageArrayLayers = 1;
  swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t sharedQueueFamilyIndices[2] = {_queueFamilyIndices.Graphics.value(),
                                          _queueFamilyIndices.Present.value()};
  if (_queueFamilyIndices.Graphics != _queueFamilyIndices.Present) {
    // TODO(ilgwon): Can be an interesting optimization to use EXCLUSIVE mode
    // with multiple queues
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapChainCreateInfo.queueFamilyIndexCount = 2;
    swapChainCreateInfo.pQueueFamilyIndices = sharedQueueFamilyIndices;
  } else {
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  swapChainCreateInfo.preTransform =
      _swapChainSupportDetails.Capabilities.currentTransform;
  swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapChainCreateInfo.presentMode =
      _swapChainSupportDetails.choosePresentMode();
  swapChainCreateInfo.clipped = VK_TRUE;
  if (_oldSwapChain) {
    swapChainCreateInfo.oldSwapchain = _oldSwapChain->Handle;
  } else {
    swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
  }

  SwapChain swapChain;
  swapChain.ColorFormat = swapChainCreateInfo.imageFormat;
  swapChain.Extent = swapChainCreateInfo.imageExtent;
  BB_VK_ASSERT(vkCreateSwapchainKHR(_device, &swapChainCreateInfo, nullptr,
                                    &swapChain.Handle));

  vkGetSwapchainImagesKHR(_device, swapChain.Handle, &swapChain.NumColorImages,
                          nullptr);
  swapChain.ColorImages.resize(swapChain.NumColorImages);
  vkGetSwapchainImagesKHR(_device, swapChain.Handle, &swapChain.NumColorImages,
                          swapChain.ColorImages.data());
  swapChain.ColorImageViews.resize(swapChain.NumColorImages);
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkImageViewCreateInfo swapChainImageViewCreateInfo = {};
    swapChainImageViewCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    swapChainImageViewCreateInfo.image = swapChain.ColorImages[i];
    swapChainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    swapChainImageViewCreateInfo.format = swapChain.ColorFormat;
    swapChainImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    swapChainImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    swapChainImageViewCreateInfo.subresourceRange.levelCount = 1;
    swapChainImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    swapChainImageViewCreateInfo.subresourceRange.layerCount = 1;
    BB_VK_ASSERT(vkCreateImageView(_device, &swapChainImageViewCreateInfo,
                                   nullptr, &swapChain.ColorImageViews[i]));
  }

  // TODO(ilgwon): Support varius depth formats
  swapChain.DepthFormat = VK_FORMAT_D32_SFLOAT;

  VkImageCreateInfo depthImageCreateInfo = {};
  depthImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  depthImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  depthImageCreateInfo.extent.width = _width;
  depthImageCreateInfo.extent.height = _height;
  depthImageCreateInfo.extent.depth = 1;
  depthImageCreateInfo.mipLevels = 1;
  depthImageCreateInfo.arrayLayers = 1;
  depthImageCreateInfo.format = swapChain.DepthFormat;
  depthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  depthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  depthImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  depthImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  depthImageCreateInfo.flags = 0;
  BB_VK_ASSERT(vkCreateImage(_device, &depthImageCreateInfo, nullptr,
                             &swapChain.DepthImage));

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(_device, swapChain.DepthImage, &memRequirements);

  VkMemoryAllocateInfo depthImageMemoryAllocInfo = {};
  depthImageMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  depthImageMemoryAllocInfo.allocationSize = memRequirements.size;
  depthImageMemoryAllocInfo.memoryTypeIndex =
      findMemoryType(_physicalDevice, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  BB_VK_ASSERT(vkAllocateMemory(_device, &depthImageMemoryAllocInfo, nullptr,
                                &swapChain.DepthImageMemory));

  BB_VK_ASSERT(vkBindImageMemory(_device, swapChain.DepthImage,
                                 swapChain.DepthImageMemory, 0));

  VkImageViewCreateInfo depthImageViewCreateInfo = {};
  depthImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  depthImageViewCreateInfo.image = swapChain.DepthImage;
  depthImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  depthImageViewCreateInfo.format = swapChain.DepthFormat;
  depthImageViewCreateInfo.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_DEPTH_BIT;
  depthImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
  depthImageViewCreateInfo.subresourceRange.levelCount = 1;
  depthImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
  depthImageViewCreateInfo.subresourceRange.layerCount = 1;
  BB_VK_ASSERT(vkCreateImageView(_device, &depthImageViewCreateInfo, nullptr,
                                 &swapChain.DepthImageView));

  return swapChain;
}

void destroySwapChain(VkDevice _device, SwapChain &_swapChain) {
  for (VkImageView imageView : _swapChain.ColorImageViews) {
    vkDestroyImageView(_device, imageView, nullptr);
  }
  vkDestroyImageView(_device, _swapChain.DepthImageView, nullptr);
  vkDestroyImage(_device, _swapChain.DepthImage, nullptr);
  vkFreeMemory(_device, _swapChain.DepthImageMemory, nullptr);
  vkDestroySwapchainKHR(_device, _swapChain.Handle, nullptr);
  _swapChain = {};
}

void recordCommand(VkCommandBuffer _cmdBuffer, VkRenderPass _renderPass,
                   VkFramebuffer _swapChainFramebuffer,
                   VkExtent2D _swapChainExtent, VkPipeline _graphicsPipeline,
                   const Buffer &_vertexBuffer, const Buffer &_indexBuffer,
                   VkImage textureImage,
                   const QueueFamilyIndices &queueFamilyIndices,
                   VkPipelineLayout _pipelineLayout,
                   VkDescriptorSet _descriptorSet,
                   const std::vector<uint32_t> &_indices) {

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = 0;
  cmdBeginInfo.pInheritanceInfo = nullptr;

  BB_VK_ASSERT(vkBeginCommandBuffer(_cmdBuffer, &cmdBeginInfo));

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _renderPass;
  renderPassInfo.framebuffer = _swapChainFramebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapChainExtent;

  VkClearValue clearValues[2] = {0.f, 0.f, 0.f, 1.f};
  clearValues[0].color = {0, 0, 0, 1};
  clearValues[1].depthStencil = {0, 0};
  renderPassInfo.clearValueCount = (uint32_t)std::size(clearValues);
  renderPassInfo.pClearValues = clearValues;

  vkCmdBeginRenderPass(_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _graphicsPipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(_cmdBuffer, 0, 1, &_vertexBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(_cmdBuffer, _indexBuffer.Handle, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
  vkCmdDrawIndexed(_cmdBuffer, (uint32_t)_indices.size(), 1, 0, 0, 0);
  vkCmdEndRenderPass(_cmdBuffer);

  BB_VK_ASSERT(vkEndCommandBuffer(_cmdBuffer));
}

void initReloadableResources(
    VkDevice _device, VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
    const SwapChainSupportDetails &_swapChainSupportDetails, uint32_t _width,
    uint32_t _height, const QueueFamilyIndices &_queueFamilyIndices,
    const SwapChain *_oldSwapChain, VkShaderModule _vertShader,
    VkShaderModule _fragShader, VkPipelineLayout _pipelineLayout,
    SwapChain *_outSwapChain, VkRenderPass *_outRenderPass,
    VkPipeline *_outGraphicsPipeline,
    std::vector<VkFramebuffer> *_outSwapChainFramebuffers) {

  SwapChain swapChain = createSwapChain(
      _device, _physicalDevice, _surface, _swapChainSupportDetails, _width,
      _height, _queueFamilyIndices, _oldSwapChain);

  VkPipelineShaderStageCreateInfo shaderStages[2] = {};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = _vertShader;
  shaderStages[0].pName = "main";
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = _fragShader;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription bindingDesc = Vertex::getBindingDesc();
  std::array<VkVertexInputAttributeDescription, 3> attributeDescs =
      Vertex::getAttributeDescs();
  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount = 1;
  vertexInputState.pVertexBindingDescriptions = &bindingDesc;
  vertexInputState.vertexAttributeDescriptionCount =
      (uint32_t)attributeDescs.size();
  vertexInputState.pVertexAttributeDescriptions = attributeDescs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
  inputAssemblyState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssemblyState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.width = (float)swapChain.Extent.width;
  viewport.height = (float)swapChain.Extent.height;
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapChain.Extent;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizationState = {};
  rasterizationState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.depthClampEnable = VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.lineWidth = 1.f;
  rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.depthBiasConstantFactor = 0.f;
  rasterizationState.depthBiasClamp = 0.f;
  rasterizationState.depthBiasSlopeFactor = 0.f;

  VkPipelineMultisampleStateCreateInfo multisampleState = {};
  multisampleState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.sampleShadingEnable = VK_FALSE;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampleState.minSampleShading = 1.f;
  multisampleState.pSampleMask = nullptr;
  multisampleState.alphaToCoverageEnable = VK_FALSE;
  multisampleState.alphaToOneEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
  depthStencilState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  depthStencilState.depthBoundsTestEnable = VK_FALSE;
  depthStencilState.minDepthBounds = 1.f;
  depthStencilState.maxDepthBounds = 0.f;
  depthStencilState.stencilTestEnable = VK_FALSE;
  // depthStencilState.front = {};
  // depthStencilState.back = {};

  VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
  colorBlendAttachmentState.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachmentState.blendEnable = VK_FALSE;
  colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlendState = {};
  colorBlendState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.logicOpEnable = VK_FALSE;
  colorBlendState.logicOp = VK_LOGIC_OP_COPY;
  colorBlendState.attachmentCount = 1;
  colorBlendState.pAttachments = &colorBlendAttachmentState;
  colorBlendState.blendConstants[0] = 0.f;
  colorBlendState.blendConstants[1] = 0.f;
  colorBlendState.blendConstants[2] = 0.f;
  colorBlendState.blendConstants[3] = 0.f;

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = swapChain.ColorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment = {};
  depthAttachment.format = swapChain.DepthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef = {};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency subpassDependency = {};
  subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpassDependency.dstSubpass = 0;
  subpassDependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.srcAccessMask = 0;
  subpassDependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassCreateInfo = {};
  renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
  renderPassCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
  renderPassCreateInfo.pAttachments = attachments;
  renderPassCreateInfo.subpassCount = 1;
  renderPassCreateInfo.pSubpasses = &subpass;
  renderPassCreateInfo.dependencyCount = 1;
  renderPassCreateInfo.pDependencies = &subpassDependency;

  VkRenderPass renderPass;
  BB_VK_ASSERT(
      vkCreateRenderPass(_device, &renderPassCreateInfo, nullptr, &renderPass));

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.stageCount = 2;
  pipelineCreateInfo.pStages = shaderStages;
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;
  pipelineCreateInfo.pDynamicState = nullptr;
  pipelineCreateInfo.layout = _pipelineLayout;
  pipelineCreateInfo.renderPass = renderPass;
  pipelineCreateInfo.subpass = 0;
  pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCreateInfo.basePipelineIndex = -1;

  VkPipeline graphicsPipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
                                         &pipelineCreateInfo, nullptr,
                                         &graphicsPipeline));

  std::vector<VkFramebuffer> swapChainFramebuffers(swapChain.NumColorImages);
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = renderPass;
    VkImageView attachments[] = {swapChain.ColorImageViews[i],
                                 swapChain.DepthImageView};
    fbCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
    fbCreateInfo.pAttachments = attachments;
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_device, &fbCreateInfo, nullptr,
                                     &swapChainFramebuffers[i]));
  }

  *_outSwapChain = std::move(swapChain);
  *_outSwapChainFramebuffers = std::move(swapChainFramebuffers);
  *_outRenderPass = renderPass;
  *_outGraphicsPipeline = graphicsPipeline;
}

void cleanupReloadableResources(
    VkDevice _device, SwapChain &_swapChain, VkRenderPass &_renderPass,
    VkPipeline &_graphicsPipeline,
    std::vector<VkFramebuffer> &_swapChainFramebuffers) {
  for (VkFramebuffer fb : _swapChainFramebuffers) {
    vkDestroyFramebuffer(_device, fb, nullptr);
  }
  _swapChainFramebuffers.clear();
  vkDestroyPipeline(_device, _graphicsPipeline, nullptr);
  _graphicsPipeline = VK_NULL_HANDLE;
  vkDestroyRenderPass(_device, _renderPass, nullptr);
  _renderPass = VK_NULL_HANDLE;
  destroySwapChain(_device, _swapChain);
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void onWindowResize(SDL_Window *_window, VkDevice _device,
                    VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
                    const SwapChainSupportDetails &_swapChainSupportDetails,
                    const QueueFamilyIndices &_queueFamilyIndices,
                    VkShaderModule _vertShader, VkShaderModule _fragShader,
                    VkPipelineLayout _pipelineLayout, SwapChain &_swapChain,
                    VkRenderPass &_renderPass, VkPipeline &_graphicsPipeline,
                    std::vector<VkFramebuffer> &_swapChainFramebuffers) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(_device); // Ensure that device finished using swap chain.

  cleanupReloadableResources(_device, _swapChain, _renderPass,
                             _graphicsPipeline, _swapChainFramebuffers);

  initReloadableResources(
      _device, _physicalDevice, _surface, _swapChainSupportDetails, width,
      height, _queueFamilyIndices, nullptr, _vertShader, _fragShader,
      _pipelineLayout, &_swapChain, &_renderPass, &_graphicsPipeline,
      &_swapChainFramebuffers);
}

VkShaderModule createShaderModuleFromFile(VkDevice _device,
                                          const std::string &_filePath) {
  FILE *f = fopen(_filePath.c_str(), "rb");
  BB_ASSERT(f);
  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  rewind(f);
  uint8_t *contents = new uint8_t[fileSize];
  fread(contents, sizeof(*contents), fileSize, f);

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = fileSize;
  createInfo.pCode = (uint32_t *)contents;
  VkShaderModule shaderModule;
  BB_VK_ASSERT(
      vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule));

  delete[] contents;
  fclose(f);

  return shaderModule;
}

Buffer createBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                    VkDeviceSize _size, VkBufferUsageFlags _usage,
                    VkMemoryPropertyFlags _properties) {
  Buffer result = {};

  VkBufferCreateInfo bufferCreateInfo = {};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = _size;
  bufferCreateInfo.usage = _usage;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  BB_VK_ASSERT(
      vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &result.Handle));

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(_device, result.Handle, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      _physicalDevice, memRequirements.memoryTypeBits, _properties);

  BB_VK_ASSERT(vkAllocateMemory(_device, &allocInfo, nullptr, &result.Memory));
  BB_VK_ASSERT(vkBindBufferMemory(_device, result.Handle, result.Memory, 0));

  result.Size = _size;

  return result;
};

Buffer createStagingBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                           const Buffer &_orgBuffer) {
  Buffer result = createBuffer(_device, _physicalDevice, _orgBuffer.Size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  return result;
}

void destroyBuffer(VkDevice _device, Buffer &_buffer) {
  vkDestroyBuffer(_device, _buffer.Handle, nullptr);
  _buffer.Handle = VK_NULL_HANDLE;
  vkFreeMemory(_device, _buffer.Memory, nullptr);
  _buffer.Memory = VK_NULL_HANDLE;
}

void copyBuffer(VkDevice _device, VkCommandPool _cmdPool, VkQueue _queue,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size) {
  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = _cmdPool;
  cmdBufferAllocInfo.commandBufferCount = 1;
  VkCommandBuffer cmdBuffer;
  BB_VK_ASSERT(
      vkAllocateCommandBuffers(_device, &cmdBufferAllocInfo, &cmdBuffer));

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

  VkBufferCopy copyRegion = {};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = _size;
  vkCmdCopyBuffer(cmdBuffer, _srcBuffer.Handle, _dstBuffer.Handle, 1,
                  &copyRegion);

  BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmdBuffer;
  vkQueueSubmit(_queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(_queue);

  vkFreeCommandBuffers(_device, cmdBufferAllocInfo.commandPool, 1, &cmdBuffer);
}

#if 0
struct Image {
  VkImage Handle;
  VkDeviceMemory Memory;
};

Image createImage() {}
#endif

} // namespace bb

int main(int _argc, char **_argv) {
  using namespace bb;

  BB_VK_ASSERT(volkInitialize());

  SDL_Init(SDL_INIT_VIDEO);
  int width = 1280;
  int height = 720;
  SDL_Window *window = SDL_CreateWindow(
      "Bibim Renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  SDL_SysWMinfo sysinfo = {};
  SDL_VERSION(&sysinfo.version);
  SDL_GetWindowWMInfo(window, &sysinfo);

  std::string resourceRootPath = SDL_GetBasePath();
  resourceRootPath += "\\..\\..\\resources\\";

  Assimp::Importer importer;
  const aiScene *shaderBallScene =
      importer.ReadFile(resourceRootPath + "ShaderBall.fbx",
                        aiProcess_Triangulate | aiProcess_FlipUVs);
  const aiMesh *shaderBallMesh = shaderBallScene->mMeshes[0];
  std::vector<Vertex> shaderBallVertices;
  shaderBallVertices.reserve(shaderBallMesh->mNumFaces * 3);
  for (unsigned int i = 0; i < shaderBallMesh->mNumFaces; ++i) {
    const aiFace &face = shaderBallMesh->mFaces[i];
    BB_ASSERT(face.mNumIndices == 3);

    for (int j = 0; j < 3; ++j) {
      Vertex v = {};
      v.Pos = Float3::fromAssimpVector3(
          shaderBallMesh->mVertices[face.mIndices[j]]);
      std::swap(v.Pos.Y, v.Pos.Z);
      v.Pos.Z *= -1.f;
      v.UV.X = shaderBallMesh->mTextureCoords[0][face.mIndices[j]].x;
      v.UV.Y = shaderBallMesh->mTextureCoords[0][face.mIndices[j]].y;
      shaderBallVertices.push_back(v);
    }
  }

  std::vector<uint32_t> shaderBallIndices;
  shaderBallIndices.resize(shaderBallVertices.size());
  std::iota(shaderBallIndices.begin(), shaderBallIndices.end(), 0);

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

  std::vector<const char *> extensions;
  // TODO: ADD vk_win32_surface extension.
  unsigned numInstantExtensions = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &numInstantExtensions, nullptr);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  unsigned numExtraInstantExtensions = extensions.size();
  extensions.resize(numExtraInstantExtensions + numInstantExtensions);

  SDL_Vulkan_GetInstanceExtensions(window, &numInstantExtensions,
                                   extensions.data() +
                                       numExtraInstantExtensions);

  instanceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
  instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

  BB_VK_ASSERT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
  volkLoadInstance(instance);

  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  if (enableValidationLayers) {
    VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {};
    messengerCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerCreateInfo.pfnUserCallback = &vulkanDebugCallback;
    BB_VK_ASSERT(vkCreateDebugUtilsMessengerEXT(instance, &messengerCreateInfo,
                                                nullptr, &messenger));
  }

  VkSurfaceKHR surface = {};
  BB_VK_ASSERT(!SDL_Vulkan_CreateSurface(
      window, instance, &surface)); // ! to convert SDL_bool to VkResult

  uint32_t numPhysicalDevices = 0;
  std::vector<VkPhysicalDevice> physicalDevices;
  BB_VK_ASSERT(
      vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr));
  physicalDevices.resize(numPhysicalDevices);
  BB_VK_ASSERT(vkEnumeratePhysicalDevices(instance, &numPhysicalDevices,
                                          physicalDevices.data()));

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceFeatures deviceFeatures = {};
  QueueFamilyIndices queueFamilyIndices = {};
  SwapChainSupportDetails swapChainSupportDetails = {};

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  for (VkPhysicalDevice currentPhysicalDevice : physicalDevices) {
    if (checkPhysicalDevice(currentPhysicalDevice, surface, deviceExtensions,
                            &deviceFeatures, &queueFamilyIndices,
                            &swapChainSupportDetails)) {
      physicalDevice = currentPhysicalDevice;
      break;
    }
  }
  BB_ASSERT(physicalDevice != VK_NULL_HANDLE);

  std::unordered_map<uint32_t, VkQueue> queueMap;
  queueMap[queueFamilyIndices.Graphics.value()] = VK_NULL_HANDLE;
  queueMap[queueFamilyIndices.Transfer0.value()] = VK_NULL_HANDLE;
  queueMap[queueFamilyIndices.Present.value()] = VK_NULL_HANDLE;
  queueMap[queueFamilyIndices.Compute.value()] = VK_NULL_HANDLE;

  float queuePriority = 1.f;
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  queueCreateInfos.reserve(queueMap.size());

  for (auto [queueFamilyIndex, _] : queueMap) {
    VkDeviceQueueCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndex;
    createInfo.queueCount = 1;
    createInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(createInfo);
  }

  VkDeviceCreateInfo deviceCreateInfo = {};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
  deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
  deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
  deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
  VkDevice device;
  BB_VK_ASSERT(
      vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

  for (auto &[queueFamilyIndex, queue] : queueMap) {
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
  }

  VkQueue graphicsQueue = queueMap[queueFamilyIndices.Graphics.value()];
  VkQueue transferQueue = queueMap[queueFamilyIndices.Transfer0.value()];
  VkQueue presentQueue = queueMap[queueFamilyIndices.Present.value()];
  VkQueue computeQueue = queueMap[queueFamilyIndices.Compute.value()];
  BB_ASSERT(graphicsQueue != VK_NULL_HANDLE &&
            transferQueue != VK_NULL_HANDLE && presentQueue != VK_NULL_HANDLE &&
            computeQueue != VK_NULL_HANDLE);

  VkShaderModule testVertShader = createShaderModuleFromFile(
      device, resourceRootPath + "..\\src\\shaders\\test.vert.spv");
  VkShaderModule testFragShader = createShaderModuleFromFile(
      device, resourceRootPath + "..\\src\\shaders\\test.frag.spv");

  VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[2] = {};
  descriptorSetLayoutBindings[0].binding = 0;
  descriptorSetLayoutBindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorSetLayoutBindings[0].descriptorCount = 1;
  descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
  descriptorSetLayoutBindings[1].binding = 1;
  descriptorSetLayoutBindings[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorSetLayoutBindings[1].descriptorCount = 1;
  descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount =
      (uint32_t)std::size(descriptorSetLayoutBindings);
  descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;

  VkDescriptorSetLayout descriptorSetLayout;
  BB_VK_ASSERT(vkCreateDescriptorSetLayout(
      device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCreateInfo.setLayoutCount = 1;
  pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout pipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo,
                                      nullptr, &pipelineLayout));

  SwapChain swapChain;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  initReloadableResources(
      device, physicalDevice, surface, swapChainSupportDetails, width, height,
      queueFamilyIndices, nullptr, testVertShader, testFragShader,
      pipelineLayout, &swapChain, &renderPass, &graphicsPipeline,
      &swapChainFramebuffers);

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndices.Graphics.value();
  cmdPoolCreateInfo.flags = 0;

  std::vector<VkCommandPool> graphicsCmdPools(swapChain.NumColorImages);
  for (VkCommandPool &cmdPool : graphicsCmdPools) {
    BB_VK_ASSERT(
        vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr, &cmdPool));
  }

  cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndices.Transfer0.value();
  cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool transferCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr,
                                   &transferCmdPool));

  std::vector<VkCommandBuffer> graphicsCmdBuffers(swapChain.NumColorImages);

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = 1;
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {

    cmdBufferAllocInfo.commandPool = graphicsCmdPools[i];
    BB_VK_ASSERT(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo,
                                          &graphicsCmdBuffers[i]));
  }

  // clang-format off
  std::vector<Vertex> quadVertices = {
      {{-0.5f, -0.5f, 0}, {1.0f, 0.0f, 0.0f}, {0, 0}},
      {{0.5f, -0.5f, 0}, {0.0f, 1.0f, 0.0f}, {1, 0}},
      {{0.5f, 0.5f, 0}, {0.0f, 0.0f, 1.0f}, {1, 1}},
      {{-0.5f, 0.5f, 0}, {1.0f, 1.0f, 1.0f}, {0, 1}},

      {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0, 0}},
      {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1, 0}},
      {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1, 1}},
      {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0, 1}}};

  std::vector<uint32_t> quadIndices = {
    4, 5, 6, 6, 7, 4,
    0, 1, 2, 2, 3, 0,
  };
  // clang-format on

  Buffer quadVertexBuffer = createBuffer(
      device, physicalDevice, size_bytes32(quadVertices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  Buffer quadIndexBuffer = createBuffer(
      device, physicalDevice, size_bytes32(quadIndices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  {
    Buffer vertexStagingBuffer =
        createStagingBuffer(device, physicalDevice, quadVertexBuffer);

    Buffer indexStagingBuffer =
        createStagingBuffer(device, physicalDevice, quadIndexBuffer);
    void *data;
    vkMapMemory(device, vertexStagingBuffer.Memory, 0, vertexStagingBuffer.Size,
                0, &data);
    memcpy(data, quadVertices.data(), vertexStagingBuffer.Size);
    vkUnmapMemory(device, vertexStagingBuffer.Memory);
    vkMapMemory(device, indexStagingBuffer.Memory, 0, indexStagingBuffer.Size,
                0, &data);
    memcpy(data, quadIndices.data(), indexStagingBuffer.Size);
    vkUnmapMemory(device, indexStagingBuffer.Memory);

    copyBuffer(device, transferCmdPool, transferQueue, quadVertexBuffer,
               vertexStagingBuffer, vertexStagingBuffer.Size);
    copyBuffer(device, transferCmdPool, transferQueue, quadIndexBuffer,
               indexStagingBuffer, indexStagingBuffer.Size);

    destroyBuffer(device, vertexStagingBuffer);
    destroyBuffer(device, indexStagingBuffer);
  }

  Buffer shaderBallVertexBuffer =
      createBuffer(device, physicalDevice, size_bytes32(shaderBallVertices),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  {
    void *data;
    vkMapMemory(device, shaderBallVertexBuffer.Memory, 0,
                shaderBallVertexBuffer.Size, 0, &data);
    memcpy(data, shaderBallVertices.data(), shaderBallVertexBuffer.Size);
    vkUnmapMemory(device, shaderBallVertexBuffer.Memory);
  }
  Buffer shaderBallIndexBuffer =
      createBuffer(device, physicalDevice, size_bytes32(shaderBallIndices),
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  {
    void *data;
    vkMapMemory(device, shaderBallIndexBuffer.Memory, 0,
                shaderBallIndexBuffer.Size, 0, &data);
    memcpy(data, shaderBallIndices.data(), shaderBallIndexBuffer.Size);
    vkUnmapMemory(device, shaderBallIndexBuffer.Memory);
  }

  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  {
    std::string textureFilePath = resourceRootPath + "\\texture.jpg";
    Int2 textureDims = {};
    int numChannels;
    stbi_uc *pixels = stbi_load(textureFilePath.c_str(), &textureDims.X,
                                &textureDims.Y, &numChannels, STBI_rgb_alpha);
    BB_ASSERT(pixels);

    VkDeviceSize textureSize = textureDims.X * textureDims.Y * 4;

    Buffer textureStagingBuffer = createBuffer(
        device, physicalDevice, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *data;
    vkMapMemory(device, textureStagingBuffer.Memory, 0, textureSize, 0, &data);
    memcpy(data, pixels, textureSize);
    vkUnmapMemory(device, textureStagingBuffer.Memory);
    stbi_image_free(pixels);

    VkImageCreateInfo textureImageCreateInfo = {};
    textureImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    textureImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    textureImageCreateInfo.extent.width = (uint32_t)textureDims.X;
    textureImageCreateInfo.extent.height = (uint32_t)textureDims.Y;
    textureImageCreateInfo.extent.depth = 1;
    textureImageCreateInfo.mipLevels = 1;
    textureImageCreateInfo.arrayLayers = 1;
    textureImageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    textureImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    textureImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureImageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    textureImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    textureImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    textureImageCreateInfo.flags = 0;

    BB_VK_ASSERT(
        vkCreateImage(device, &textureImageCreateInfo, nullptr, &textureImage));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, textureImage, &memRequirements);

    VkMemoryAllocateInfo textureImageMemoryAllocateInfo = {};
    textureImageMemoryAllocateInfo.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    textureImageMemoryAllocateInfo.allocationSize = memRequirements.size;
    textureImageMemoryAllocateInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    BB_VK_ASSERT(vkAllocateMemory(device, &textureImageMemoryAllocateInfo,
                                  nullptr, &textureImageMemory));

    BB_VK_ASSERT(
        vkBindImageMemory(device, textureImage, textureImageMemory, 0));

    VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandPool = transferCmdPool;
    cmdBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    BB_VK_ASSERT(
        vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &cmdBuffer));

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = textureDims.toExtent3D();
    vkCmdCopyBufferToImage(cmdBuffer, textureStagingBuffer.Handle, textureImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (queueFamilyIndices.Transfer0 == queueFamilyIndices.Graphics) {
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    } else {
      barrier.srcQueueFamilyIndex = queueFamilyIndices.Transfer0.value();
      barrier.dstQueueFamilyIndex = queueFamilyIndices.Graphics.value();
    }
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    BB_VK_ASSERT(vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE));
    BB_VK_ASSERT(vkQueueWaitIdle(transferQueue));
    vkFreeCommandBuffers(device, transferCmdPool, 1, &cmdBuffer);

    cmdBufferAllocInfo.commandPool = graphicsCmdPools[0];
    BB_VK_ASSERT(
        vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &cmdBuffer));
    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
    submitInfo.pCommandBuffers = &cmdBuffer;
    BB_VK_ASSERT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    BB_VK_ASSERT(vkQueueWaitIdle(graphicsQueue));
    vkFreeCommandBuffers(device, graphicsCmdPools[0], 1, &cmdBuffer);

    destroyBuffer(device, textureStagingBuffer);
  }

  VkImageView textureImageView;
  VkImageViewCreateInfo textureImageViewCreateInfo = {};
  textureImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  textureImageViewCreateInfo.image = textureImage;
  textureImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  textureImageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  textureImageViewCreateInfo.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  textureImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
  textureImageViewCreateInfo.subresourceRange.levelCount = 1;
  textureImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
  textureImageViewCreateInfo.subresourceRange.layerCount = 1;
  BB_VK_ASSERT(vkCreateImageView(device, &textureImageViewCreateInfo, nullptr,
                                 &textureImageView));

  VkSampler textureSampler;
  VkSamplerCreateInfo textureSamplerCreateInfo = {};
  textureSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  textureSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
  textureSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  textureSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.anisotropyEnable = VK_TRUE;
  textureSamplerCreateInfo.maxAnisotropy = 16.f;
  textureSamplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
  textureSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
  textureSamplerCreateInfo.compareEnable = VK_FALSE;
  textureSamplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  textureSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  textureSamplerCreateInfo.mipLodBias = 0.f;
  textureSamplerCreateInfo.minLod = 0.f;
  textureSamplerCreateInfo.maxLod = 0.f;
  BB_VK_ASSERT(vkCreateSampler(device, &textureSamplerCreateInfo, nullptr,
                               &textureSampler));

  std::vector<Buffer> uniformBuffers(swapChain.NumColorImages);
  for (Buffer &uniformBuffer : uniformBuffers) {
    uniformBuffer = createBuffer(device, physicalDevice, sizeof(UniformBlock),
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }

  VkDescriptorPoolSize descriptorPoolSizes[2] = {};
  descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorPoolSizes[0].descriptorCount = swapChain.NumColorImages;
  descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorPoolSizes[1].descriptorCount = swapChain.NumColorImages;
  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
  descriptorPoolCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.poolSizeCount =
      (uint32_t)std::size(descriptorPoolSizes);
  descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes;
  descriptorPoolCreateInfo.maxSets = swapChain.NumColorImages;

  VkDescriptorPool descriptorPool;
  BB_VK_ASSERT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo,
                                      nullptr, &descriptorPool));

  std::vector<VkDescriptorSet> descriptorSets(swapChain.NumColorImages);
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts(
      swapChain.NumColorImages, descriptorSetLayout);
  VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = descriptorPool;
  descriptorSetAllocInfo.descriptorSetCount = swapChain.NumColorImages;
  descriptorSetAllocInfo.pSetLayouts = descriptorSetLayouts.data();
  BB_VK_ASSERT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo,
                                        descriptorSets.data()));
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkDescriptorBufferInfo descriptorBufferInfo = {};
    descriptorBufferInfo.buffer = uniformBuffers[i].Handle;
    descriptorBufferInfo.offset = 0;
    descriptorBufferInfo.range = sizeof(UniformBlock);

    VkDescriptorImageInfo descriptorImageInfo = {};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView = textureImageView;
    descriptorImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrites[2] = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &descriptorBufferInfo;
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &descriptorImageInfo;
    vkUpdateDescriptorSets(device, (uint32_t)std::size(descriptorWrites),
                           descriptorWrites, 0, nullptr);
  }

  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  std::vector<VkFence> renderFinishedFences(swapChain.NumColorImages);

  VkSemaphoreCreateInfo semaphoreCreateInfo = {};
  semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  BB_VK_ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                                 &imageAvailableSemaphore));
  BB_VK_ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                                 &renderFinishedSemaphore));

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    BB_VK_ASSERT(vkCreateFence(device, &fenceCreateInfo, nullptr,
                               &renderFinishedFences[i]));
  }

  uint32_t currentFrame = 0;

  bool running = true;

  Time lastTime = getCurrentTime();

  SDL_Event e = {};
  while (running) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        running = false;
      }
    }

    SDL_GetWindowSize(window, &width, &height);

    Time currentTime = getCurrentTime();
    float dt = getElapsedTimeInSeconds(lastTime, currentTime);
    lastTime = currentTime;

    VkResult acquireNextImageResult = vkAcquireNextImageKHR(
        device, swapChain.Handle, UINT64_MAX, imageAvailableSemaphore,
        VK_NULL_HANDLE, &currentFrame);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physicalDevice, surface, &swapChainSupportDetails.Capabilities);

      onWindowResize(window, device, physicalDevice, surface,
                     swapChainSupportDetails, queueFamilyIndices,
                     testVertShader, testFragShader, pipelineLayout, swapChain,
                     renderPass, graphicsPipeline, swapChainFramebuffers);
      continue;
    }

    vkWaitForFences(device, 1, &renderFinishedFences[currentFrame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(device, 1, &renderFinishedFences[currentFrame]);

    static float angle = 0;
    angle += 30.f * dt;
    if (angle > 360.f) {
      angle -= 360.f;
    }
    UniformBlock uniformBlock = {};
    uniformBlock.ModelMat =
        Mat4::rotateY(angle) * Mat4::scale({0.01f, 0.01f, 0.01f});
    uniformBlock.ViewMat = Mat4::lookAt({1, 1.5f, -1}, {0, 0, 0});
    uniformBlock.ProjMat =
        Mat4::perspective(90.f, (float)width / (float)height, 0.1f, 1000.f);
    {
      void *data;
      vkMapMemory(device, uniformBuffers[currentFrame].Memory, 0,
                  sizeof(UniformBlock), 0, &data);
      memcpy(data, &uniformBlock, sizeof(UniformBlock));
      vkUnmapMemory(device, uniformBuffers[currentFrame].Memory);
    }

    vkResetCommandPool(device, graphicsCmdPools[currentFrame],
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    recordCommand(graphicsCmdBuffers[currentFrame], renderPass,
                  swapChainFramebuffers[currentFrame], swapChain.Extent,
                  graphicsPipeline, shaderBallVertexBuffer,
                  shaderBallIndexBuffer, textureImage, queueFamilyIndices,
                  pipelineLayout, descriptorSets[currentFrame],
                  shaderBallIndices);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &graphicsCmdBuffers[currentFrame];

    BB_VK_ASSERT(vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                               renderFinishedFences[currentFrame]));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain.Handle;
    presentInfo.pImageIndices = &currentFrame;

    VkResult queuePresentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        queuePresentResult == VK_SUBOPTIMAL_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physicalDevice, surface, &swapChainSupportDetails.Capabilities);

      onWindowResize(window, device, physicalDevice, surface,
                     swapChainSupportDetails, queueFamilyIndices,
                     testVertShader, testFragShader, pipelineLayout, swapChain,
                     renderPass, graphicsPipeline, swapChainFramebuffers);
    }

    currentFrame = (currentFrame + 1) % swapChain.NumColorImages;
  }

  vkDeviceWaitIdle(device);
  for (VkFence fence : renderFinishedFences) {
    vkDestroyFence(device, fence, nullptr);
  }
  vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
  vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

  vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  for (Buffer &uniformBuffer : uniformBuffers) {
    destroyBuffer(device, uniformBuffer);
  }

  vkDestroySampler(device, textureSampler, nullptr);
  vkDestroyImageView(device, textureImageView, nullptr);
  vkDestroyImage(device, textureImage, nullptr);
  vkFreeMemory(device, textureImageMemory, nullptr);
  destroyBuffer(device, shaderBallIndexBuffer);
  destroyBuffer(device, shaderBallVertexBuffer);
  destroyBuffer(device, quadIndexBuffer);
  destroyBuffer(device, quadVertexBuffer);

  vkDestroyCommandPool(device, transferCmdPool, nullptr);
  for (VkCommandPool cmdPool : graphicsCmdPools) {
    vkDestroyCommandPool(device, cmdPool, nullptr);
  }

  cleanupReloadableResources(device, swapChain, renderPass, graphicsPipeline,
                             swapChainFramebuffers);

  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  vkDestroyShaderModule(device, testVertShader, nullptr);
  vkDestroyShaderModule(device, testFragShader, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  if (messenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    messenger = VK_NULL_HANDLE;
  }
  vkDestroyInstance(instance, nullptr);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}