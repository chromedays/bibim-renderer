#pragma once
#include "vector_math.h"
#include "enum_array.h"
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
  VkImageView DepthImageView;
  VkDeviceMemory DepthImageMemory;
};

SwapChain createSwapChain(const Renderer &_renderer, uint32_t _width,
                          uint32_t _height,
                          const SwapChain *_oldSwapChain = nullptr);
void destroySwapChain(const Renderer &_renderer, SwapChain &_swapChain);

enum class LightType : int { Point = 0, Spot, Directional };

enum class GBufferAttachmentType {
  Position,
  Normal,
  Albedo,
  MRAH,
  MaterialIndex,
  COUNT
};

struct InstanceBlock {
  Mat4 ModelMat;
  Mat4 InvModelMat;
  alignas(16) Float3 Albedo = {1, 1, 1};
  float Metallic;
  float Roughness = 0.5f;
  float AO = 1;
};

struct Vertex {
  Float3 Pos;
  Float2 UV;
  Float3 Normal = {0, 0, -1};
  Float3 Tangent = {0, -1, 0};

  static std::array<VkVertexInputBindingDescription, 2> getBindingDescs();
  static std::array<VkVertexInputAttributeDescription, 12> getAttributeDescs();
  static VkPipelineVertexInputStateCreateInfo getVertexInputState();
};

struct GizmoVertex {
  Float3 Pos;
  Float3 Color;
  Float3 Normal;

  static std::array<VkVertexInputBindingDescription, 1> getBindingDescs();
  static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescs();
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
Buffer createDeviceLocalBufferFromMemory(const Renderer &_renderer,
                                         VkCommandPool _cmdPool,
                                         VkBufferUsageFlags _usage,
                                         VkDeviceSize _size, void *_data);

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

struct RenderPass {
  VkRenderPass Handle;
  // TODO(ilgwon): Not implemented yet
  VkRenderPassBeginInfo BeginInfo = {};
};

RenderPass createForwardRenderPass(const Renderer &_renderer,
                                   const SwapChain &_swapChain);
RenderPass createDeferredRenderPass(const Renderer &_renderer,
                                    const SwapChain &_swapChain);

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

  struct {
    uint32_t NumAttachments;
  } Blend;

  VkPipelineLayout PipelineLayout;
  VkRenderPass RenderPass;
};

VkPipeline createPipeline(const Renderer &_renderer,
                          const PipelineParams &_params,
                          uint32_t _numColorBlend = 1, uint32_t _subpass = 0);
enum class PBRMapType {
  Albedo,
  Metallic,
  Roughness,
  AO,
  Normal,
  Height,
  COUNT
};

struct PBRMaterial {
  static constexpr int NumImages = (int)PBRMapType::COUNT;
  EnumArray<PBRMapType, Image> Maps;
};

PBRMaterial createPBRMaterialFromFiles(const Renderer &_renderer,
                                       VkCommandPool _transientCmdPool,
                                       const std::string &_rootPath);
void destroyPBRMaterial(const Renderer &_renderer, PBRMaterial &_material);

enum class DescriptorFrequency {
  PerFrame,
  PerView,
  PerMaterial,
  PerDraw,
  COUNT
};

enum class SamplerType { Nearest, Linear, COUNT };

struct DescriptorSetLayout {
  VkDescriptorSetLayout Handle;
  std::unordered_map<VkDescriptorType, uint32_t> NumDescriptorsTable;
};

struct StandardPipelineLayout {
  EnumArray<SamplerType, VkSampler> ImmutableSamplers;
  EnumArray<DescriptorFrequency, DescriptorSetLayout> DescriptorSetLayouts;
  VkPipelineLayout Handle;
};

EnumArray<SamplerType, VkSampler>
createImmutableSamplers(const Renderer &_renderer);

StandardPipelineLayout createStandardPipelineLayout(const Renderer &_renderer);

void destroyStandardPipelineLayout(const Renderer &_renderer,
                                   StandardPipelineLayout &_layout);

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
struct FrameUniformBlock {
  int NumLights;
  Light Lights[MAX_NUM_LIGHTS];
  int VisualizedGBufferAttachmentIndex;
};

struct ViewUniformBlock {
  Mat4 ViewMat;
  Mat4 ProjMat;
  Float3 ViewPos;
};

struct Frame {
  VkDescriptorSet FrameDescriptorSet;
  VkDescriptorSet ViewDescriptorSet;
  std::vector<VkDescriptorSet> MaterialDescriptorSets;

  Buffer FrameUniformBuffer;
  Buffer ViewUniformBuffer;
};

struct FrameSync {
  VkFence FrameAvailableFence;
  VkSemaphore RenderFinishedSemaphore;
  VkSemaphore ImagePresentedSemaphore;
};

Frame createFrame(
    const Renderer &_renderer,
    const StandardPipelineLayout &_standardPipelineLayout,
    VkDescriptorPool _descriptorPool,
    const std::vector<PBRMaterial> &_pbrMaterials,
    EnumArray<GBufferAttachmentType, Image> &_deferredAttachmentImages);

void destroyFrame(const Renderer &_renderer, Frame &_frame);

} // namespace bb