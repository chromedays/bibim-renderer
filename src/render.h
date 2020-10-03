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

enum class LightType : int { Point = 0, Spot, Directional };

struct alignas(16) Light {
  Float3 Pos;
  LightType Type;
  Float3 Dir;
  float Intensity;
  Float3 Color;
  float InnerCutOff;
  float OuterCutOff;
};

#define MAX_NUM_LIGHTS 100

struct UniformBlock {
  Mat4 ViewMat;
  Mat4 ProjMat;
  alignas(16) Float3 ViewPos;
  int NumLights;
  Light lights[MAX_NUM_LIGHTS];
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
  Float3 Tangent = {0, -1, 0};

  static std::array<VkVertexInputBindingDescription, 2> getBindingDescs();
  static std::array<VkVertexInputAttributeDescription, 17> getAttributeDescs();
  static VkPipelineVertexInputStateCreateInfo getVertexInputState();
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
  VkShaderStageFlagBits Stage;
  VkShaderModule Handle;

  VkPipelineShaderStageCreateInfo getStageInfo() const;
};

Shader createShaderFromFile(const Renderer &_renderer,
                            const std::string &_filePath);
void destroyShader(const Renderer &_renderer, Shader &_shader);

struct PipelineParams {
  const Shader **Shaders;
  int NumShaders;

  struct {
    VkVertexInputBindingDescription *Bindings;
    int NumBindings;
    VkVertexInputAttributeDescription *Attributes;
    int NumAttributes;
  } VertexInput;

  struct {
    VkPrimitiveTopology Topology;
  } InputAssembly;

  struct {
    Float2 Offset;
    Float2 Extent;
    Int2 ScissorOffset;
    Int2 ScissorExtent;
  } Viewport;

  struct {
    VkPolygonMode PolygonMode;
    VkCullModeFlags CullMode;
  } Rasterizer;

  struct {
    bool DepthTestEnable;
    bool DepthWriteEnable;
  } DepthStencil;

  VkPipelineLayout PipelineLayout;
  VkRenderPass RenderPass;
};

VkPipeline createPipeline(const Renderer &_renderer,
                          const PipelineParams &_params);

struct PBRMaterial {
  static constexpr int NumImages = 6;

  Image AlbedoMap;
  Image MetallicMap;
  Image RoughnessMap;
  Image AOMap;
  Image NormalMap;
  Image HeightMap;
};

PBRMaterial createPBRMaterialFromFiles(const Renderer &_renderer,
                                       VkCommandPool _transientCmdPool,
                                       const std::string &_rootPath);
void destroyPBRMaterial(const Renderer &_renderer, PBRMaterial &_material);

struct Frame {
  VkCommandPool CmdPool;
  VkCommandBuffer CmdBuffer;
  VkDescriptorPool DescriptorPool;
  VkDescriptorSet DescriptorSet;
  Buffer UniformBuffer;
  VkFence FrameAvailableFence;
  VkSemaphore RenderFinishedSemaphore;
  VkSemaphore ImagePresentedSemaphore;
};

Frame createFrame(const Renderer &_renderer, VkDescriptorPool _descriptorPool,
                  VkDescriptorSetLayout _descriptorSetLayout,
                  const std::vector<PBRMaterial> &_pbrMaterials);

void destroyFrame(const Renderer &_renderer, Frame &_frame);

} // namespace bb