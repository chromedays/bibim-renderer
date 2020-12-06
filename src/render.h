#pragma once
// Render Pass Compat
// - Format
// - Sample Count
// - Both are null references (either a null pointer or VK_ATTACHMENT_UNUSED)
// - Color
#include "vector_math.h"
#include "enum_array.h"
#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include <array>

namespace bb {
constexpr int numFrames = 2;
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
  VkPhysicalDeviceFeatures PhysicalDeviceFeatures;

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
  VkSampleCountFlagBits NumColorSamples = VK_SAMPLE_COUNT_1_BIT;
  VkSampleCountFlagBits NumDepthSamples = VK_SAMPLE_COUNT_1_BIT;
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

enum class DeferredAttachmentType {
  Color,
  Depth,
  GBufferPosition,
  GBufferNormal,
  GBufferAlbedo,
  GBufferMRAH,
  GBufferMaterialIndex,
  HDR,
  COUNT
};
constexpr uint32_t numGBufferAttachments =
    (uint32_t)DeferredAttachmentType::HDR -
    (uint32_t)DeferredAttachmentType::GBufferPosition;

enum class DeferredSubpassType {
  GBufferWrite,
  Lighting,
  ForwardLighting,
  HDR,
  Overlay,
  COUNT
};

constexpr VkFormat gbufferAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat hdrAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct InstanceBlock {
  Mat4 ModelMat;
  Mat4 InvModelMat;
};

#define VERTEX_BINDINGS_DECL(numBindings)                                      \
  using BindingDescs =                                                         \
      std::array<VkVertexInputBindingDescription, numBindings>;                \
  static BindingDescs getBindingDescs();                                       \
  inline static BindingDescs Bindings = getBindingDescs();
#define VERTEX_ATTRIBUTES_DECL(numAttributes)                                  \
  using AttributeDescs =                                                       \
      std::array<VkVertexInputAttributeDescription, numAttributes>;            \
  static AttributeDescs getAttributeDescs();                                   \
  inline static AttributeDescs Attributes = getAttributeDescs();

struct Vertex {
  Float3 Pos;
  Float2 UV;
  Float3 Normal = {0, 0, -1};
  Float3 Tangent = {0, -1, 0};

  VERTEX_BINDINGS_DECL(2);
  VERTEX_ATTRIBUTES_DECL(12);
};

struct GizmoVertex {
  Float3 Pos;
  Float3 Color;
  Float3 Normal;

  VERTEX_BINDINGS_DECL(1);
  VERTEX_ATTRIBUTES_DECL(3);
};

struct LightSourceVertex {
  Float3 Pos;

  VERTEX_BINDINGS_DECL(2);
  VERTEX_ATTRIBUTES_DECL(2);
};

struct SkyVertex {
  Float3 Pos;

  VERTEX_BINDINGS_DECL(1);
  VERTEX_ATTRIBUTES_DECL(1);
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
                                         VkDeviceSize _size, const void *_data);

void destroyBuffer(const Renderer &_renderer, Buffer &_buffer);
void copyBuffer(const Renderer &_renderer, VkCommandPool _cmdPool,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size);

struct Image {
  VkImage Handle;
  VkDeviceMemory Memory;
  VkImageView View;
  uint32_t Width;
  uint32_t Height;
};

struct ImageParams {
  VkFormat Format;
  uint32_t Width;
  uint32_t Height;
  VkImageUsageFlags Usage;
};

Image createImage(const Renderer &_renderer, const ImageParams &_params);
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
};

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
    uint32_t NumColorBlends;
  } Blend;

  uint32_t Subpass;

  VkPipelineLayout PipelineLayout;
  VkRenderPass RenderPass;
};

VkPipeline createPipeline(const Renderer &_renderer,
                          const PipelineParams &_params);
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
  static constexpr auto NumImages = EnumCount<PBRMapType>;
  std::string Name;
  EnumArray<PBRMapType, Image> Maps;
};

PBRMaterial createPBRMaterialFromFiles(const Renderer &_renderer,
                                       VkCommandPool _transientCmdPool,
                                       const std::string &_rootPath);
void destroyPBRMaterial(const Renderer &_renderer, PBRMaterial &_material);

struct PBRMaterialSet {
  std::vector<PBRMaterial> Materials;
  PBRMaterial DefaultMaterial;
};

PBRMaterialSet createPBRMaterialSet(const Renderer &_renderer,
                                    VkCommandPool _cmdPool);
void destroyPBRMaterialSet(const Renderer &_renderer,
                           PBRMaterialSet &_materialSet);

Image getPBRMapOrDefault(const PBRMaterialSet &_materialSet, int _materialIndex,
                         PBRMapType _mapType);

enum class DescriptorFrequency {
  PerFrame,
  PerView,
  PerMaterial,
  PerDraw,
  COUNT
};

enum class SamplerType { Nearest, Linear, COUNT };

struct DescriptorBinding {
  VkDescriptorType Type;
  uint32_t NumDescriptors;
  VkSampler *ImmutableSamplers;
};

constexpr uint32_t maxNumDescriptorBindings = 16;

struct DescriptorSetLayout {
  VkDescriptorSetLayout Handle;
  DescriptorBinding Bindings[maxNumDescriptorBindings];
  uint32_t NumBindings;
};

DescriptorSetLayout
createDescriptorSetLayout(const Renderer &_renderer,
                          const std::vector<DescriptorBinding> &_bindings);

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

VkDescriptorPool createStandardDescriptorPool(
    const Renderer &_renderer, const StandardPipelineLayout &_layout,
    const EnumArray<DescriptorFrequency, uint32_t> &_numSets);

struct ComputePipelineLayout {
  DescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout Handle;
};

struct ComputePipeline {

  DescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout PipelineLayout;
  VkPipeline Handle;
};

ComputePipeline
createComputePipeline(const Renderer &_renderer,
                      const std::vector<DescriptorBinding> &_bindings,
                      const Shader &_shader);
void destroyComputePipeline(const Renderer &_renderer,
                            ComputePipeline &_pipeline);

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
  int EnableToneMapping;
  float Exposure;
};

struct ViewUniformBlock {
  Mat4 ViewMat;
  Mat4 ProjMat;
  Float3 ViewPos;
  int EnableNormalMap;
};

struct Frame {
  VkDescriptorSet FrameDescriptorSet;
  VkDescriptorSet ViewDescriptorSet;
  std::vector<VkDescriptorSet> MaterialDescriptorSets;

  Buffer FrameUniformBuffer;
  Buffer ViewUniformBuffer;

  VkCommandPool CmdPool;
  VkCommandBuffer CmdBuffer;
};

struct FrameSync {
  VkFence FrameAvailableFence;
  VkSemaphore RenderFinishedSemaphore;
  VkSemaphore ImagePresentedSemaphore;
};

Frame createFrame(
    const Renderer &_renderer,
    const StandardPipelineLayout &_standardPipelineLayout,
    VkDescriptorPool _descriptorPool, const PBRMaterialSet &_materialSet,
    const VkImageView (&_gbufferAttachments)[numGBufferAttachments],
    VkImageView _hdrAttachment);
void destroyFrame(const Renderer &_renderer, Frame &_frame);

void linkExternalAttachmentsToDescriptorSet(
    const Renderer &_renderer, Frame &_frame,
    const VkImageView (&_gbufferAttachments)[numGBufferAttachments],
    VkImageView _hdrAttachment);

void generatePlaneMesh(std::vector<Vertex> &_vertices,
                       std::vector<uint32_t> &_indices);
void generateQuadMesh(std::vector<Vertex> &_vertices,
                      std::vector<uint32_t> &_indices);

// The tangent vector of UV Sphere will be broken at the top and the bottom
// side, because of how UV Sphere is constructed.
void generateUVSphereMesh(std::vector<Vertex> &_vertices,
                          std::vector<uint32_t> &_indices, float _radius = 1.f,
                          int _horizontalDivision = 16,
                          int _verticalDivision = 16);

#if BB_DEBUG
void labelGPUResource(const Renderer &_renderer, const Image &_image,
                      const std::string &_name);
#endif

} // namespace bb