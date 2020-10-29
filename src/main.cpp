#include "util.h"
#include "gui.h"
#include "enum_array.h"
#include "vector_math.h"
#include "camera.h"
#include "input.h"
#include "render.h"
#include "type_conversion.h"
#include "resource.h"
#include "scene.h"
#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include "external/SDL2/SDL_main.h"
#include "external/SDL2/SDL_syswm.h"
#include "external/SDL2/SDL_vulkan.h"
#include "external/SDL2/SDL_events.h"
#include "external/SDL2/SDL_keycode.h"
#include "external/SDL2/SDL_mouse.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"
#include "external/imgui/imgui_impl_vulkan.h"
#include <WinUser.h>
#include <ShellScalingApi.h>
#include <limits>
#include <chrono>
#include <algorithm>
#include <optional>
#include <numeric>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

namespace bb {

constexpr int numFrames = 2;

static Gizmo gGizmo;
static GBufferVisualize gBufferVisualize;
static LightSources gLightSources;

static StandardPipelineLayout gStandardPipelineLayout;

enum class SceneType { Triangle, ShaderBalls, COUNT };

static EnumArray<SceneType, const char *> gSceneLabels = {"Triangle",
                                                          "Shader Balls"};
static EnumArray<SceneType, SceneBase *> gScenes;
static SceneType gCurrentSceneType = SceneType::ShaderBalls;

void recordCommand(VkRenderPass _deferredRenderPass,
                   VkFramebuffer _deferredFramebuffer,
                   VkPipeline _forwardPipeline, VkPipeline _gBufferPipeline,
                   VkPipeline _brdfPipeline, VkPipeline _hdrToneMappingPipeline,
                   VkExtent2D _swapChainExtent, const Frame &_frame) {
  SceneBase *currentScene = gScenes[gCurrentSceneType];

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = 0;
  cmdBeginInfo.pInheritanceInfo = nullptr;

  VkCommandBuffer cmdBuffer = _frame.CmdBuffer;

  BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

  vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          gStandardPipelineLayout.Handle, 0, 1,
                          &_frame.FrameDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          gStandardPipelineLayout.Handle, 1, 1,
                          &_frame.ViewDescriptorSet, 0, nullptr);

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _deferredRenderPass;
  renderPassInfo.framebuffer = _deferredFramebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapChainExtent;
  EnumArray<DeferredAttachmentType, VkClearValue> clearValues = {};
  renderPassInfo.clearValueCount = clearValues.size();
  renderPassInfo.pClearValues = clearValues.data();
  vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  if (currentScene->SceneRenderPassType == RenderPassType::Deferred) {
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _gBufferPipeline);
    currentScene->drawScene(_frame);
  }

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
  if (currentScene->SceneRenderPassType == RenderPassType::Deferred &&
      gBufferVisualize.CurrentOption ==
          GBufferVisualizingOption::RenderedScene) {

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _brdfPipeline);

    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
  }

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);

  if (currentScene->SceneRenderPassType == RenderPassType::Forward) {
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _forwardPipeline);
    currentScene->drawScene(_frame);
  }

  if (gBufferVisualize.CurrentOption !=
      GBufferVisualizingOption::RenderedScene) {

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      gBufferVisualize.Pipeline);

    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
  }

  // Draw light sources and gizmo
  {
    VkDeviceSize offsets[2] = {};
    VkBuffer vertexBuffers[2] = {gLightSources.VertexBuffer.Handle,
                                 gLightSources.InstanceBuffer.Handle};

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      gLightSources.Pipeline);
    vkCmdBindVertexBuffers(cmdBuffer, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, gLightSources.IndexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuffer, gLightSources.NumIndices,
                     gLightSources.NumLights, 0, 0, 0);

    VkClearAttachment clearDepth = {};
    clearDepth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clearDepth.clearValue.depthStencil = {0.f, 0};
    VkClearRect clearDepthRegion = {};
    clearDepthRegion.rect.offset = {
        (int32_t)(_swapChainExtent.width - gGizmo.ViewportExtent), 0};
    clearDepthRegion.rect.extent = {(uint32_t)gGizmo.ViewportExtent,
                                    (uint32_t)gGizmo.ViewportExtent};
    clearDepthRegion.layerCount = 1;
    clearDepthRegion.baseArrayLayer = 0;
    vkCmdClearAttachments(cmdBuffer, 1, &clearDepth, 1, &clearDepthRegion);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      gGizmo.Pipeline);

    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &gGizmo.VertexBuffer.Handle,
                           offsets);
    vkCmdBindIndexBuffer(cmdBuffer, gGizmo.IndexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmdBuffer, gGizmo.NumIndices, 1, 0, 0, 0);
  }

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _hdrToneMappingPipeline);
  vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer);

  vkCmdEndRenderPass(cmdBuffer);

  BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
}

} // namespace bb

int main(int _argc, char **_argv) {
  using namespace bb;

  SetProcessDPIAware();
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  CommonSceneResources commonSceneResources = {};

  BB_VK_ASSERT(volkInitialize());

  SDL_Init(SDL_INIT_VIDEO);
  int width = 1280;
  int height = 720;
  SDL_Window *window = SDL_CreateWindow(
      "Bibim Renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  Renderer renderer = createRenderer(window);
  commonSceneResources.Renderer = &renderer;

  VkCommandPoolCreateInfo transientCmdPoolCreateInfo = {};
  transientCmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  transientCmdPoolCreateInfo.queueFamilyIndex = renderer.QueueFamilyIndex;
  transientCmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

  VkCommandPool transientCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(renderer.Device, &transientCmdPoolCreateInfo,
                                   nullptr, &transientCmdPool));
  commonSceneResources.TransientCmdPool = transientCmdPool;

  gStandardPipelineLayout = createStandardPipelineLayout(renderer);
  commonSceneResources.StandardPipelineLayout = &gStandardPipelineLayout;

  initResourceRoot();

  // Load gizmo model
  std::vector<GizmoVertex> gizmoVertices;
  std::vector<uint32_t> gizmoIndices;
  {
    Assimp::Importer importer;
    const aiScene *gizmoScene = importer.ReadFile(
        createAbsolutePath("gizmo.obj"), aiProcess_Triangulate);

    {
      size_t numVertices = 0;
      size_t numFaces = 0;

      for (unsigned int meshIndex = 0; meshIndex < gizmoScene->mNumMeshes;
           ++meshIndex) {
        const aiMesh *mesh = gizmoScene->mMeshes[meshIndex];
        numVertices += mesh->mNumVertices;
        numFaces += mesh->mNumFaces;
      }

      gizmoVertices.reserve(numVertices);
      gizmoIndices.reserve(numFaces * 3);
    }

    for (unsigned int meshIndex = 0; meshIndex < gizmoScene->mNumMeshes;
         ++meshIndex) {
      const aiMesh *mesh = gizmoScene->mMeshes[meshIndex];

      aiMaterial *material = gizmoScene->mMaterials[mesh->mMaterialIndex];

      aiMaterialProperty *diffuseProperty = nullptr;
      for (unsigned int propertyIndex = 0;
           propertyIndex < material->mNumProperties; ++propertyIndex) {
        aiMaterialProperty *property = material->mProperties[propertyIndex];
        if ((property->mType == aiPTI_Float) &&
            (property->mDataLength >= (3 * sizeof(float))) &&
            contains(property->mKey.data, "diffuse")) {
          diffuseProperty = property;
          break;
        }
      }
      BB_ASSERT(diffuseProperty);

      float *propertyFloats = (float *)diffuseProperty->mData;
      Float3 color = {propertyFloats[0], propertyFloats[1], propertyFloats[2]};

      uint32_t baseIndex = (uint32_t)gizmoVertices.size();

      for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices;
           ++vertexIndex) {
        GizmoVertex v = {};
        v.Pos = aiVector3DToFloat3(mesh->mVertices[vertexIndex]);
        v.Color = color;
        v.Normal = aiVector3DToFloat3(mesh->mNormals[vertexIndex]);

        gizmoVertices.push_back(v);
      }

      for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces;
           ++faceIndex) {
        const aiFace &face = mesh->mFaces[faceIndex];
        BB_ASSERT(face.mNumIndices == 3);

        gizmoIndices.push_back(baseIndex + face.mIndices[0]);
        gizmoIndices.push_back(baseIndex + face.mIndices[1]);
        gizmoIndices.push_back(baseIndex + face.mIndices[2]);
      }
    }
  }

  std::string shaderRootPath = "../src/shaders";

  Shader gBufferVertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gbuffer.vert.spv")));
  Shader gBufferFragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gbuffer.frag.spv")));

  Shader brdfVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.vert.spv")));
  Shader brdfFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.frag.spv")));

  Shader forwardBrdfVertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "forward_brdf.vert.spv")));
  Shader forwardBrdfFragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "forward_brdf.frag.spv")));

  Shader hdrToneMappingVertShader = createShaderFromFile(
      renderer, createAbsolutePath(
                    joinPaths(shaderRootPath, "hdr_tone_mapping.vert.spv")));
  Shader hdrToneMappingFragShader = createShaderFromFile(
      renderer, createAbsolutePath(
                    joinPaths(shaderRootPath, "hdr_tone_mapping.frag.spv")));

  gGizmo.VertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.vert.spv")));
  gGizmo.FragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.frag.spv")));

  gLightSources.VertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "light.vert.spv")));
  gLightSources.FragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "light.frag.spv")));

  gBufferVisualize.VertShader = createShaderFromFile(
      renderer, createAbsolutePath(
                    joinPaths(shaderRootPath, "buffer_visualize.vert.spv")));
  gBufferVisualize.FragShader = createShaderFromFile(
      renderer, createAbsolutePath(
                    joinPaths(shaderRootPath, "buffer_visualize.frag.spv")));

  PBRMaterialSet materialSet = createPBRMaterialSet(renderer, transientCmdPool);
  commonSceneResources.MaterialSet = &materialSet;

  // Create a descriptor pool corresponding to the standard pipeline layout
  VkDescriptorPool standardDescriptorPool = createStandardDescriptorPool(
      renderer, gStandardPipelineLayout,
      {numFrames, 1, (uint32_t)materialSet.Materials.size(), 1});
  RenderPass deferredRenderPass;

  VkPipeline forwardPipeline;
  VkPipeline gBufferPipeline;
  VkPipeline brdfPipeline;
  VkPipeline hdrToneMappingPipeline;

  PipelineParams forwardPipelineParams = {};
  const Shader *forwardShaders[] = {&forwardBrdfVertShader,
                                    &forwardBrdfFragShader};
  forwardPipelineParams.Shaders = forwardShaders;
  forwardPipelineParams.NumShaders = std::size(forwardShaders);
  forwardPipelineParams.VertexInput.Bindings = Vertex::Bindings.data();
  forwardPipelineParams.VertexInput.NumBindings = Vertex::Bindings.size();
  forwardPipelineParams.VertexInput.Attributes = Vertex::Attributes.data();
  forwardPipelineParams.VertexInput.NumAttributes = Vertex::Attributes.size();
  forwardPipelineParams.InputAssembly.Topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  forwardPipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
  forwardPipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;
  forwardPipelineParams.Blend.NumColorBlends = 1;
  forwardPipelineParams.Subpass =
      (uint32_t)DeferredSubpassType::ForwardLighting;
  forwardPipelineParams.DepthStencil.DepthTestEnable = true;
  forwardPipelineParams.DepthStencil.DepthWriteEnable = true;
  forwardPipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;

  PipelineParams gBufferPipelineParams = {};
  const Shader *gBufferShaders[] = {&gBufferVertShader, &gBufferFragShader};
  gBufferPipelineParams.Shaders = gBufferShaders;
  gBufferPipelineParams.NumShaders = std::size(gBufferShaders);
  gBufferPipelineParams.VertexInput.Bindings = Vertex::Bindings.data();
  gBufferPipelineParams.VertexInput.NumBindings = Vertex::Bindings.size();
  gBufferPipelineParams.VertexInput.Attributes = Vertex::Attributes.data();
  gBufferPipelineParams.VertexInput.NumAttributes = Vertex::Attributes.size();
  gBufferPipelineParams.InputAssembly.Topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  gBufferPipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
  gBufferPipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;
  gBufferPipelineParams.Blend.NumColorBlends = numGBufferAttachments;
  gBufferPipelineParams.Subpass = (uint32_t)DeferredSubpassType::GBufferWrite;
  gBufferPipelineParams.DepthStencil.DepthTestEnable = true;
  gBufferPipelineParams.DepthStencil.DepthWriteEnable = true;
  gBufferPipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;

  PipelineParams brdfPipelineParams = {};
  const Shader *brdfShaders[] = {&brdfVertShader, &brdfFragShader};
  brdfPipelineParams.Shaders = brdfShaders;
  brdfPipelineParams.NumShaders = std::size(brdfShaders);
  brdfPipelineParams.InputAssembly.Topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  brdfPipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
  brdfPipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;
  brdfPipelineParams.Blend.NumColorBlends = 1;
  brdfPipelineParams.Subpass = (uint32_t)DeferredSubpassType::Lighting;
  brdfPipelineParams.DepthStencil.DepthTestEnable = true;
  brdfPipelineParams.DepthStencil.DepthWriteEnable = true;
  brdfPipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;

  PipelineParams hdrToneMappingPipelineParams = {};
  const Shader *hdrToneMappingShaders[] = {&hdrToneMappingVertShader,
                                           &hdrToneMappingFragShader};
  hdrToneMappingPipelineParams.Shaders = hdrToneMappingShaders;
  hdrToneMappingPipelineParams.NumShaders = std::size(hdrToneMappingShaders);
  hdrToneMappingPipelineParams.InputAssembly.Topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  hdrToneMappingPipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
  hdrToneMappingPipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;
  hdrToneMappingPipelineParams.Blend.NumColorBlends = 1;
  hdrToneMappingPipelineParams.Subpass = (uint32_t)DeferredSubpassType::HDR;
  hdrToneMappingPipelineParams.DepthStencil.DepthTestEnable = false;
  hdrToneMappingPipelineParams.DepthStencil.DepthWriteEnable = false;
  hdrToneMappingPipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;

  SwapChain swapChain;
  std::vector<VkFramebuffer> deferredFramebuffers;
  Image gbufferAttachmentImages[numGBufferAttachments] = {};
  Image hdrAttachmentImage = {};

  auto initReloadableResources = [&] {
    swapChain = createSwapChain(renderer, width, height, nullptr);

    gBufferVisualize.ViewportExtent.width = width;
    gBufferVisualize.ViewportExtent.height = height;

    // clang-format off
    // All render passes' first and second attachments' format and sampel should be following:
    // 0 - Color Attachment (swapChain.ColorFormat, VK_SAMPLE_COUNT_1_BIT)
    // 1 - Depth Attachment (swapChain.DepthFormat, VK_SAMPLE_COUNT_1_BIT)
    // clang-format on
    // Create deferred render pass
    {
      EnumArray<DeferredAttachmentType, VkAttachmentDescription> attachments =
          {};
      VkAttachmentDescription &colorAttachment =
          attachments[DeferredAttachmentType::Color];
      colorAttachment.format = swapChain.ColorFormat;
      colorAttachment.samples = swapChain.NumColorSamples;
      colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

      VkAttachmentDescription &depthAttachment =
          attachments[DeferredAttachmentType::Depth];
      depthAttachment.format = swapChain.DepthFormat;
      depthAttachment.samples = swapChain.NumDepthSamples;
      depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      depthAttachment.finalLayout =
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

      VkAttachmentDescription gbufferColorAttachment = {};
      gbufferColorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      gbufferColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      gbufferColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      gbufferColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      gbufferColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      gbufferColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      gbufferColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      gbufferColorAttachment.finalLayout =
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      attachments[DeferredAttachmentType::GBufferPosition] =
          gbufferColorAttachment;
      attachments[DeferredAttachmentType::GBufferNormal] =
          gbufferColorAttachment;
      attachments[DeferredAttachmentType::GBufferAlbedo] =
          gbufferColorAttachment;
      attachments[DeferredAttachmentType::GBufferMRAH] = gbufferColorAttachment;
      attachments[DeferredAttachmentType::GBufferMaterialIndex] =
          gbufferColorAttachment;

      VkAttachmentDescription &hdrAttachment =
          attachments[DeferredAttachmentType::HDR];
      hdrAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      hdrAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      hdrAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      hdrAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      hdrAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      hdrAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      hdrAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      hdrAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkAttachmentReference finalColorAttachmentRef = {
          (uint32_t)DeferredAttachmentType::Color,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };

      VkAttachmentReference depthAttachmentRef = {
          (uint32_t)DeferredAttachmentType::Depth,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      };

      VkAttachmentReference gbufferReadonlyAttachmentRefs[] = {
          {
              (uint32_t)DeferredAttachmentType::GBufferPosition,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferNormal,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferAlbedo,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferMRAH,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferMaterialIndex,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
      };

      VkAttachmentReference gbufferColorAttachmentRefs[] = {
          {
              (uint32_t)DeferredAttachmentType::GBufferPosition,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferNormal,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferAlbedo,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferMRAH,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
          {
              (uint32_t)DeferredAttachmentType::GBufferMaterialIndex,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
      };

      VkAttachmentReference hdrColorAttachmentRef = {
          (uint32_t)DeferredAttachmentType::HDR,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };

      VkAttachmentReference hdrReadonlyAttachmentRef = {
          (uint32_t)DeferredAttachmentType::HDR,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      };

      EnumArray<DeferredSubpassType, VkSubpassDescription> subpasses = {};

      subpasses[DeferredSubpassType::GBufferWrite].pipelineBindPoint =
          VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[DeferredSubpassType::GBufferWrite].colorAttachmentCount =
          std::size(gbufferColorAttachmentRefs);
      subpasses[DeferredSubpassType::GBufferWrite].pColorAttachments =
          gbufferColorAttachmentRefs;
      subpasses[DeferredSubpassType::GBufferWrite].pDepthStencilAttachment =
          &depthAttachmentRef;

      subpasses[DeferredSubpassType::Lighting].pipelineBindPoint =
          VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[DeferredSubpassType::Lighting].inputAttachmentCount =
          std::size(gbufferReadonlyAttachmentRefs);
      subpasses[DeferredSubpassType::Lighting].pInputAttachments =
          gbufferReadonlyAttachmentRefs;
      subpasses[DeferredSubpassType::Lighting].colorAttachmentCount = 1;
      subpasses[DeferredSubpassType::Lighting].pColorAttachments =
          &hdrColorAttachmentRef;

      subpasses[DeferredSubpassType::ForwardLighting].pipelineBindPoint =
          VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[DeferredSubpassType::ForwardLighting].colorAttachmentCount = 1;
      subpasses[DeferredSubpassType::ForwardLighting].pColorAttachments =
          &hdrColorAttachmentRef;
      subpasses[DeferredSubpassType::ForwardLighting].pDepthStencilAttachment =
          &depthAttachmentRef;

      subpasses[DeferredSubpassType::HDR].pipelineBindPoint =
          VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[DeferredSubpassType::HDR].inputAttachmentCount = 1;
      subpasses[DeferredSubpassType::HDR].pInputAttachments =
          &hdrReadonlyAttachmentRef;
      subpasses[DeferredSubpassType::HDR].colorAttachmentCount = 1;
      subpasses[DeferredSubpassType::HDR].pColorAttachments =
          &finalColorAttachmentRef;

      subpasses[DeferredSubpassType::Overlay].pipelineBindPoint =
          VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[DeferredSubpassType::Overlay].colorAttachmentCount = 1;
      subpasses[DeferredSubpassType::Overlay].pColorAttachments =
          &finalColorAttachmentRef;

      VkSubpassDependency subpassDependencies[6] = {};
      subpassDependencies[0].srcSubpass =
          (uint32_t)DeferredSubpassType::GBufferWrite;
      subpassDependencies[0].dstSubpass =
          (uint32_t)DeferredSubpassType::Lighting;
      subpassDependencies[0].srcStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[0].dstStageMask =
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      subpassDependencies[0].srcAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      subpassDependencies[0].dstAccessMask =
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

      subpassDependencies[1].srcSubpass =
          (uint32_t)DeferredSubpassType::GBufferWrite;
      subpassDependencies[1].dstSubpass =
          (uint32_t)DeferredSubpassType::ForwardLighting;
      subpassDependencies[1].srcStageMask =
          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      subpassDependencies[1].dstStageMask =
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      subpassDependencies[1].srcAccessMask =
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      subpassDependencies[1].dstAccessMask =
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

      subpassDependencies[2].srcSubpass =
          (uint32_t)DeferredSubpassType::Lighting;
      subpassDependencies[2].dstSubpass =
          (uint32_t)DeferredSubpassType::ForwardLighting;
      subpassDependencies[2].srcStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[2].dstStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[2].srcAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      subpassDependencies[2].dstAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

      subpassDependencies[3].srcSubpass =
          (uint32_t)DeferredSubpassType::Lighting;
      subpassDependencies[3].dstSubpass = (uint32_t)DeferredSubpassType::HDR;
      subpassDependencies[3].srcStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[3].dstStageMask =
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      subpassDependencies[3].srcAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      subpassDependencies[3].dstAccessMask =
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

      subpassDependencies[4].srcSubpass =
          (uint32_t)DeferredSubpassType::ForwardLighting;
      subpassDependencies[4].dstSubpass = (uint32_t)DeferredSubpassType::HDR;
      subpassDependencies[4].srcStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[4].dstStageMask =
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      subpassDependencies[4].srcAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      subpassDependencies[4].dstAccessMask =
          VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

      subpassDependencies[5].srcSubpass = (uint32_t)DeferredSubpassType::HDR;
      subpassDependencies[5].dstSubpass =
          (uint32_t)DeferredSubpassType::Overlay;
      subpassDependencies[5].srcStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[5].dstStageMask =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      subpassDependencies[5].srcAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      subpassDependencies[5].dstAccessMask =
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

      VkRenderPassCreateInfo renderPassCreateInfo = {};
      renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      renderPassCreateInfo.attachmentCount = attachments.size();
      renderPassCreateInfo.pAttachments = attachments.data();
      renderPassCreateInfo.subpassCount = subpasses.size();
      renderPassCreateInfo.pSubpasses = subpasses.data();
      renderPassCreateInfo.dependencyCount =
          (uint32_t)std::size(subpassDependencies);
      renderPassCreateInfo.pDependencies = subpassDependencies;

      BB_VK_ASSERT(vkCreateRenderPass(renderer.Device, &renderPassCreateInfo,
                                      nullptr, &deferredRenderPass.Handle));
    }

    for (Image &image : gbufferAttachmentImages) {
      ImageParams params = {};
      params.Format = gbufferAttachmentFormat;
      params.Width = swapChain.Extent.width;
      params.Height = swapChain.Extent.height;
      params.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
      image = createImage(renderer, params);
    }

    ImageParams hdrImageParams = {};
    hdrImageParams.Format = hdrAttachmentFormat;
    hdrImageParams.Width = swapChain.Extent.width;
    hdrImageParams.Height = swapChain.Extent.height;
    hdrImageParams.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    hdrAttachmentImage = createImage(renderer, hdrImageParams);

    deferredFramebuffers.resize(swapChain.NumColorImages);
    // Create deferred framebuffer
    for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
      VkFramebufferCreateInfo fbCreateInfo = {};
      fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbCreateInfo.renderPass = deferredRenderPass.Handle;
      EnumArray<DeferredAttachmentType, VkImageView> attachments = {
          swapChain.ColorImageViews[i],    swapChain.DepthImageView,
          gbufferAttachmentImages[0].View, gbufferAttachmentImages[1].View,
          gbufferAttachmentImages[2].View, gbufferAttachmentImages[3].View,
          gbufferAttachmentImages[4].View, hdrAttachmentImage.View,
      };
      fbCreateInfo.attachmentCount = attachments.size();
      fbCreateInfo.pAttachments = attachments.data();
      fbCreateInfo.width = swapChain.Extent.width;
      fbCreateInfo.height = swapChain.Extent.height;
      fbCreateInfo.layers = 1;

      BB_VK_ASSERT(vkCreateFramebuffer(renderer.Device, &fbCreateInfo, nullptr,
                                       &deferredFramebuffers[i]));
    }

    forwardPipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                             (float)swapChain.Extent.height};
    forwardPipelineParams.Viewport.ScissorExtent = {
        (int)swapChain.Extent.width, (int)swapChain.Extent.height};
    forwardPipelineParams.RenderPass = deferredRenderPass.Handle;
    forwardPipeline = createPipeline(renderer, forwardPipelineParams);
    gBufferPipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                             (float)swapChain.Extent.height};
    gBufferPipelineParams.Viewport.ScissorExtent = {
        (int)swapChain.Extent.width, (int)swapChain.Extent.height};
    gBufferPipelineParams.RenderPass = deferredRenderPass.Handle;
    gBufferPipeline = createPipeline(renderer, gBufferPipelineParams);
    brdfPipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                          (float)swapChain.Extent.height};
    brdfPipelineParams.Viewport.ScissorExtent = {(int)swapChain.Extent.width,
                                                 (int)swapChain.Extent.height};
    brdfPipelineParams.RenderPass = deferredRenderPass.Handle;
    brdfPipeline = createPipeline(renderer, brdfPipelineParams);
    hdrToneMappingPipelineParams.Viewport.Extent = {
        (float)swapChain.Extent.width, (float)swapChain.Extent.height};
    hdrToneMappingPipelineParams.Viewport.ScissorExtent = {
        (int)swapChain.Extent.width, (int)swapChain.Extent.height};
    hdrToneMappingPipelineParams.RenderPass = deferredRenderPass.Handle;
    hdrToneMappingPipeline =
        createPipeline(renderer, hdrToneMappingPipelineParams);

    // Gizmo Pipeline
    {
      const Shader *shaders[] = {&gGizmo.VertShader, &gGizmo.FragShader};
      PipelineParams pipelineParams = {};
      pipelineParams.Shaders = shaders;
      pipelineParams.NumShaders = std::size(shaders);

      auto bindings = GizmoVertex::getBindingDescs();
      auto attributes = GizmoVertex::getAttributeDescs();
      pipelineParams.VertexInput.Bindings = bindings.data();
      pipelineParams.VertexInput.NumBindings = bindings.size();
      pipelineParams.VertexInput.Attributes = attributes.data();
      pipelineParams.VertexInput.NumAttributes = attributes.size();

      pipelineParams.InputAssembly.Topology =
          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      pipelineParams.Viewport.Offset = {
          (float)swapChain.Extent.width - gGizmo.ViewportExtent,
          0,
      };
      pipelineParams.Viewport.Extent = {(float)gGizmo.ViewportExtent,
                                        (float)gGizmo.ViewportExtent};
      pipelineParams.Viewport.ScissorOffset = {
          (int)pipelineParams.Viewport.Offset.X,
          (int)pipelineParams.Viewport.Offset.Y,
      };
      pipelineParams.Viewport.ScissorExtent = {(int)gGizmo.ViewportExtent,
                                               (int)gGizmo.ViewportExtent};

      pipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
      pipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;

      pipelineParams.Blend.NumColorBlends = 1;
      pipelineParams.Subpass = (uint32_t)DeferredSubpassType::ForwardLighting;

      pipelineParams.DepthStencil.DepthTestEnable = true;
      pipelineParams.DepthStencil.DepthWriteEnable = true;
      pipelineParams.PipelineLayout = forwardPipelineParams.PipelineLayout;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gGizmo.Pipeline = createPipeline(renderer, pipelineParams);
    }

    // Light Sources Pipeline
    {
      PipelineParams pipelineParams = {};
      const Shader *shaders[] = {&gLightSources.VertShader,
                                 &gLightSources.FragShader};
      pipelineParams.Shaders = shaders;
      pipelineParams.NumShaders = std::size(shaders);

      auto bindings = LightSourceVertex::getBindingDescs();
      auto attributes = LightSourceVertex::getAttributeDescs();

      pipelineParams.VertexInput.Bindings = bindings.data();
      pipelineParams.VertexInput.NumBindings = bindings.size();
      pipelineParams.VertexInput.Attributes = attributes.data();
      pipelineParams.VertexInput.NumAttributes = attributes.size();

      pipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                        (float)swapChain.Extent.height};
      pipelineParams.Viewport.ScissorExtent = {(int)swapChain.Extent.width,
                                               (int)swapChain.Extent.height};

      pipelineParams.InputAssembly.Topology =
          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

      pipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
      pipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;

      pipelineParams.DepthStencil.DepthTestEnable = true;
      pipelineParams.DepthStencil.DepthWriteEnable = true;

      pipelineParams.Blend.NumColorBlends = 1;
      pipelineParams.Subpass = (uint32_t)DeferredSubpassType::ForwardLighting;

      pipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gLightSources.Pipeline = createPipeline(renderer, pipelineParams);
    }

    // Buffer visualizer
    {
      const Shader *shaders[] = {&gBufferVisualize.VertShader,
                                 &gBufferVisualize.FragShader};
      PipelineParams pipelineParams = {};
      pipelineParams.Shaders = shaders;
      pipelineParams.NumShaders = std::size(shaders);

      auto bindings = GizmoVertex::getBindingDescs();
      auto attributes = GizmoVertex::getAttributeDescs();
      pipelineParams.VertexInput.Bindings = bindings.data();
      pipelineParams.VertexInput.NumBindings = bindings.size();
      pipelineParams.VertexInput.Attributes = attributes.data();
      pipelineParams.VertexInput.NumAttributes = attributes.size();

      pipelineParams.InputAssembly.Topology =
          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      pipelineParams.Viewport.Offset = {
          0,
          0,
      };
      pipelineParams.Viewport.Extent = {
          (float)gBufferVisualize.ViewportExtent.width,
          (float)gBufferVisualize.ViewportExtent.height};
      pipelineParams.Viewport.ScissorOffset = {
          (int)pipelineParams.Viewport.Offset.X,
          (int)pipelineParams.Viewport.Offset.Y,
      };
      pipelineParams.Viewport.ScissorExtent = {
          (int)gBufferVisualize.ViewportExtent.width,
          (int)gBufferVisualize.ViewportExtent.height};

      pipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
      pipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;

      pipelineParams.Blend.NumColorBlends = 1;
      pipelineParams.Subpass = (uint32_t)DeferredSubpassType::ForwardLighting;

      pipelineParams.DepthStencil.DepthTestEnable = false;
      pipelineParams.DepthStencil.DepthWriteEnable = false;
      pipelineParams.PipelineLayout = brdfPipelineParams.PipelineLayout;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gBufferVisualize.Pipeline = createPipeline(renderer, pipelineParams);
    }
  };

  auto cleanupReloadableResources = [&] {
    vkDestroyPipeline(renderer.Device, gLightSources.Pipeline, nullptr);
    gLightSources.Pipeline = VK_NULL_HANDLE;

    vkDestroyPipeline(renderer.Device, gGizmo.Pipeline, nullptr);
    gGizmo.Pipeline = VK_NULL_HANDLE;

    vkDestroyPipeline(renderer.Device, gBufferVisualize.Pipeline, nullptr);
    gBufferVisualize.Pipeline = VK_NULL_HANDLE;

    destroyImage(renderer, hdrAttachmentImage);
    for (Image &image : gbufferAttachmentImages) {
      destroyImage(renderer, image);
    }

    for (VkFramebuffer fb : deferredFramebuffers) {
      vkDestroyFramebuffer(renderer.Device, fb, nullptr);
    }
    deferredFramebuffers.clear();

    vkDestroyPipeline(renderer.Device, hdrToneMappingPipeline, nullptr);
    vkDestroyPipeline(renderer.Device, forwardPipeline, nullptr);
    vkDestroyPipeline(renderer.Device, gBufferPipeline, nullptr);
    vkDestroyPipeline(renderer.Device, brdfPipeline, nullptr);

    forwardPipeline = VK_NULL_HANDLE;
    gBufferPipeline = VK_NULL_HANDLE;
    brdfPipeline = VK_NULL_HANDLE;

    vkDestroyRenderPass(renderer.Device, deferredRenderPass.Handle, nullptr);
    deferredRenderPass.Handle = VK_NULL_HANDLE;

    destroySwapChain(renderer, swapChain);
  };

  initReloadableResources();

  std::vector<LightSourceVertex> lightSourceVertices;
  std::vector<uint32_t> lightSourceIndices;
  {
    std::vector<Vertex> sphereVertices;
    generateUVSphereMesh(sphereVertices, lightSourceIndices, 0.1f, 16, 16);
    lightSourceVertices.reserve(sphereVertices.size());
    for (const Vertex &v : sphereVertices) {
      lightSourceVertices.push_back({v.Pos});
    }
  }

  gLightSources.VertexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      sizeBytes32(lightSourceVertices), lightSourceVertices.data());
  gLightSources.IndexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      sizeBytes32(lightSourceIndices), lightSourceIndices.data());
  gLightSources.NumIndices = lightSourceIndices.size();

  gLightSources.InstanceBuffer = createBuffer(
      renderer, sizeof(int) * MAX_NUM_LIGHTS, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  gGizmo.VertexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      sizeBytes32(gizmoVertices), gizmoVertices.data());
  gGizmo.IndexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      sizeBytes32(gizmoIndices), gizmoIndices.data());
  gGizmo.NumIndices = gizmoIndices.size();

  // Imgui descriptor pool and descriptor sets
  VkDescriptorPool imguiDescriptorPool = {};
  {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 10},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10}};
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 10 * IM_ARRAYSIZE(poolSizes);
    poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    BB_VK_ASSERT(vkCreateDescriptorPool(renderer.Device, &poolInfo, nullptr,
                                        &imguiDescriptorPool));
  }

  std::vector<Frame> frames;
  for (int i = 0; i < numFrames; ++i) {
    VkImageView gbufferAttachments[numGBufferAttachments] = {};
    for (uint32_t i = 0; i < numGBufferAttachments; ++i) {
      gbufferAttachments[i] = gbufferAttachmentImages[i].View;
    }
    frames.push_back(createFrame(renderer, gStandardPipelineLayout,
                                 standardDescriptorPool, materialSet,
                                 gbufferAttachments, hdrAttachmentImage.View));
  }

  std::vector<FrameSync> frameSyncObjects;
  for (int i = 0; i < numFrames; ++i) {
    FrameSync syncObject = {};

    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    BB_VK_ASSERT(vkCreateSemaphore(renderer.Device, &semaphoreCreateInfo,
                                   nullptr,
                                   &syncObject.RenderFinishedSemaphore));
    BB_VK_ASSERT(vkCreateSemaphore(renderer.Device, &semaphoreCreateInfo,
                                   nullptr,
                                   &syncObject.ImagePresentedSemaphore));

    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Signaled state
    BB_VK_ASSERT(vkCreateFence(renderer.Device, &fenceCreateInfo, nullptr,
                               &syncObject.FrameAvailableFence));
    frameSyncObjects.push_back(syncObject);
  }

  // Important : You need to delete every cmd used by swapchain
  // through queue. Dont forget to add it here too when you add another cmd.
  auto onWindowResize = [&] {
    int width = 0, height = 0;

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
      SDL_WaitEvent(nullptr);

    SDL_GetWindowSize(window, &width, &height);
    if (width == 0 || height == 0)
      return;

    vkDeviceWaitIdle(
        renderer.Device); // Ensure that device finished using swap chain.

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        renderer.PhysicalDevice, renderer.Surface,
        &renderer.SwapChainSupportDetails.Capabilities);

    cleanupReloadableResources();
    initReloadableResources();

    for (Frame &frame : frames) {
      VkImageView gbufferAttachments[numGBufferAttachments] = {};
      for (uint32_t i = 0; i < numGBufferAttachments; ++i) {
        gbufferAttachments[i] = gbufferAttachmentImages[i].View;
      }
      linkExternalAttachmentsToDescriptorSet(
          renderer, frame, gbufferAttachments, hdrAttachmentImage.View);
    }
  };

  uint32_t currentFrameIndex = 0;
  uint32_t currentSwapChainImageIndex = 0;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForVulkan(window);
  ImGui_ImplVulkan_InitInfo initInfo = {};
  initInfo.Instance = renderer.Instance;
  initInfo.PhysicalDevice = renderer.PhysicalDevice;
  initInfo.Device = renderer.Device;
  initInfo.QueueFamily = renderer.QueueFamilyIndex;
  initInfo.Queue = renderer.Queue;
  initInfo.PipelineCache = nullptr;
  initInfo.DescriptorPool = imguiDescriptorPool;
  initInfo.Allocator = nullptr;
  initInfo.MinImageCount = numFrames;
  initInfo.ImageCount = numFrames;
  initInfo.CheckVkResultFn = nullptr;
  ImGui_ImplVulkan_Init(&initInfo, deferredRenderPass.Handle);

  {
    VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandPool = transientCmdPool;
    cmdBufferAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(renderer.Device, &cmdBufferAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &beginInfo));

    ImGui_ImplVulkan_CreateFontsTexture(cmdBuffer);

    VkSubmitInfo endInfo = {};
    endInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    endInfo.commandBufferCount = 1;
    endInfo.pCommandBuffers = &cmdBuffer;
    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &endInfo, VK_NULL_HANDLE));
    BB_VK_ASSERT(vkQueueWaitIdle(renderer.Queue));

    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }

  FreeLookCamera cam = {};
  Input input = {};

  bool running = true;

  Time lastTime = getCurrentTime();

  SDL_Event e = {};
  while (running) {
    while (SDL_PollEvent(&e) != 0) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      switch (e.type) {
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
        input.MouseDown = (e.button.state == SDL_PRESSED);
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP:
        input.processKeyboardEvents(e);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    Time currentTime = getCurrentTime();
    float dt = getElapsedTimeInSeconds(lastTime, currentTime);
    lastTime = currentTime;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    if (ImGui::Begin("Scene")) {
      if (ImGui::BeginCombo("Select Scene", gSceneLabels[gCurrentSceneType])) {

        for (SceneType sceneType : AllEnums<SceneType>) {
          if (ImGui::Selectable(gSceneLabels[sceneType],
                                sceneType == gCurrentSceneType)) {

            gCurrentSceneType = sceneType;
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
    }
    ImGui::End();

    if (!gScenes[gCurrentSceneType]) {
      switch (gCurrentSceneType) {
      case SceneType::Triangle:
        gScenes[gCurrentSceneType] = new TriangleScene(&commonSceneResources);
        break;
      case SceneType::ShaderBalls:
        gScenes[gCurrentSceneType] = new ShaderBallScene(&commonSceneResources);
        break;
      }
    }

    SceneBase *currentScene = gScenes[gCurrentSceneType];

    if (ImGui::Begin("Render Setting")) {
      EnumArray<RenderPassType, const char *> renderPassOptionLabels = {
          "Forward", "Deferred"};
      if (ImGui::BeginCombo(
              "Scene Render Pass",
              renderPassOptionLabels[currentScene->SceneRenderPassType])) {

        for (auto renderPassType : AllEnums<RenderPassType>) {
          if (ImGui::Selectable(renderPassOptionLabels[renderPassType],
                                currentScene->SceneRenderPassType ==
                                    renderPassType)) {
            currentScene->SceneRenderPassType = renderPassType;
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (currentScene->SceneRenderPassType == RenderPassType::Deferred) {
        if (ImGui::BeginCombo(
                "Deferred Buffer",
                gBufferVisualize

                    .OptionLabels[gBufferVisualize.CurrentOption])) {

          for (auto option : AllEnums<GBufferVisualizingOption>) {
            bool isSelected = (gBufferVisualize.CurrentOption == option);

            if (ImGui::Selectable(gBufferVisualize.OptionLabels[option],
                                  isSelected)) {
              gBufferVisualize.CurrentOption = option;
            }

            if (isSelected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }
    }
    ImGui::End();

    currentScene->updateGUI(dt);

    SDL_GetWindowSize(window, &width, &height);

    Int2 currentCursorScreenPos;
    SDL_GetMouseState(&currentCursorScreenPos.X, &currentCursorScreenPos.Y);
    input.CursorScreenDelta = currentCursorScreenPos - input.CursorScreenPos;
    input.CursorScreenPos = currentCursorScreenPos;

    if (input.MouseDown && !ImGui::GetIO().WantCaptureMouse) {
      cam.Yaw -= (float)input.CursorScreenDelta.X * 0.6f;
      cam.Pitch -= (float)input.CursorScreenDelta.Y * 0.6f;
      cam.Pitch = std::clamp(cam.Pitch, -88.f, 88.f);
    }

    Int2 direction = {};
    if (input.isKeyDown(SDLK_a)) {
      direction.X -= 1;
    }
    if (input.isKeyDown(SDLK_d)) {
      direction.X += 1;
    }
    if (input.isKeyDown(SDLK_w)) {
      direction.Y += 1;
    }
    if (input.isKeyDown(SDLK_s)) {
      direction.Y -= 1;
    }

    float camMovementSpeed = 4.f;
    Float3 camMovement =
        (cam.getRight() * (float)direction.X * camMovementSpeed +
         cam.getLook() * direction.Y * camMovementSpeed) *
        dt;
    cam.Pos += camMovement;

    Frame &currentFrame = frames[currentFrameIndex];
    const FrameSync &frameSyncObject = frameSyncObjects[currentFrameIndex];

    VkResult acquireNextImageResult =
        vkAcquireNextImageKHR(renderer.Device, swapChain.Handle, UINT64_MAX,
                              frameSyncObject.ImagePresentedSemaphore,
                              VK_NULL_HANDLE, &currentSwapChainImageIndex);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      onWindowResize();
      continue;
    }

    vkWaitForFences(renderer.Device, 1, &frameSyncObject.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &frameSyncObject.FrameAvailableFence);

    VkFramebuffer currentDeferredFramebuffer =
        deferredFramebuffers[currentSwapChainImageIndex];

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)frames.size();

    currentScene->updateScene(dt);

    FrameUniformBlock frameUniformBlock = {};
    BB_ASSERT(currentScene->Lights.size() <
              std::size(frameUniformBlock.Lights));
    frameUniformBlock.NumLights = currentScene->Lights.size();
    gLightSources.NumLights = frameUniformBlock.NumLights;
    memcpy(frameUniformBlock.Lights, currentScene->Lights.data(),
           sizeBytes32(currentScene->Lights));

    if (gBufferVisualize.CurrentOption !=
        GBufferVisualizingOption::RenderedScene) {
      frameUniformBlock.VisualizedGBufferAttachmentIndex =
          (int)gBufferVisualize.CurrentOption;
    }

    {

      void *data;
      vkMapMemory(renderer.Device, currentFrame.FrameUniformBuffer.Memory, 0,
                  sizeof(FrameUniformBlock), 0, &data);
      memcpy(data, &frameUniformBlock, sizeof(FrameUniformBlock));
      vkUnmapMemory(renderer.Device, currentFrame.FrameUniformBuffer.Memory);
    }

    static bool enableNormalMap;
    if (ImGui::Begin("Settings")) {
      ImGui::Checkbox("Enable Normal Map", &enableNormalMap);
    }
    ImGui::End();

    ViewUniformBlock viewUniformBlock = {};
    viewUniformBlock.ViewMat = cam.getViewMatrix();
    viewUniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    viewUniformBlock.ViewPos = cam.Pos;
    viewUniformBlock.EnableNormalMap = enableNormalMap;

    {
      void *data;
      vkMapMemory(renderer.Device, currentFrame.ViewUniformBuffer.Memory, 0,
                  sizeof(ViewUniformBlock), 0, &data);
      memcpy(data, &viewUniformBlock, sizeof(ViewUniformBlock));
      vkUnmapMemory(renderer.Device, currentFrame.ViewUniformBuffer.Memory);
    }

    vkResetCommandPool(renderer.Device, currentFrame.CmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    ImGui::Render();
    recordCommand(deferredRenderPass.Handle, currentDeferredFramebuffer,
                  forwardPipeline, gBufferPipeline, brdfPipeline,
                  hdrToneMappingPipeline, swapChain.Extent, currentFrame);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frameSyncObject.ImagePresentedSemaphore;
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frameSyncObject.RenderFinishedSemaphore;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentFrame.CmdBuffer;

    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo,
                               frameSyncObject.FrameAvailableFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frameSyncObject.RenderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain.Handle;
    presentInfo.pImageIndices = &currentSwapChainImageIndex;

    VkResult queuePresentResult =
        vkQueuePresentKHR(renderer.Queue, &presentInfo);
    if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        queuePresentResult == VK_SUBOPTIMAL_KHR) {
      onWindowResize();
    }
  }

  vkDeviceWaitIdle(renderer.Device);

  for (SceneBase *&scene : gScenes) {
    delete scene;
    scene = nullptr;
  }

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  for (FrameSync &frameSyncObject : frameSyncObjects) {
    vkDestroyFence(renderer.Device, frameSyncObject.FrameAvailableFence,
                   nullptr);
    vkDestroySemaphore(renderer.Device, frameSyncObject.ImagePresentedSemaphore,
                       nullptr);
    vkDestroySemaphore(renderer.Device, frameSyncObject.RenderFinishedSemaphore,
                       nullptr);
    frameSyncObject = {};
  }

  for (Frame &frame : frames) {
    destroyFrame(renderer, frame);
  }

  vkDestroyDescriptorPool(renderer.Device, standardDescriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, imguiDescriptorPool, nullptr);

  destroyBuffer(renderer, gLightSources.InstanceBuffer);
  destroyBuffer(renderer, gLightSources.IndexBuffer);
  destroyBuffer(renderer, gLightSources.VertexBuffer);
  destroyBuffer(renderer, gGizmo.IndexBuffer);
  destroyBuffer(renderer, gGizmo.VertexBuffer);

  cleanupReloadableResources();

  destroyStandardPipelineLayout(renderer, gStandardPipelineLayout);

  destroyPBRMaterialSet(renderer, materialSet);

  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);

  destroyShader(renderer, gLightSources.VertShader);
  destroyShader(renderer, gLightSources.FragShader);
  destroyShader(renderer, gGizmo.VertShader);
  destroyShader(renderer, gGizmo.FragShader);
  destroyShader(renderer, hdrToneMappingFragShader);
  destroyShader(renderer, hdrToneMappingVertShader);
  destroyShader(renderer, brdfVertShader);
  destroyShader(renderer, brdfFragShader);
  destroyShader(renderer, gBufferVertShader);
  destroyShader(renderer, gBufferFragShader);
  destroyShader(renderer, forwardBrdfVertShader);
  destroyShader(renderer, forwardBrdfFragShader);
  destroyShader(renderer, gBufferVisualize.VertShader);
  destroyShader(renderer, gBufferVisualize.FragShader);
  destroyRenderer(renderer);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}