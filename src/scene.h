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

struct TBNVisualize {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader GeomShader;
  Shader FragShader;

  bool IsSupported = false;
  bool IsEnabled = false;
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
  Renderer *Renderer;
  VkCommandPool TransientCmdPool;
  StandardPipelineLayout *StandardPipelineLayout;
  PBRMaterialSet *MaterialSet;
};

struct SceneBase {
  CommonSceneResources *Common;
  RenderPassType SceneRenderPassType = RenderPassType::Deferred;
  std::vector<Light> Lights;

  explicit SceneBase(CommonSceneResources *_common) : Common(_common) {}
  virtual ~SceneBase() = default;
  virtual void updateGUI(float _dt) = 0;
  virtual void updateScene(float _dt) = 0;
  virtual void drawScene(const Frame &_frame) = 0;

  template <typename Container>
  Buffer createVertexBuffer(const Container &_vertices) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_vertices), Vertex>,
                  "Element type for _vertices is not Vertex!");
    const Renderer &renderer = *Common->Renderer;
    VkCommandPool transientCmdPool = Common->TransientCmdPool;
    Buffer vertexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeBytes32(_vertices), std::data(_vertices));
    return vertexBuffer;
  }

  template <typename Container>
  Buffer createIndexBuffer(const Container &_indices) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_indices), uint32_t>,
                  "Element type for _indices is not uint32_t!");
    const Renderer &renderer = *Common->Renderer;
    VkCommandPool transientCmdPool = Common->TransientCmdPool;
    Buffer indexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeBytes32(_indices), std::data(_indices));
    return indexBuffer;
  }

  Buffer createInstanceBuffer(uint32_t _numInstances) const {
    const Renderer &renderer = *Common->Renderer;
    Buffer instanceBuffer =
        createBuffer(renderer, sizeof(InstanceBlock) * _numInstances,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    return instanceBuffer;
  }

  template <typename Container>
  void updateInstanceBufferMemory(const Buffer &_instanceBuffer,
                                  const Container &_instanceData) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_instanceData), InstanceBlock>,
                  "Element type for _instanceData is not InstanceBlock!");
    const Renderer &renderer = *Common->Renderer;

    void *dst;
    vkMapMemory(renderer.Device, _instanceBuffer.Memory, 0,
                _instanceBuffer.Size, 0, &dst);
    memcpy(dst, std::data(_instanceData), _instanceBuffer.Size);
    vkUnmapMemory(renderer.Device, _instanceBuffer.Memory);
  }
};

struct TriangleScene : SceneBase {
  Buffer VertexBuffer;
  uint32_t NumVertices;
  Buffer InstanceBuffer;

  explicit TriangleScene(CommonSceneResources *_common) : SceneBase(_common) {
    Lights.resize(1);
    Light *light = &Lights[0];
    light->Dir = {-1, -1, 0};
    light->Type = LightType::Directional;
    light->Color = {0.0347f, 0.0131f, 0.2079f};
    light->Intensity = 10.f;

    // clang-format off
    Vertex vertices[] = {
        {{0, 1, 5}, {0.5, 1}},
        {{1, -1, 5}, {1, 0}},
        {{-1, -1, 5}, {0, 0}}};
    // clang-format on
    VertexBuffer = createVertexBuffer(vertices);
    NumVertices = std::size(vertices);
    InstanceBuffer = createInstanceBuffer(1);
    InstanceBlock instanceData[1] = {};
    instanceData[0].ModelMat = Mat4::identity();
    instanceData[0].InvModelMat = Mat4::identity();
    updateInstanceBufferMemory(InstanceBuffer, instanceData);
  }

  ~TriangleScene() override {
    const Renderer &renderer = *Common->Renderer;
    destroyBuffer(renderer, InstanceBuffer);
    destroyBuffer(renderer, VertexBuffer);
  }
  void updateGUI(float _dt) override {}
  void updateScene(float _dt) override {}
  void drawScene(const Frame &_frame) override {
    VkCommandBuffer cmd = _frame.CmdBuffer;
    const StandardPipelineLayout &standardPipelineLayout =
        *Common->StandardPipelineLayout;

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            standardPipelineLayout.Handle, 2, 1,
                            &_frame.MaterialDescriptorSets[0], 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &VertexBuffer.Handle, &offset);
    vkCmdBindVertexBuffers(cmd, 1, 1, &InstanceBuffer.Handle, &offset);
    vkCmdDraw(cmd, NumVertices, 1, 0, 0);
  }
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
    uint32_t NumVertices;

    uint32_t NumInstances = 1;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;

    float Angle = -90;
  } ShaderBall;

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
  void drawScene(const Frame &_frame) override;
};

struct SponzaScene : SceneBase {
  struct {
    Buffer VertexBuffer;
    uint32_t NumVertices;
    Buffer IndexBuffer;
    uint32_t NumIndices;

    uint32_t NumInstances = 1;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;
  } Sponza;

  struct Mesh{
    uint32_t Index;
    uint32_t NumIndies;
    uint32_t Offset;
  };

  std::vector<Mesh> MeshGroups;

  explicit SponzaScene(CommonSceneResources *_common);
  ~SponzaScene() override;
  void updateGUI(float _dt) override;
  void updateScene(float _dt) override;
  void drawScene(const Frame &_frame) override;
};

} // namespace bb