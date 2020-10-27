#pragma once
#include "render.h"

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
  std::string ShaderRootPath = "../src/shaders";
  RenderPassType SceneRenderPassType;

  virtual ~SceneBase() = default;
  virtual void updateGUI(float _dt) = 0;
  virtual void updateScene(float _dt) = 0;
  virtual void drawScene() = 0;

  // Utility functions
  Shader loadShader();
};

struct ShaderBallScene : SceneBase {
  struct {
    Buffer VertexBuffer;
    Buffer InstaceBuffer;
    Buffer IndexBuffer;
  } Plane;

  struct {
    Buffer VertexBuffer;
    Buffer InstaceBuffer;
    Buffer IndexBuffer;
  } ShaderBall;

  ~ShaderBallScene() override {}
  void updateGUI(float _dt) override {}
  void updateScene(float _dt) override {}
  void drawScene() override {}
};

} // namespace bb