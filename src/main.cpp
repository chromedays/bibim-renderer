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

constexpr uint32_t numInstances = 30;
constexpr int numFrames = 2;

static Gizmo gGizmo;
static GBufferVisualize gBufferVisualize;
static LightSources gLightSources;

Buffer gPlaneInstanceBuffer;
Buffer gPlaneVertexBuffer;
Buffer gPlaneIndexBuffer;
uint32_t gNumPlaneIndices;

int gCurrentMaterial = 1;
static StandardPipelineLayout gStandardPipelineLayout;

RenderPassType gSceneRenderPassType = RenderPassType::Forward;

static ShaderBallScene *gShaderBallScene;

void recordCommand(VkRenderPass _deferredRenderPass,
                   VkFramebuffer _deferredFramebuffer,
                   VkPipeline _forwardPipeline, VkPipeline _gBufferPipeline,
                   VkPipeline _brdfPipeline, VkExtent2D _swapChainExtent,
                   const Buffer &_vertexBuffer, const Buffer &_instanceBuffer,
                   const Buffer &_indexBuffer, const Frame &_frame,
                   uint32_t _numIndices, uint32_t _numInstances) {

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

  vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          gStandardPipelineLayout.Handle, 2, 1,
                          &_frame.MaterialDescriptorSets[gCurrentMaterial], 0,
                          nullptr);

  VkRenderPassBeginInfo deferredRenderPassInfo = {};
  deferredRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  deferredRenderPassInfo.renderPass = _deferredRenderPass;
  deferredRenderPassInfo.framebuffer = _deferredFramebuffer;
  deferredRenderPassInfo.renderArea.offset = {0, 0};
  deferredRenderPassInfo.renderArea.extent = _swapChainExtent;

  VkClearValue
      deferredRenderPassClearValues[(EnumCount<GBufferAttachmentType> + 2)] =
          {}; // +1 for depth, 1 for lighting pass
  deferredRenderPassClearValues[0].color = {0, 0, 0, 1};  // Lighting pass
  deferredRenderPassClearValues[1].depthStencil = {0, 0}; // Depth
  deferredRenderPassClearValues[2].color = {0, 0, 0, 1};  // Position
  deferredRenderPassClearValues[3].color = {0, 0, 0, 1};  // Normal
  deferredRenderPassClearValues[4].color = {0, 0, 0, 1};  // Albedo
  deferredRenderPassClearValues[5].color = {0, 0, 0, 1};  // MRAH
  deferredRenderPassClearValues[6].color = {0, 0, 0, 1};  // Material index
  deferredRenderPassInfo.clearValueCount =
      (uint32_t)std::size(deferredRenderPassClearValues);
  deferredRenderPassInfo.pClearValues = deferredRenderPassClearValues;

  VkRenderPassBeginInfo renderPassInfo = {};
  vkCmdBeginRenderPass(cmdBuffer, &deferredRenderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  if (gSceneRenderPassType == RenderPassType::Deferred)
  // Draw scene objects
  {
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _gBufferPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &_vertexBuffer.Handle, &offset);
    vkCmdBindVertexBuffers(cmdBuffer, 1, 1, &_instanceBuffer.Handle, &offset);
    vkCmdBindIndexBuffer(cmdBuffer, _indexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuffer, _numIndices, _numInstances, 0, 0, 0);

    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &gPlaneVertexBuffer.Handle,
                           &offset);
    vkCmdBindVertexBuffers(cmdBuffer, 1, 1, &gPlaneInstanceBuffer.Handle,
                           &offset);
    vkCmdBindIndexBuffer(cmdBuffer, gPlaneIndexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuffer, gNumPlaneIndices, 1, 0, 0, 0);
  }

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
  if (gSceneRenderPassType == RenderPassType::Deferred &&
      gBufferVisualize.CurrentOption ==
          GBufferVisualizingOption::RenderedScene) {

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _brdfPipeline);

    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
  }

  vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);

  if (gSceneRenderPassType == RenderPassType::Forward) {
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _forwardPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &_vertexBuffer.Handle, &offset);
    vkCmdBindVertexBuffers(cmdBuffer, 1, 1, &_instanceBuffer.Handle, &offset);
    vkCmdBindIndexBuffer(cmdBuffer, _indexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuffer, _numIndices, _numInstances, 0, 0, 0);

    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &gPlaneVertexBuffer.Handle,
                           &offset);
    vkCmdBindVertexBuffers(cmdBuffer, 1, 1, &gPlaneInstanceBuffer.Handle,
                           &offset);
    vkCmdBindIndexBuffer(cmdBuffer, gPlaneIndexBuffer.Handle, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuffer, gNumPlaneIndices, 1, 0, 0, 0);
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
  commonSceneResources.Window = window;

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

  Assimp::Importer importer;
  const aiScene *shaderBallScene =
      importer.ReadFile(createAbsolutePath("ShaderBall.fbx"),
                        aiProcess_Triangulate | aiProcess_CalcTangentSpace);
  const aiMesh *shaderBallMesh = shaderBallScene->mMeshes[0];
  std::vector<Vertex> shaderBallVertices;
  shaderBallVertices.reserve(shaderBallMesh->mNumFaces * 3);
  for (unsigned int i = 0; i < shaderBallMesh->mNumFaces; ++i) {
    const aiFace &face = shaderBallMesh->mFaces[i];
    BB_ASSERT(face.mNumIndices == 3);

    for (int j = 0; j < 3; ++j) {
      unsigned int vi = face.mIndices[j];

      Vertex v = {};
      v.Pos = aiVector3DToFloat3(shaderBallMesh->mVertices[vi]);
      v.UV = aiVector3DToFloat2(shaderBallMesh->mTextureCoords[0][vi]);
      v.Normal = aiVector3DToFloat3(shaderBallMesh->mNormals[vi]);
      v.Tangent = aiVector3DToFloat3(shaderBallMesh->mTangents[vi]);
      shaderBallVertices.push_back(v);
    }
  }

  std::vector<uint32_t> shaderBallIndices;
  shaderBallIndices.resize(shaderBallVertices.size());
  std::iota(shaderBallIndices.begin(), shaderBallIndices.end(), 0);

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

  // Create a descriptor pool corresponding to the standard pipeline layout
  VkDescriptorPool descriptorPool;
  {
    std::unordered_map<VkDescriptorType, uint32_t> numDescriptorsTable;

    for (DescriptorFrequency frequency : AllEnums<DescriptorFrequency>) {
      const DescriptorSetLayout &descriptorSetLayout =
          gStandardPipelineLayout.DescriptorSetLayouts[frequency];

      uint32_t numDescriptorSets = numFrames;
      if (frequency == DescriptorFrequency::PerMaterial) {
        numDescriptorSets *= materialSet.Materials.size();
      }

      for (auto [type, numDescriptors] :
           descriptorSetLayout.NumDescriptorsTable) {
        numDescriptorsTable[type] += numDescriptors * numDescriptorSets;
      }
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(numDescriptorsTable.size());
    for (auto [type, num] : numDescriptorsTable) {
      VkDescriptorPoolSize poolSize = {};
      poolSize.type = type;
      poolSize.descriptorCount = num;
      poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = (uint32_t)poolSizes.size();
    descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();

    const int numPerFrameSets = 1;
    const int numPerViewSets = 1;
    const int numPerMaterialSets = materialSet.Materials.size();
    const int numPerDrawSets = 1;

    descriptorPoolCreateInfo.maxSets =
        numFrames * (numPerFrameSets + numPerViewSets + numPerMaterialSets +
                     numPerDrawSets);
    BB_VK_ASSERT(vkCreateDescriptorPool(
        renderer.Device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));
  }

  RenderPass deferredRenderPass;

  VkPipeline forwardPipeline;
  VkPipeline gBufferPipeline;
  VkPipeline brdfPipeline;

  std::vector<VkFramebuffer> deferredFramebuffers;
  EnumArray<GBufferAttachmentType, Image> deferredAttachmentImages;

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
  brdfPipelineParams.DepthStencil.DepthTestEnable = true;
  brdfPipelineParams.DepthStencil.DepthWriteEnable = true;
  brdfPipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;

  SwapChain swapChain;

  commonSceneResources.SwapChain = &swapChain;
  commonSceneResources.RenderPass = &deferredRenderPass;
  commonSceneResources.Framebuffers = &deferredFramebuffers;
  commonSceneResources.GBufferAttachmentImages = &deferredAttachmentImages;

  auto initReloadableResources = [&] {
    swapChain = createSwapChain(renderer, width, height, nullptr);

    gBufferVisualize.ViewportExtent.width = width;
    gBufferVisualize.ViewportExtent.height = height;

    deferredRenderPass = createDeferredRenderPass(renderer, swapChain);

    VkImageCreateInfo gBufferAttachmentImageCreateInfo = {};
    gBufferAttachmentImageCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    gBufferAttachmentImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    gBufferAttachmentImageCreateInfo.extent.width = swapChain.Extent.width;
    gBufferAttachmentImageCreateInfo.extent.height = swapChain.Extent.height;
    gBufferAttachmentImageCreateInfo.extent.depth = 1;
    gBufferAttachmentImageCreateInfo.mipLevels = 1;
    gBufferAttachmentImageCreateInfo.arrayLayers = 1;
    // gBufferAttachmentImageCreateInfo.format = swapChain.ColorFormat;
    gBufferAttachmentImageCreateInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    gBufferAttachmentImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    gBufferAttachmentImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    gBufferAttachmentImageCreateInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    gBufferAttachmentImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    gBufferAttachmentImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    gBufferAttachmentImageCreateInfo.flags = 0;

    for (GBufferAttachmentType type : AllEnums<GBufferAttachmentType>) {
      Image &currentImage = deferredAttachmentImages[type];
      BB_VK_ASSERT(vkCreateImage(renderer.Device,
                                 &gBufferAttachmentImageCreateInfo, nullptr,
                                 &currentImage.Handle));

      VkMemoryRequirements memRequirements;
      vkGetImageMemoryRequirements(renderer.Device, currentImage.Handle,
                                   &memRequirements);

      VkMemoryAllocateInfo textureImageMemoryAllocateInfo = {};
      textureImageMemoryAllocateInfo.sType =
          VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      textureImageMemoryAllocateInfo.allocationSize = memRequirements.size;
      textureImageMemoryAllocateInfo.memoryTypeIndex =
          findMemoryType(renderer, memRequirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      BB_VK_ASSERT(vkAllocateMemory(renderer.Device,
                                    &textureImageMemoryAllocateInfo, nullptr,
                                    &currentImage.Memory));

      BB_VK_ASSERT(vkBindImageMemory(renderer.Device, currentImage.Handle,
                                     currentImage.Memory, 0));

      VkImageViewCreateInfo imageViewCreateInfo = {};
      imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      imageViewCreateInfo.image = currentImage.Handle;
      imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      imageViewCreateInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      imageViewCreateInfo.subresourceRange.aspectMask =
          VK_IMAGE_ASPECT_COLOR_BIT;
      imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
      imageViewCreateInfo.subresourceRange.levelCount = 1;
      imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
      imageViewCreateInfo.subresourceRange.layerCount = 1;
      BB_VK_ASSERT(vkCreateImageView(renderer.Device, &imageViewCreateInfo,
                                     nullptr, &currentImage.View));
    }

    deferredFramebuffers.resize(numFrames);
    // Create deferred framebuffer
    for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
      VkFramebufferCreateInfo fbCreateInfo = {};
      fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbCreateInfo.renderPass = deferredRenderPass.Handle;
      VkImageView attachments[] = {
          swapChain.ColorImageViews[i],
          swapChain.DepthImageView,
          deferredAttachmentImages[GBufferAttachmentType::Position].View,
          deferredAttachmentImages[GBufferAttachmentType::Normal].View,
          deferredAttachmentImages[GBufferAttachmentType::Albedo].View,
          deferredAttachmentImages[GBufferAttachmentType::MRAH].View,
          deferredAttachmentImages[GBufferAttachmentType::MaterialIndex].View,
      };
      fbCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
      fbCreateInfo.pAttachments = attachments;
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
    forwardPipeline = createPipeline(renderer, forwardPipelineParams, 1, 2);
    gBufferPipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                             (float)swapChain.Extent.height};
    gBufferPipelineParams.Viewport.ScissorExtent = {
        (int)swapChain.Extent.width, (int)swapChain.Extent.height};
    gBufferPipelineParams.RenderPass = deferredRenderPass.Handle;
    gBufferPipeline = createPipeline(renderer, gBufferPipelineParams,
                                     EnumCount<GBufferAttachmentType>, 0);
    brdfPipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                          (float)swapChain.Extent.height};
    brdfPipelineParams.Viewport.ScissorExtent = {(int)swapChain.Extent.width,
                                                 (int)swapChain.Extent.height};
    brdfPipelineParams.RenderPass = deferredRenderPass.Handle;
    brdfPipeline = createPipeline(renderer, brdfPipelineParams, 1, 1);

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

      pipelineParams.DepthStencil.DepthTestEnable = true;
      pipelineParams.DepthStencil.DepthWriteEnable = true;
      pipelineParams.PipelineLayout = forwardPipelineParams.PipelineLayout;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gGizmo.Pipeline = createPipeline(renderer, pipelineParams, 1, 2);
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

      pipelineParams.PipelineLayout = gStandardPipelineLayout.Handle;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gLightSources.Pipeline = createPipeline(renderer, pipelineParams, 1, 2);
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

      pipelineParams.DepthStencil.DepthTestEnable = false;
      pipelineParams.DepthStencil.DepthWriteEnable = false;
      pipelineParams.PipelineLayout = brdfPipelineParams.PipelineLayout;
      pipelineParams.RenderPass = deferredRenderPass.Handle;

      gBufferVisualize.Pipeline =
          createPipeline(renderer, pipelineParams, 1, 2);
    }

    commonSceneResources.GBufferPipeline = gBufferPipeline;
    commonSceneResources.DeferredBrdfPipeline = brdfPipeline;
    commonSceneResources.ForwardBrdfPipeline = forwardPipeline;
  };

  auto cleanupReloadableResources = [&] {
    vkDestroyPipeline(renderer.Device, gLightSources.Pipeline, nullptr);
    gLightSources.Pipeline = VK_NULL_HANDLE;

    vkDestroyPipeline(renderer.Device, gGizmo.Pipeline, nullptr);
    gGizmo.Pipeline = VK_NULL_HANDLE;

    vkDestroyPipeline(renderer.Device, gBufferVisualize.Pipeline, nullptr);
    gBufferVisualize.Pipeline = VK_NULL_HANDLE;

    for (Image &image : deferredAttachmentImages) {
      destroyImage(renderer, image);
    }

    for (VkFramebuffer fb : deferredFramebuffers) {
      vkDestroyFramebuffer(renderer.Device, fb, nullptr);
    }
    deferredFramebuffers.clear();

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

  std::vector<Vertex> planeVertices;
  std::vector<uint32_t> planeIndices;
  generatePlaneMesh(planeVertices, planeIndices);
  gPlaneVertexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      sizeBytes32(planeVertices), planeVertices.data());
  gPlaneIndexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      sizeBytes32(planeIndices), planeIndices.data());
  gNumPlaneIndices = planeIndices.size();

  InstanceBlock planeInstanceData = {};
  planeInstanceData.ModelMat =
      Mat4::translate({0, -10, 0}) * Mat4::scale({100.f, 100.f, 100.f});
  planeInstanceData.InvModelMat = planeInstanceData.ModelMat.inverse();

  gPlaneInstanceBuffer = createBuffer(renderer, sizeof(planeInstanceData),
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  {
    void *dst;
    vkMapMemory(renderer.Device, gPlaneInstanceBuffer.Memory, 0,
                gPlaneInstanceBuffer.Size, 0, &dst);
    memcpy(dst, &planeInstanceData, sizeof(planeInstanceData));
    vkUnmapMemory(renderer.Device, gPlaneInstanceBuffer.Memory);
  }

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

  Buffer shaderBallVertexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      sizeBytes32(shaderBallVertices), shaderBallVertices.data());

  Buffer shaderBallIndexBuffer = createDeviceLocalBufferFromMemory(
      renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      sizeBytes32(shaderBallIndices), shaderBallIndices.data());

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
    frames.push_back(createFrame(renderer, gStandardPipelineLayout,
                                 descriptorPool, materialSet,
                                 deferredAttachmentImages));
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

  commonSceneResources.Frames = &frames;
  commonSceneResources.FrameSyncObjects = &frameSyncObjects;

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
      writeGBuffersToDescriptorSet(renderer, frame, deferredAttachmentImages);
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

  std::vector<InstanceBlock> instanceData(numInstances);

  Buffer instanceBuffer = createBuffer(renderer, sizeBytes32(instanceData),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  FreeLookCamera cam = {};
  Input input = {};

  GUIInitParams guiParams = {};
  guiParams.MaterialImageSampler =
      gStandardPipelineLayout.ImmutableSamplers[SamplerType::Nearest];
  guiParams.MaterialSet = &materialSet;
  GUI gui = createGUI(guiParams);
  gui.SelectedMaterialIndex = gCurrentMaterial;

  gShaderBallScene = new ShaderBallScene(&commonSceneResources);

  bool running = true;

  Time lastTime = getCurrentTime();

  const Image &testImage =
      materialSet.Materials.front().Maps[PBRMapType::Albedo];
  ImTextureID textureId = ImGui_ImplVulkan_AddTexture(
      gStandardPipelineLayout.ImmutableSamplers[SamplerType::Nearest],
      testImage.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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

    updateGUI(gui);
    gCurrentMaterial = gui.SelectedMaterialIndex;

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

    static float angle = -90;
    // angle += 30.f * dt;
    if (angle > 360) {
      angle -= 360;
    }

    FrameUniformBlock frameUniformBlock = {};
    frameUniformBlock.NumLights = 3;
    gLightSources.NumLights = frameUniformBlock.NumLights;
    Light *light = &frameUniformBlock.Lights[0];
    light->Dir = {-1, -1, 0};
    light->Type = LightType::Directional;
    light->Color = {0.2347f, 0.2131f, 0.2079f};
    light->Intensity = 10.f;
    ++light;
    light->Pos = {0, 2, 0};
    light->Type = LightType::Point;
    light->Color = {1, 0, 0};
    light->Intensity = 200;
    ++light;
    light->Pos = {4, 2, 0};
    light->Dir = {0, -1, 0};
    light->Type = LightType::Point;
    light->Color = {0, 1, 0};
    light->Intensity = 200;
    light->InnerCutOff = degToRad(30);
    light->OuterCutOff = degToRad(25);

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

    static int selectedInstanceIndex = -1;

    if (ImGui::Begin("Objects")) {
      for (size_t i = 0; i < instanceData.size(); ++i) {
        std::string label = fmt::format("Shader Ball {}", i);
        if (ImGui::Selectable(label.c_str(), i == selectedInstanceIndex)) {
          selectedInstanceIndex = i;
        }
      }
    }
    ImGui::End();

    if (ImGui::Begin("Render Setting")) {
      EnumArray<RenderPassType, const char *> renderPassOptionLabels = {
          "Forward", "Deferred"};
      if (ImGui::BeginCombo("Scene Render Pass",
                            renderPassOptionLabels[gSceneRenderPassType])) {

        for (auto renderPassType : AllEnums<RenderPassType>) {
          if (ImGui::Selectable(renderPassOptionLabels[renderPassType],
                                gSceneRenderPassType == renderPassType)) {
            gSceneRenderPassType = renderPassType;
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (gSceneRenderPassType == RenderPassType::Deferred) {
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

    for (int i = 0; i < instanceData.size(); i++) {
      instanceData[i].ModelMat =
          Mat4::translate({(float)(i * 2), -1, 2}) * Mat4::rotateY(angle)
#if 1
          * Mat4::rotateX(-90) * Mat4::scale({0.01f, 0.01f, 0.01f})
#endif
          ;
      instanceData[i].InvModelMat = instanceData[i].ModelMat.inverse();
    }

    {
      void *data;
      vkMapMemory(renderer.Device, instanceBuffer.Memory, 0,
                  instanceBuffer.Size, 0, &data);
      memcpy(data, instanceData.data(), instanceBuffer.Size);
      vkUnmapMemory(renderer.Device, instanceBuffer.Memory);
    }

    vkResetCommandPool(renderer.Device, currentFrame.CmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    ImGui::Render();
    recordCommand(deferredRenderPass.Handle, currentDeferredFramebuffer,
                  forwardPipeline, gBufferPipeline, brdfPipeline,
                  swapChain.Extent, shaderBallVertexBuffer, instanceBuffer,
                  shaderBallIndexBuffer, currentFrame, shaderBallIndices.size(),
                  numInstances);

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

  delete gShaderBallScene;
  gShaderBallScene = nullptr;

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

  vkDestroyDescriptorPool(renderer.Device, descriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, imguiDescriptorPool, nullptr);

  destroyBuffer(renderer, gLightSources.InstanceBuffer);
  destroyBuffer(renderer, gLightSources.IndexBuffer);
  destroyBuffer(renderer, gLightSources.VertexBuffer);
  destroyBuffer(renderer, gGizmo.IndexBuffer);
  destroyBuffer(renderer, gGizmo.VertexBuffer);
  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, instanceBuffer);
  destroyBuffer(renderer, gPlaneIndexBuffer);
  destroyBuffer(renderer, gPlaneVertexBuffer);
  destroyBuffer(renderer, gPlaneInstanceBuffer);

  cleanupReloadableResources();

  destroyStandardPipelineLayout(renderer, gStandardPipelineLayout);

  destroyPBRMaterialSet(renderer, materialSet);

  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);

  destroyShader(renderer, gLightSources.VertShader);
  destroyShader(renderer, gLightSources.FragShader);
  destroyShader(renderer, gGizmo.VertShader);
  destroyShader(renderer, gGizmo.FragShader);
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