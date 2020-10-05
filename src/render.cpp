#include "render.h"
#include "resource.h"
#include "type_conversion.h"
#include "external/SDL2/SDL_vulkan.h"
#include "external/stb_image.h"

namespace bb {

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT _severity,
    VkDebugUtilsMessageTypeFlagsEXT _type,
    const VkDebugUtilsMessengerCallbackDataEXT *_callbackData, void *_userData);
static bool
checkPhysicalDevice(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
                    const std::vector<const char *> &_deviceExtensions,
                    VkPhysicalDeviceFeatures *_outDeviceFeatures,
                    uint32_t *_outQueueFamilyIndex,
                    SwapChainSupportDetails *_outSwapChainSupportDetails);

Renderer createRenderer(SDL_Window *_window) {
  Renderer result;

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
  SDL_Vulkan_GetInstanceExtensions(_window, &numInstantExtensions, nullptr);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  unsigned numExtraInstantExtensions = extensions.size();
  extensions.resize(numExtraInstantExtensions + numInstantExtensions);

  SDL_Vulkan_GetInstanceExtensions(_window, &numInstantExtensions,
                                   extensions.data() +
                                       numExtraInstantExtensions);

  instanceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
  instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

  BB_VK_ASSERT(
      vkCreateInstance(&instanceCreateInfo, nullptr, &result.Instance));
  volkLoadInstance(result.Instance);

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
    BB_VK_ASSERT(vkCreateDebugUtilsMessengerEXT(result.Instance,
                                                &messengerCreateInfo, nullptr,
                                                &result.DebugMessenger));
  }

  BB_VK_ASSERT(!SDL_Vulkan_CreateSurface(
      _window, result.Instance,
      &result.Surface)); // ! to convert SDL_bool to VkResult

  uint32_t numPhysicalDevices = 0;
  std::vector<VkPhysicalDevice> physicalDevices;
  BB_VK_ASSERT(vkEnumeratePhysicalDevices(result.Instance, &numPhysicalDevices,
                                          nullptr));
  physicalDevices.resize(numPhysicalDevices);
  BB_VK_ASSERT(vkEnumeratePhysicalDevices(result.Instance, &numPhysicalDevices,
                                          physicalDevices.data()));

  VkPhysicalDeviceFeatures deviceFeatures = {};

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  for (VkPhysicalDevice currentPhysicalDevice : physicalDevices) {
    if (checkPhysicalDevice(currentPhysicalDevice, result.Surface,
                            deviceExtensions, &deviceFeatures,
                            &result.QueueFamilyIndex,
                            &result.SwapChainSupportDetails)) {
      result.PhysicalDevice = currentPhysicalDevice;
      break;
    }
  }
  BB_ASSERT(result.PhysicalDevice != VK_NULL_HANDLE);

  std::unordered_map<uint32_t, VkQueue> queueMap;
  float queuePriority = 1.f;
  VkDeviceQueueCreateInfo queueCreateInfo = {};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = result.QueueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkDeviceCreateInfo deviceCreateInfo = {};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
  deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
  deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

  BB_VK_ASSERT(vkCreateDevice(result.PhysicalDevice, &deviceCreateInfo, nullptr,
                              &result.Device));

  vkGetDeviceQueue(result.Device, result.QueueFamilyIndex, 0, &result.Queue);

  return result;
}

void destroyRenderer(Renderer &_renderer) {
  vkDestroyDevice(_renderer.Device, nullptr);
  vkDestroySurfaceKHR(_renderer.Instance, _renderer.Surface, nullptr);
  if (_renderer.DebugMessenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(_renderer.Instance,
                                    _renderer.DebugMessenger, nullptr);
  }
  vkDestroyInstance(_renderer.Instance, nullptr);
  _renderer = {};
}

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

static bool getQueueFamily(VkPhysicalDevice _physicalDevice,
                           VkSurfaceKHR _surface,
                           uint32_t *_outQueueFamilyIndex) {

  uint32_t numQueueFamilyProperties = 0;
  std::vector<VkQueueFamilyProperties> queueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice,
                                           &numQueueFamilyProperties, nullptr);
  queueFamilyProperties.resize(numQueueFamilyProperties);
  vkGetPhysicalDeviceQueueFamilyProperties(
      _physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  for (uint32_t i = 0; i < numQueueFamilyProperties; i++) {
    VkQueueFlags flags = queueFamilyProperties[i].queueFlags;
    if (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT |
                 VK_QUEUE_COMPUTE_BIT)) {
      VkBool32 supportPresent = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, i, _surface,
                                           &supportPresent);
      if (supportPresent) {
        *_outQueueFamilyIndex = i;
        return true;
      }
    }
  }

  return false;
}

uint32_t findMemoryType(const Renderer &_renderer, uint32_t _typeFilter,
                        VkMemoryPropertyFlags _properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(_renderer.PhysicalDevice, &memProperties);

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

VkSurfaceFormatKHR SwapChainSupportDetails::chooseSurfaceFormat() const {
  for (const VkSurfaceFormatKHR &format : Formats) {
    if ((format.format == VK_FORMAT_R8G8B8A8_SRGB ||
         format.format == VK_FORMAT_B8G8R8A8_SRGB) &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  return Formats[0];
}

VkPresentModeKHR SwapChainSupportDetails::choosePresentMode() const {

  // TODO(JJJ): Mailbox mod have some issue.
  // for (VkPresentModeKHR presentMode : PresentModes) {
  //   if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
  //     return presentMode;
  //   }
  // }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChainSupportDetails::chooseExtent(uint32_t _width,
                                                 uint32_t _height) const {
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

bool checkPhysicalDevice(VkPhysicalDevice _physicalDevice,
                         VkSurfaceKHR _surface,
                         const std::vector<const char *> &_deviceExtensions,
                         VkPhysicalDeviceFeatures *_outDeviceFeatures,
                         uint32_t *_outQueueFamilyIndex,
                         SwapChainSupportDetails *_outSwapChainSupportDetails) {
  VkPhysicalDeviceProperties deviceProperties = {};
  VkPhysicalDeviceFeatures deviceFeatures = {};

  vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
  vkGetPhysicalDeviceFeatures(_physicalDevice, &deviceFeatures);

  bool supportFullFeaturedQueueFamilyIndex =
      getQueueFamily(_physicalDevice, _surface, _outQueueFamilyIndex);

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
  bool isQueueComplete = supportFullFeaturedQueueFamilyIndex;

  if (_outDeviceFeatures) {
    *_outDeviceFeatures = deviceFeatures;
  }

  return areAllExtensionsSupported && isSwapChainAdequate && isProperType &&
         isFeatureComplete && isQueueComplete;
}

SwapChain createSwapChain(const Renderer &_renderer, uint32_t _width,
                          uint32_t _height, const SwapChain *_oldSwapChain) {
  VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
  swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapChainCreateInfo.surface = _renderer.Surface;
  swapChainCreateInfo.minImageCount =
      _renderer.SwapChainSupportDetails.Capabilities.minImageCount + 1;
  if (_renderer.SwapChainSupportDetails.Capabilities.maxImageCount > 0 &&
      swapChainCreateInfo.minImageCount >
          _renderer.SwapChainSupportDetails.Capabilities.maxImageCount) {
    swapChainCreateInfo.minImageCount =
        _renderer.SwapChainSupportDetails.Capabilities.maxImageCount;
  }
  VkSurfaceFormatKHR surfaceFormat =
      _renderer.SwapChainSupportDetails.chooseSurfaceFormat();
  swapChainCreateInfo.imageFormat = surfaceFormat.format;
  swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
  swapChainCreateInfo.imageExtent =
      _renderer.SwapChainSupportDetails.chooseExtent(_width, _height);
  swapChainCreateInfo.imageArrayLayers = 1;
  swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapChainCreateInfo.preTransform =
      _renderer.SwapChainSupportDetails.Capabilities.currentTransform;
  swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapChainCreateInfo.presentMode =
      _renderer.SwapChainSupportDetails.choosePresentMode();
  swapChainCreateInfo.clipped = VK_TRUE;
  if (_oldSwapChain) {
    swapChainCreateInfo.oldSwapchain = _oldSwapChain->Handle;
  } else {
    swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
  }

  SwapChain swapChain;
  swapChain.ColorFormat = swapChainCreateInfo.imageFormat;
  swapChain.Extent = swapChainCreateInfo.imageExtent;
  BB_VK_ASSERT(vkCreateSwapchainKHR(_renderer.Device, &swapChainCreateInfo,
                                    nullptr, &swapChain.Handle));

  vkGetSwapchainImagesKHR(_renderer.Device, swapChain.Handle,
                          &swapChain.NumColorImages, nullptr);
  swapChain.ColorImages.resize(swapChain.NumColorImages);
  vkGetSwapchainImagesKHR(_renderer.Device, swapChain.Handle,
                          &swapChain.NumColorImages,
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
    BB_VK_ASSERT(vkCreateImageView(_renderer.Device,
                                   &swapChainImageViewCreateInfo, nullptr,
                                   &swapChain.ColorImageViews[i]));
  }
  swapChain.MinNumImages = swapChainCreateInfo.minImageCount;

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
  BB_VK_ASSERT(vkCreateImage(_renderer.Device, &depthImageCreateInfo, nullptr,
                             &swapChain.DepthImage));

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(_renderer.Device, swapChain.DepthImage,
                               &memRequirements);

  VkMemoryAllocateInfo depthImageMemoryAllocInfo = {};
  depthImageMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  depthImageMemoryAllocInfo.allocationSize = memRequirements.size;
  depthImageMemoryAllocInfo.memoryTypeIndex =
      findMemoryType(_renderer, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  BB_VK_ASSERT(vkAllocateMemory(_renderer.Device, &depthImageMemoryAllocInfo,
                                nullptr, &swapChain.DepthImageMemory));

  BB_VK_ASSERT(vkBindImageMemory(_renderer.Device, swapChain.DepthImage,
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
  BB_VK_ASSERT(vkCreateImageView(_renderer.Device, &depthImageViewCreateInfo,
                                 nullptr, &swapChain.DepthImageView));

  return swapChain;
}

void destroySwapChain(const Renderer &_renderer, SwapChain &_swapChain) {
  for (VkImageView imageView : _swapChain.ColorImageViews) {
    vkDestroyImageView(_renderer.Device, imageView, nullptr);
  }
  vkDestroyImageView(_renderer.Device, _swapChain.DepthImageView, nullptr);
  vkDestroyImage(_renderer.Device, _swapChain.DepthImage, nullptr);
  vkFreeMemory(_renderer.Device, _swapChain.DepthImageMemory, nullptr);
  vkDestroySwapchainKHR(_renderer.Device, _swapChain.Handle, nullptr);
  _swapChain = {};
}

std::array<VkVertexInputBindingDescription, 2> Vertex::getBindingDescs() {
  std::array<VkVertexInputBindingDescription, 2> bindingDescs = {};
  // Vertex
  bindingDescs[0].binding = 0;
  bindingDescs[0].stride = sizeof(Vertex);
  bindingDescs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  // instance
  bindingDescs[1].binding = 1;
  bindingDescs[1].stride = sizeof(InstanceBlock);
  bindingDescs[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

  return bindingDescs;
}

std::array<VkVertexInputAttributeDescription, 16> Vertex::getAttributeDescs() {
  std::array<VkVertexInputAttributeDescription, 16> attributeDescs = {};

  int lastAttributeIndex = 0;

  auto pushIntAttribute = [&](uint32_t _binding, int _numComponents,
                              uint32_t _offset) {
    BB_ASSERT(_numComponents >= 1 && _numComponents <= 4);
    VkFormat format;
    switch (_numComponents) {
    case 1:
      format = VK_FORMAT_R32_SINT;
      break;
    case 2:
      format = VK_FORMAT_R32G32_SINT;
      break;
    case 3:
      format = VK_FORMAT_R32G32B32_SINT;
      break;
    case 4:
      format = VK_FORMAT_R32G32B32A32_SINT;
      break;
    }

    VkVertexInputAttributeDescription &attribute =
        attributeDescs[lastAttributeIndex];
    attribute.binding = _binding;
    attribute.location = lastAttributeIndex;
    attribute.format = format;
    attribute.offset = _offset;

    ++lastAttributeIndex;
  };

  auto pushVecAttribute = [&](uint32_t _binding, int _numComponents,
                              uint32_t _offset) {
    BB_ASSERT(_numComponents >= 1 && _numComponents <= 4);
    VkFormat format;
    switch (_numComponents) {
    case 1:
      format = VK_FORMAT_R32_SFLOAT;
      break;
    case 2:
      format = VK_FORMAT_R32G32_SFLOAT;
      break;
    case 3:
      format = VK_FORMAT_R32G32B32_SFLOAT;
      break;
    case 4:
      format = VK_FORMAT_R32G32B32A32_SFLOAT;
      break;
    }

    VkVertexInputAttributeDescription &attribute =
        attributeDescs[lastAttributeIndex];
    attribute.binding = _binding;
    attribute.location = lastAttributeIndex;
    attribute.format = format;
    attribute.offset = _offset;

    ++lastAttributeIndex;
  };

  pushVecAttribute(0, 3, offsetof(Vertex, Pos));
  pushVecAttribute(0, 2, offsetof(Vertex, UV));
  pushVecAttribute(0, 3, offsetof(Vertex, Normal));
  pushVecAttribute(0, 3, offsetof(Vertex, Tangent));

  auto pushMat4Attribute = [&](uint32_t _binding, uint32_t _offset) {
    for (int i = 0; i < 4; ++i) {
      VkVertexInputAttributeDescription &attribute =
          attributeDescs[lastAttributeIndex + i];
      attribute.binding = _binding;
      attribute.location = lastAttributeIndex + i;
      attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      attribute.offset = _offset + (uint32_t)(sizeof(float) * 4 * i);
    }
    lastAttributeIndex += 4;
  };

  pushMat4Attribute(1, offsetof(InstanceBlock, ModelMat));
  pushMat4Attribute(1, offsetof(InstanceBlock, InvModelMat));
  pushVecAttribute(1, 3, offsetof(InstanceBlock, Albedo));
  pushVecAttribute(1, 1, offsetof(InstanceBlock, Metallic));
  pushVecAttribute(1, 1, offsetof(InstanceBlock, Roughness));
  pushVecAttribute(1, 1, offsetof(InstanceBlock, AO));

  BB_ASSERT(lastAttributeIndex == attributeDescs.size());

  return attributeDescs;
}

Buffer createBuffer(const Renderer &_renderer, VkDeviceSize _size,
                    VkBufferUsageFlags _usage,
                    VkMemoryPropertyFlags _properties) {
  Buffer result = {};

  VkBufferCreateInfo bufferCreateInfo = {};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = _size;
  bufferCreateInfo.usage = _usage;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  BB_VK_ASSERT(vkCreateBuffer(_renderer.Device, &bufferCreateInfo, nullptr,
                              &result.Handle));

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(_renderer.Device, result.Handle,
                                &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(_renderer, memRequirements.memoryTypeBits, _properties);

  BB_VK_ASSERT(
      vkAllocateMemory(_renderer.Device, &allocInfo, nullptr, &result.Memory));
  BB_VK_ASSERT(
      vkBindBufferMemory(_renderer.Device, result.Handle, result.Memory, 0));

  result.Size = _size;

  return result;
};

Buffer createStagingBuffer(const Renderer &_renderer,
                           const Buffer &_orgBuffer) {
  Buffer result =
      createBuffer(_renderer, _orgBuffer.Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  return result;
}

void destroyBuffer(const Renderer &_renderer, Buffer &_buffer) {
  vkDestroyBuffer(_renderer.Device, _buffer.Handle, nullptr);
  _buffer.Handle = VK_NULL_HANDLE;
  vkFreeMemory(_renderer.Device, _buffer.Memory, nullptr);
  _buffer.Memory = VK_NULL_HANDLE;
}

void copyBuffer(const Renderer &_renderer, VkCommandPool _cmdPool,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size) {
  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = _cmdPool;
  cmdBufferAllocInfo.commandBufferCount = 1;
  VkCommandBuffer cmdBuffer;
  BB_VK_ASSERT(vkAllocateCommandBuffers(_renderer.Device, &cmdBufferAllocInfo,
                                        &cmdBuffer));

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
  vkQueueSubmit(_renderer.Queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(_renderer.Queue);

  vkFreeCommandBuffers(_renderer.Device, cmdBufferAllocInfo.commandPool, 1,
                       &cmdBuffer);
}

Image createImageFromFile(const Renderer &_renderer,
                          VkCommandPool _transientCmdPool,
                          const std::string &_filePath) {
  Image result = {};

  Int2 textureDims = {};
  int numChannels;
  stbi_uc *pixels = stbi_load(_filePath.c_str(), &textureDims.X, &textureDims.Y,
                              &numChannels, STBI_rgb_alpha);
  if (!pixels)
    return {};

  VkDeviceSize textureSize = textureDims.X * textureDims.Y * 4;

  Buffer textureStagingBuffer =
      createBuffer(_renderer, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  void *data;
  vkMapMemory(_renderer.Device, textureStagingBuffer.Memory, 0, textureSize, 0,
              &data);
  memcpy(data, pixels, textureSize);
  vkUnmapMemory(_renderer.Device, textureStagingBuffer.Memory);

  stbi_image_free(pixels);

  VkImageCreateInfo imageCreateInfo = {};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.extent.width = (uint32_t)textureDims.X;
  imageCreateInfo.extent.height = (uint32_t)textureDims.Y;
  imageCreateInfo.extent.depth = 1;
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.flags = 0;

  BB_VK_ASSERT(vkCreateImage(_renderer.Device, &imageCreateInfo, nullptr,
                             &result.Handle));

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(_renderer.Device, result.Handle,
                               &memRequirements);

  VkMemoryAllocateInfo textureImageMemoryAllocateInfo = {};
  textureImageMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  textureImageMemoryAllocateInfo.allocationSize = memRequirements.size;
  textureImageMemoryAllocateInfo.memoryTypeIndex =
      findMemoryType(_renderer, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  BB_VK_ASSERT(vkAllocateMemory(_renderer.Device,
                                &textureImageMemoryAllocateInfo, nullptr,
                                &result.Memory));

  BB_VK_ASSERT(
      vkBindImageMemory(_renderer.Device, result.Handle, result.Memory, 0));

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = _transientCmdPool;
  cmdBufferAllocInfo.commandBufferCount = 1;

  VkCommandBuffer cmdBuffer;
  BB_VK_ASSERT(vkAllocateCommandBuffers(_renderer.Device, &cmdBufferAllocInfo,
                                        &cmdBuffer));

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
  barrier.image = result.Handle;
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
  region.imageExtent = int2ToExtent3D(textureDims);
  vkCmdCopyBufferToImage(cmdBuffer, textureStagingBuffer.Handle, result.Handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmdBuffer;
  BB_VK_ASSERT(vkQueueSubmit(_renderer.Queue, 1, &submitInfo, VK_NULL_HANDLE));
  BB_VK_ASSERT(vkQueueWaitIdle(_renderer.Queue));
  vkFreeCommandBuffers(_renderer.Device, _transientCmdPool, 1, &cmdBuffer);

  destroyBuffer(_renderer, textureStagingBuffer);

  VkImageViewCreateInfo imageViewCreateInfo = {};
  imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCreateInfo.image = result.Handle;
  imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
  imageViewCreateInfo.subresourceRange.levelCount = 1;
  imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
  imageViewCreateInfo.subresourceRange.layerCount = 1;
  BB_VK_ASSERT(vkCreateImageView(_renderer.Device, &imageViewCreateInfo,
                                 nullptr, &result.View));

  return result;
}

void destroyImage(const Renderer &_renderer, Image &_image) {
  vkDestroyImageView(_renderer.Device, _image.View, nullptr);
  vkDestroyImage(_renderer.Device, _image.Handle, nullptr);
  vkFreeMemory(_renderer.Device, _image.Memory, nullptr);
  _image = {};
}

VkPipelineShaderStageCreateInfo Shader::getStageInfo() const {
  VkPipelineShaderStageCreateInfo stageInfo = {};
  stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stageInfo.stage = Stage;
  stageInfo.module = Handle;
  stageInfo.pName = "main";
  return stageInfo;
}

Shader createShaderFromFile(const Renderer &_renderer,
                            const std::string &_filePath) {
  Shader result;

  if (endsWith(_filePath, ".vert.spv")) {
    result.Stage = VK_SHADER_STAGE_VERTEX_BIT;
  } else if (endsWith(_filePath, ".frag.spv")) {
    result.Stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  } else if (endsWith(_filePath, ".geom.spv")) {
    result.Stage = VK_SHADER_STAGE_GEOMETRY_BIT;
  } else {
    BB_ASSERT(false);
  }

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

  BB_VK_ASSERT(vkCreateShaderModule(_renderer.Device, &createInfo, nullptr,
                                    &result.Handle));

  delete[] contents;
  fclose(f);

  return result;
}

void destroyShader(const Renderer &_renderer, Shader &_shader) {
  vkDestroyShaderModule(_renderer.Device, _shader.Handle, nullptr);
  _shader = {};
}

VkPipeline createPipeline(const Renderer &_renderer,
                          const PipelineParams &_params) {
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
  shaderStages.reserve(_params.NumShaders);
  for (int i = 0; i < _params.NumShaders; ++i) {
    shaderStages.push_back(_params.Shaders[i]->getStageInfo());
  }

  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount =
      (uint32_t)_params.VertexInput.NumBindings;
  vertexInputState.pVertexBindingDescriptions = _params.VertexInput.Bindings;
  vertexInputState.vertexAttributeDescriptionCount =
      (uint32_t)_params.VertexInput.NumAttributes;
  vertexInputState.pVertexAttributeDescriptions =
      _params.VertexInput.Attributes;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
  inputAssemblyState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = _params.InputAssembly.Topology;
  inputAssemblyState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = _params.Viewport.Offset.X;
  viewport.y = _params.Viewport.Offset.Y;
  viewport.width = _params.Viewport.Extent.X;
  viewport.height = _params.Viewport.Extent.Y;
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;

  VkRect2D scissor = {};
  scissor.offset.x = _params.Viewport.ScissorOffset.X;
  scissor.offset.y = _params.Viewport.ScissorOffset.Y;
  scissor.extent.width = _params.Viewport.ScissorExtent.X;
  scissor.extent.height = _params.Viewport.ScissorExtent.Y;

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
  rasterizationState.polygonMode = _params.Rasterizer.PolygonMode;
  rasterizationState.lineWidth = 1.f;
  rasterizationState.cullMode = _params.Rasterizer.CullMode;
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
  depthStencilState.depthTestEnable =
      _params.DepthStencil.DepthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencilState.depthWriteEnable =
      _params.DepthStencil.DepthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  depthStencilState.depthBoundsTestEnable = VK_FALSE;
  depthStencilState.minDepthBounds = 1.f;
  depthStencilState.maxDepthBounds = 0.f;
  depthStencilState.stencilTestEnable = VK_FALSE;

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

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
  pipelineCreateInfo.pStages = shaderStages.data();
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;
  pipelineCreateInfo.pDynamicState = nullptr;
  pipelineCreateInfo.layout = _params.PipelineLayout;
  pipelineCreateInfo.renderPass = _params.RenderPass;
  pipelineCreateInfo.subpass = 0;
  pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCreateInfo.basePipelineIndex = -1;

  VkPipeline pipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_renderer.Device, VK_NULL_HANDLE, 1,
                                         &pipelineCreateInfo, nullptr,
                                         &pipeline));

  return pipeline;
}

PBRMaterial createPBRMaterialFromFiles(const Renderer &_renderer,
                                       VkCommandPool _transientCmdPool,
                                       const std::string &_rootPath) {
  // TODO(ilgwon): Convert _rootPath to absolute path if it's not already.
  PBRMaterial result = {};
  result.Maps[PBRMapType::Albedo] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "albedo.png"));
  result.Maps[PBRMapType::Metallic] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "metallic.png"));
  result.Maps[PBRMapType::Roughness] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "roughness.png"));
  result.Maps[PBRMapType::AO] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "ao.png"));
  result.Maps[PBRMapType::Normal] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "normal.png"));
  result.Maps[PBRMapType::Height] = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "height.png"));
  return result;
}

void destroyPBRMaterial(const Renderer &_renderer, PBRMaterial &_material) {
  for (Image &image : _material.Maps) {
    destroyImage(_renderer, image);
  }
  _material = {};
}

StandardPipelineLayout createStandardPipelineLayout(const Renderer &_renderer) {
  StandardPipelineLayout layout = {};

  VkSamplerCreateInfo samplerCreateInfo = {};
  samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
  samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
  samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.anisotropyEnable = VK_TRUE;
  samplerCreateInfo.maxAnisotropy = 16.f;
  samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
  samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
  samplerCreateInfo.compareEnable = VK_FALSE;
  samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerCreateInfo.mipLodBias = 0.f;
  samplerCreateInfo.minLod = 0.f;
  samplerCreateInfo.maxLod = 0.f;

  BB_VK_ASSERT(
      vkCreateSampler(_renderer.Device, &samplerCreateInfo, nullptr,
                      &layout.ImmutableSamplers[SamplerType::Nearest]));

  samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  BB_VK_ASSERT(vkCreateSampler(_renderer.Device, &samplerCreateInfo, nullptr,
                               &layout.ImmutableSamplers[SamplerType::Linear]));

  VkDescriptorSetLayoutBinding bindings[16] = {};
  for (size_t i = 0; i < std::size(bindings); ++i) {
    bindings[i].binding = (uint32_t)i;
    bindings[i].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.pBindings = bindings;

  // Create descriptor set layouts with the predefined metadata
  {
    EnumArray<DescriptorFrequency,
              std::unordered_map<VkDescriptorType, uint32_t>>
        meta = {{
            // PerFrame
            {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                {VK_DESCRIPTOR_TYPE_SAMPLER,
                 (uint32_t)layout.ImmutableSamplers.size()},
            },
            // PerView
            {

                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            },
            // PerMaterial
            {
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                 (uint32_t)PBRMaterial::NumImages},
            },
            // PerDraw
            {},
        }};

    for (DescriptorFrequency frequency = DescriptorFrequency::PerFrame;
         frequency < DescriptorFrequency::COUNT;
         frequency = (DescriptorFrequency)((int)frequency + 1)) {
      uint32_t numBindings = 0;
      for (auto [descriptorType, numDescriptors] : meta[frequency]) {
        VkDescriptorSetLayoutBinding &binding = bindings[numBindings++];
        binding.descriptorType = descriptorType;
        binding.descriptorCount = numDescriptors;
        if ((frequency == DescriptorFrequency::PerFrame) &&
            (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)) {
          binding.pImmutableSamplers = layout.ImmutableSamplers.data();
        }
      }

      descriptorSetLayoutCreateInfo.bindingCount = numBindings;

      BB_VK_ASSERT(vkCreateDescriptorSetLayout(
          _renderer.Device, &descriptorSetLayoutCreateInfo, nullptr,
          &layout.DescriptorSetLayouts[frequency].Handle));
      layout.DescriptorSetLayouts[frequency].NumDescriptorsTable =
          std::move(meta[frequency]);
    }
  }

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
  descriptorSetLayouts.reserve(layout.DescriptorSetLayouts.size());
  for (const DescriptorSetLayout &descriptorSetLayout :
       layout.DescriptorSetLayouts) {
    descriptorSetLayouts.push_back(descriptorSetLayout.Handle);
  }
  pipelineLayoutCreateInfo.setLayoutCount =
      (uint32_t)descriptorSetLayouts.size();
  pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
  vkCreatePipelineLayout(_renderer.Device, &pipelineLayoutCreateInfo, nullptr,
                         &layout.Handle);

  return layout;
}

void destroyStandardPipelineLayout(const Renderer &_renderer,
                                   StandardPipelineLayout &_layout) {
  vkDestroyPipelineLayout(_renderer.Device, _layout.Handle, nullptr);
  for (const DescriptorSetLayout &descriptorSetLayout :
       _layout.DescriptorSetLayouts) {
    vkDestroyDescriptorSetLayout(_renderer.Device, descriptorSetLayout.Handle,
                                 nullptr);
  }
  for (VkSampler sampler : _layout.ImmutableSamplers) {
    vkDestroySampler(_renderer.Device, sampler, nullptr);
  }
  _layout = {};
}

Frame createFrame(const Renderer &_renderer,
                  const StandardPipelineLayout &_standardPipelineLayout,
                  VkDescriptorPool _descriptorPool,
                  const std::vector<PBRMaterial> &_pbrMaterials) {
  Frame frame = {};

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = _renderer.QueueFamilyIndex;
  cmdPoolCreateInfo.flags = 0;

  BB_VK_ASSERT(vkCreateCommandPool(_renderer.Device, &cmdPoolCreateInfo,
                                   nullptr, &frame.CmdPool));

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = 1;
  cmdBufferAllocInfo.commandPool = frame.CmdPool;
  BB_VK_ASSERT(vkAllocateCommandBuffers(_renderer.Device, &cmdBufferAllocInfo,
                                        &frame.CmdBuffer));

  // Allocate descriptor sets
  {
    VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
    descriptorSetAllocInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocInfo.descriptorPool = _descriptorPool;

    descriptorSetAllocInfo.descriptorSetCount = 1;
    descriptorSetAllocInfo.pSetLayouts =
        &_standardPipelineLayout
             .DescriptorSetLayouts[DescriptorFrequency::PerFrame]
             .Handle;
    BB_VK_ASSERT(vkAllocateDescriptorSets(
        _renderer.Device, &descriptorSetAllocInfo, &frame.FrameDescriptorSet));

    descriptorSetAllocInfo.descriptorSetCount = 1;
    descriptorSetAllocInfo.pSetLayouts =
        &_standardPipelineLayout
             .DescriptorSetLayouts[DescriptorFrequency::PerView]
             .Handle;
    BB_VK_ASSERT(vkAllocateDescriptorSets(
        _renderer.Device, &descriptorSetAllocInfo, &frame.ViewDescriptorSet));

    frame.MaterialDescriptorSets.resize(_pbrMaterials.size());
    descriptorSetAllocInfo.descriptorSetCount = _pbrMaterials.size();
    descriptorSetAllocInfo.pSetLayouts =
        &_standardPipelineLayout
             .DescriptorSetLayouts[DescriptorFrequency::PerMaterial]
             .Handle;
    BB_VK_ASSERT(vkAllocateDescriptorSets(_renderer.Device,
                                          &descriptorSetAllocInfo,
                                          frame.MaterialDescriptorSets.data()));
  }

  frame.FrameUniformBuffer = createBuffer(
      _renderer, sizeof(FrameUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  frame.ViewUniformBuffer = createBuffer(
      _renderer, sizeof(ViewUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Link descriptor sets to actual resources
  {

    std::vector<VkWriteDescriptorSet> writeInfos;
    VkWriteDescriptorSet writeInfo = {};
    writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    // FrameData
    writeInfo.dstSet = frame.FrameDescriptorSet;
    writeInfo.dstBinding = 0;
    writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeInfo.descriptorCount = 1;
    VkDescriptorBufferInfo frameUniformBufferInfo = {};
    frameUniformBufferInfo.buffer = frame.FrameUniformBuffer.Handle;
    frameUniformBufferInfo.offset = 0;
    frameUniformBufferInfo.range = frame.FrameUniformBuffer.Size;
    writeInfo.pBufferInfo = &frameUniformBufferInfo;
    writeInfos.push_back(writeInfo);

    // ViewData
    writeInfo.dstSet = frame.ViewDescriptorSet;
    writeInfo.dstBinding = 0;
    writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeInfo.descriptorCount = 1;
    VkDescriptorBufferInfo viewUniformBufferInfo = {};
    viewUniformBufferInfo.buffer = frame.ViewUniformBuffer.Handle;
    viewUniformBufferInfo.offset = 0;
    viewUniformBufferInfo.range = frame.ViewUniformBuffer.Size;
    writeInfo.pBufferInfo = &viewUniformBufferInfo;
    writeInfos.push_back(writeInfo);

    // uMaterialTextures
    std::vector<std::array<VkDescriptorImageInfo, PBRMaterial::NumImages>>
        materialImagesInfos;
    materialImagesInfos.reserve(_pbrMaterials.size());
    for (const PBRMaterial &material : _pbrMaterials) {
      std::array<VkDescriptorImageInfo, PBRMaterial::NumImages> imageInfos;

      int i = 0;
      for (const Image &image : material.Maps) {
        VkDescriptorImageInfo &imageInfo = imageInfos[i++];
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = image.View;
      }

      materialImagesInfos.push_back(imageInfos);
    }

    int materialIndex = 0;
    for (const auto &materialImagesInfo : materialImagesInfos) {
      writeInfo.dstSet = frame.MaterialDescriptorSets[materialIndex++];
      writeInfo.dstBinding = 0;
      writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      writeInfo.descriptorCount = PBRMaterial::NumImages;
      writeInfo.pImageInfo = materialImagesInfo.data();
      writeInfos.push_back(writeInfo);
    }

    vkUpdateDescriptorSets(_renderer.Device, writeInfos.size(),
                           writeInfos.data(), 0, nullptr);
  }

  VkSemaphoreCreateInfo semaphoreCreateInfo = {};
  semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  BB_VK_ASSERT(vkCreateSemaphore(_renderer.Device, &semaphoreCreateInfo,
                                 nullptr, &frame.RenderFinishedSemaphore));
  BB_VK_ASSERT(vkCreateSemaphore(_renderer.Device, &semaphoreCreateInfo,
                                 nullptr, &frame.ImagePresentedSemaphore));

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  BB_VK_ASSERT(vkCreateFence(_renderer.Device, &fenceCreateInfo, nullptr,
                             &frame.FrameAvailableFence));

  return frame;
}

void destroyFrame(const Renderer &_renderer, Frame &_frame) {
  vkDestroySemaphore(_renderer.Device, _frame.ImagePresentedSemaphore, nullptr);
  vkDestroySemaphore(_renderer.Device, _frame.RenderFinishedSemaphore, nullptr);
  vkDestroyFence(_renderer.Device, _frame.FrameAvailableFence, nullptr);
  destroyBuffer(_renderer, _frame.ViewUniformBuffer);
  destroyBuffer(_renderer, _frame.FrameUniformBuffer);
  vkDestroyCommandPool(_renderer.Device, _frame.CmdPool, nullptr);
  _frame = {};
}

} // namespace bb