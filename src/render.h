#pragma once
#include "vector_math.h"
#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include <array>

namespace bb {

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR Capabilities;
  std::vector<VkSurfaceFormatKHR> Formats;
  std::vector<VkPresentModeKHR> PresentModes;

  VkSurfaceFormatKHR chooseSurfaceFormat() const;
  VkPresentModeKHR choosePresentMode() const;
  VkExtent2D chooseExtent(uint32_t _width, uint32_t _height) const;
};

struct Renderer {
  VkInstance Instance;
  VkDebugUtilsMessengerEXT DebugMessenger;
  VkSurfaceKHR Surface;
  VkDevice Device;
  VkPhysicalDevice PhysicalDevice;
  SwapChainSupportDetails
      SwapChainSupportDetails; // TODO(ilgwon): I'm not sure if this field has
                               // to belong to Renderer, because it's value
                               // changes when a window is resized.
  uint32_t QueueFamilyIndex;
  VkQueue Queue;
};

Renderer createRenderer(SDL_Window *_window);
void destroyRenderer(Renderer &_renderer);
uint32_t findMemoryType(const Renderer &_renderer, uint32_t _typeFilter,
                        VkMemoryPropertyFlags _properties);

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

SwapChain createSwapChain(const Renderer &_renderer, uint32_t _width,
                          uint32_t _height,
                          const SwapChain *_oldSwapChain = nullptr);
void destroySwapChain(const Renderer &_renderer, SwapChain &_swapChain);

struct UniformBlock {
  Mat4 ViewMat;
  Mat4 ProjMat;
  alignas(16) Float3 ViewPos;
};

struct InstanceBlock {
  Mat4 ModelMat;
  Mat4 InvModelMat;
  alignas(16) Float3 Albedo = {1, 1, 1};
  float Metallic;
  float Roughness = 0.5f;
  float AO = 1;
  int MaterialIndex;
};

struct Vertex {
  Float3 Pos;
  Float2 UV;
  Float3 Normal = {0, 0, -1};

  static std::array<VkVertexInputBindingDescription, 2> getBindingDescs();
  static std::array<VkVertexInputAttributeDescription, 16> getAttributeDescs();
};

struct Buffer {
  VkBuffer Handle;
  VkDeviceMemory Memory;
  uint32_t Size;
};

Buffer createBuffer(const Renderer &_renderer, VkDeviceSize _size,
                    VkBufferUsageFlags _usage,
                    VkMemoryPropertyFlags _properties);
Buffer createStagingBuffer(const Renderer &_renderer, const Buffer &_orgBuffer);

void destroyBuffer(const Renderer &_renderer, Buffer &_buffer);
void copyBuffer(const Renderer &_renderer, VkCommandPool _cmdPool,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size);

struct Image {
  VkImage Handle;
  VkDeviceMemory Memory;
  VkImageView View;
};

Image createImageFromFile(const Renderer &_renderer,
                          VkCommandPool _transientCmdPool,
                          const std::string &_filePath);
void destroyImage(const Renderer &_renderer, Image &_image);

struct Shader {
  VkShaderModule Vert;
  VkShaderModule Frag;
};

VkShaderModule createShaderModuleFromFile(const Renderer &_renderer,
                                          const std::string &_filePath);

Shader createShaderFromFile(const Renderer &_renderer,
                            const std::string &_vertShaderFilePath,
                            const std::string &_fragShaderFilePath);

void destroyShader(const Renderer &_renderer, Shader &_shader);

} // namespace bb