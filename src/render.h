#pragma once
#include "vector_math.h"
#include "external/volk.h"
#include <array>

namespace bb {

VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT _severity,
    VkDebugUtilsMessageTypeFlagsEXT _type,
    const VkDebugUtilsMessengerCallbackDataEXT *_callbackData, void *_userData);

bool getQueueFamily(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface,
                    uint32_t *_outQueueFamilyIndex);

uint32_t findMemoryType(VkPhysicalDevice _physicalDevice, uint32_t _typeFilter,
                        VkMemoryPropertyFlags _properties);

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR Capabilities;
  std::vector<VkSurfaceFormatKHR> Formats;
  std::vector<VkPresentModeKHR> PresentModes;

  VkSurfaceFormatKHR chooseSurfaceFormat() const;
  VkPresentModeKHR choosePresentMode() const;
  VkExtent2D chooseExtent(uint32_t _width, uint32_t _height) const;
};

bool checkPhysicalDevice(VkPhysicalDevice _physicalDevice,
                         VkSurfaceKHR _surface,
                         const std::vector<const char *> &_deviceExtensions,
                         VkPhysicalDeviceFeatures *_outDeviceFeatures,
                         uint32_t *_outQueueFamilyIndex,
                         SwapChainSupportDetails *_outSwapChainSupportDetails);

struct SwapChain {
  VkSwapchainKHR Handle;
  VkFormat ColorFormat;
  VkFormat DepthFormat;
  VkExtent2D Extent;
  uint32_t MinNumImages;
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
    uint32_t _height, const SwapChain *_oldSwapChain = nullptr);
void destroySwapChain(VkDevice _device, SwapChain &_swapChain);

struct Vertex {
  Float3 Pos;
  Float2 UV;
  Float3 Normal = {0, 0, -1};

  static VkVertexInputBindingDescription getBindingDesc();
  static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescs();
};

struct Buffer {
  VkBuffer Handle;
  VkDeviceMemory Memory;
  uint32_t Size;
};

Buffer createBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                    VkDeviceSize _size, VkBufferUsageFlags _usage,
                    VkMemoryPropertyFlags _properties);
Buffer createStagingBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                           const Buffer &_orgBuffer);

void destroyBuffer(VkDevice _device, Buffer &_buffer);
void copyBuffer(VkDevice _device, VkCommandPool _cmdPool, VkQueue _queue,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size);

struct Shader {
  VkShaderModule Vert;
  VkShaderModule Frag;
};

VkShaderModule createShaderModuleFromFile(VkDevice _device,
                                          const std::string &_filePath);

Shader createShaderFromFile(VkDevice _device,
                            const std::string &_vertShaderFilePath,
                            const std::string &_fragShaderFilePath);

void destroyShader(VkDevice _device, Shader &_shader);

} // namespace bb