#include "render.h"

namespace bb {
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

bool getQueueFamily(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
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

SwapChain createSwapChain(
    VkDevice _device, VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
    const SwapChainSupportDetails &_swapChainSupportDetails, uint32_t _width,
    uint32_t _height, const SwapChain *_oldSwapChain) {
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
  swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
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

VkVertexInputBindingDescription Vertex::getBindingDesc() {
  VkVertexInputBindingDescription bindingDesc = {};
  bindingDesc.binding = 0;
  bindingDesc.stride = sizeof(Vertex);
  bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return bindingDesc;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescs() {
  std::array<VkVertexInputAttributeDescription, 3> attributeDescs = {};
  attributeDescs[0].binding = 0;
  attributeDescs[0].location = 0;
  attributeDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescs[0].offset = offsetof(Vertex, Pos);
  attributeDescs[1].binding = 0;
  attributeDescs[1].location = 1;
  attributeDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescs[1].offset = offsetof(Vertex, UV);
  attributeDescs[2].binding = 0;
  attributeDescs[2].location = 2;
  attributeDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescs[2].offset = offsetof(Vertex, Normal);

  return attributeDescs;
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

Shader createShaderFromFile(VkDevice _device,
                            const std::string &_vertShaderFilePath,
                            const std::string &_fragShaderFilePath) {
  Shader result = {};
  result.Vert = createShaderModuleFromFile(_device, _vertShaderFilePath);
  result.Frag = createShaderModuleFromFile(_device, _fragShaderFilePath);

  return result;
}

void destroyShader(VkDevice _device, Shader &_shader) {
  vkDestroyShaderModule(_device, _shader.Vert, nullptr);
  vkDestroyShaderModule(_device, _shader.Frag, nullptr);
  _shader = {};
}

} // namespace bb