#pragma once
#include "render.h"
#include "external/imgui/imgui.h"

namespace bb {
struct Gizmo {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;
  Buffer VertexBuffer;
  Buffer IndexBuffer;
  uint32_t NumIndices;

  int ViewportExtent = 100;
};

enum class GBufferVisualizingOption {
  Position,
  Normal,
  Albedo,
  MRHA,
  MaterialIndex,
  RenderedScene,
  COUNT
};

struct GBufferVisualize {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;

  VkExtent2D ViewportExtent;

  StandardPipelineLayout PipelineLayout;

  EnumArray<GBufferVisualizingOption, const char *> OptionLabels = {
      "Position", "Normal",         "Albedo",
      "MRHA",     "Material index", "Rendered Scene"};
  GBufferVisualizingOption CurrentOption =
      GBufferVisualizingOption::RenderedScene;
};

struct LightSources {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;
  Buffer VertexBuffer;
  Buffer IndexBuffer;
  uint32_t NumIndices;
  Buffer InstanceBuffer;
  uint32_t NumLights;
};

enum class RenderPassType { Forward, Deferred, COUNT };

// CommonSceneResources doesn't own actual resources, but only references of
// them.
struct CommonSceneResources {
  SDL_Window *Window;
  Renderer *Renderer;
  VkCommandPool TransientCmdPool;

  StandardPipelineLayout *StandardPipelineLayout;

  SwapChain *SwapChain;

  RenderPass *RenderPass;
  std::vector<VkFramebuffer> *Framebuffers;
  EnumArray<GBufferAttachmentType, Image> *GBufferAttachmentImages;

  VkPipeline GBufferPipeline;
  VkPipeline DeferredBrdfPipeline;
  VkPipeline ForwardBrdfPipeline;

  std::vector<Frame> *Frames;
  std::vector<FrameSync> *FrameSyncObjects;

  Gizmo *Gizmo;
  GBufferVisualize *GBufferVisualize;
};

struct SceneBase {
  CommonSceneResources *Common;
  RenderPassType SceneRenderPassType = RenderPassType::Deferred;

  explicit SceneBase(CommonSceneResources *_common) : Common(_common) {}
  virtual ~SceneBase() = default;
  virtual void updateGUI(float _dt) = 0;
  virtual void updateScene(float _dt) = 0;
  virtual void drawScene(VkCommandBuffer _cmd) = 0;
};

struct ShaderBallScene : SceneBase {
  struct {
    Buffer VertexBuffer;
    Buffer IndexBuffer;
    uint32_t NumIndices;

    uint32_t NumInstances = 1;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;
  } Plane;

  struct {
    Buffer VertexBuffer;
    Buffer IndexBuffer;
    uint32_t NumIndices;

    uint32_t NumInstances = 30;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;

    float Angle = -90;
  } ShaderBall;

  PBRMaterialSet MaterialSet;

  struct {
    EnumArray<PBRMapType, ImTextureID> DefaultMaterialTextureId;
    std::vector<EnumArray<PBRMapType, ImTextureID>> MaterialTextureIds;
    int SelectedMaterial = 1;
    int SelectedShaderBallInstance = -1;
  } GUI;

  explicit ShaderBallScene(CommonSceneResources *_common);
  ~ShaderBallScene() override;
  void updateGUI(float _dt) override;
  void updateScene(float _dt) override;
  void drawScene(VkCommandBuffer _cmd) override;
};

} // namespace bb