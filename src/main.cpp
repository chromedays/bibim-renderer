#include "util.h"
#include "gui.h"
#include "enum_array.h"
#include "vector_math.h"
#include "camera.h"
#include "input.h"
#include "render.h"
#include "type_conversion.h"
#include "resource.h"
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

struct {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;
  Buffer VertexBuffer;
  Buffer IndexBuffer;
  uint32_t numIndices;

  int viewportExtent = 100;
} gGizmo;

void recordCommand(VkCommandBuffer _cmdBuffer, VkRenderPass _renderPass,
                   VkFramebuffer _swapChainFramebuffer,
                   VkExtent2D _swapChainExtent, VkPipeline _graphicsPipeline,
                   const Buffer &_vertexBuffer, const Buffer &_instanceBuffer,
                   const Buffer &_indexBuffer,
                   const StandardPipelineLayout &_standardPipelineLayout,
                   const Frame &_frame, const std::vector<uint32_t> &_indices,
                   uint32_t _numInstances) {

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = 0;
  cmdBeginInfo.pInheritanceInfo = nullptr;

  BB_VK_ASSERT(vkBeginCommandBuffer(_cmdBuffer, &cmdBeginInfo));

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _renderPass;
  renderPassInfo.framebuffer = _swapChainFramebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapChainExtent;

  VkClearValue clearValues[2] = {0.f, 0.f, 0.f, 1.f};
  clearValues[0].color = {0, 0, 0, 1};
  clearValues[1].depthStencil = {0, 0};
  renderPassInfo.clearValueCount = (uint32_t)std::size(clearValues);
  renderPassInfo.pClearValues = clearValues;

  vkCmdBeginRenderPass(_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _graphicsPipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(_cmdBuffer, 0, 1, &_vertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(_cmdBuffer, 1, 1, &_instanceBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(_cmdBuffer, _indexBuffer.Handle, 0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _standardPipelineLayout.Handle, 0, 1,
                          &_frame.FrameDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _standardPipelineLayout.Handle, 1, 1,
                          &_frame.ViewDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _standardPipelineLayout.Handle, 2, 1,
                          &_frame.MaterialDescriptorSets[0], 0, nullptr);

  vkCmdDrawIndexed(_cmdBuffer, (uint32_t)_indices.size(), _numInstances, 0, 0,
                   0);

  VkClearAttachment clearDepth = {};
  clearDepth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  clearDepth.clearValue.depthStencil = {0.f, 0};
  VkClearRect clearDepthRegion = {};
  clearDepthRegion.rect.offset = {
      (int32_t)(_swapChainExtent.width - gGizmo.viewportExtent), 0};
  clearDepthRegion.rect.extent = {(uint32_t)gGizmo.viewportExtent,
                                  (uint32_t)gGizmo.viewportExtent};
  clearDepthRegion.layerCount = 1;
  clearDepthRegion.baseArrayLayer = 0;
  vkCmdClearAttachments(_cmdBuffer, 1, &clearDepth, 1, &clearDepthRegion);
  vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gGizmo.Pipeline);

  vkCmdBindVertexBuffers(_cmdBuffer, 0, 1, &gGizmo.VertexBuffer.Handle,
                         &offset);
  vkCmdBindIndexBuffer(_cmdBuffer, gGizmo.IndexBuffer.Handle, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(_cmdBuffer, gGizmo.numIndices, 1, 0, 0, 0);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);

  vkCmdEndRenderPass(_cmdBuffer);

  BB_VK_ASSERT(vkEndCommandBuffer(_cmdBuffer));
}

void initReloadableResources(
    const Renderer &_renderer, uint32_t _width, uint32_t _height,
    const SwapChain *_oldSwapChain, const Shader &_vertShader,
    const Shader &_fragShader,
    const StandardPipelineLayout &_standardPipelineLayout,
    SwapChain *_outSwapChain, VkRenderPass *_outRenderPass,
    VkPipeline *_outGraphicsPipeline,
    std::vector<VkFramebuffer> *_outSwapChainFramebuffers) {

  SwapChain swapChain =
      createSwapChain(_renderer, _width, _height, _oldSwapChain);

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = swapChain.ColorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment = {};
  depthAttachment.format = swapChain.DepthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef = {};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency subpassDependency = {};
  subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpassDependency.dstSubpass = 0;
  subpassDependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.srcAccessMask = 0;
  subpassDependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassCreateInfo = {};
  renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
  renderPassCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
  renderPassCreateInfo.pAttachments = attachments;
  renderPassCreateInfo.subpassCount = 1;
  renderPassCreateInfo.pSubpasses = &subpass;
  renderPassCreateInfo.dependencyCount = 1;
  renderPassCreateInfo.pDependencies = &subpassDependency;

  VkRenderPass renderPass;
  BB_VK_ASSERT(vkCreateRenderPass(_renderer.Device, &renderPassCreateInfo,
                                  nullptr, &renderPass));

  std::vector<VkFramebuffer> swapChainFramebuffers(swapChain.NumColorImages);
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = renderPass;
    VkImageView attachments[] = {swapChain.ColorImageViews[i],
                                 swapChain.DepthImageView};
    fbCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
    fbCreateInfo.pAttachments = attachments;
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_renderer.Device, &fbCreateInfo, nullptr,
                                     &swapChainFramebuffers[i]));
  }

  // PBR Forward Pipeline
  {
    const Shader *shaders[] = {&_vertShader, &_fragShader};
    PipelineParams pipelineParams = {};
    pipelineParams.Shaders = shaders;
    pipelineParams.NumShaders = std::size(shaders);

    auto bindingDescs = Vertex::getBindingDescs();
    auto attributeDescs = Vertex::getAttributeDescs();
    pipelineParams.VertexInput.Bindings = bindingDescs.data();
    pipelineParams.VertexInput.NumBindings = bindingDescs.size();
    pipelineParams.VertexInput.Attributes = attributeDescs.data();
    pipelineParams.VertexInput.NumAttributes = attributeDescs.size();

    pipelineParams.InputAssembly.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineParams.Viewport.Extent = {(float)swapChain.Extent.width,
                                      (float)swapChain.Extent.height};
    pipelineParams.Viewport.ScissorExtent = {(int)swapChain.Extent.width,
                                             (int)swapChain.Extent.height};

    pipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
    pipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;

    pipelineParams.DepthStencil.DepthTestEnable = true;
    pipelineParams.DepthStencil.DepthWriteEnable = true;
    pipelineParams.PipelineLayout = _standardPipelineLayout.Handle;
    pipelineParams.RenderPass = renderPass;
    *_outGraphicsPipeline = createPipeline(_renderer, pipelineParams);
  }

  *_outSwapChain = std::move(swapChain);
  *_outSwapChainFramebuffers = std::move(swapChainFramebuffers);
  *_outRenderPass = renderPass;

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

    pipelineParams.InputAssembly.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineParams.Viewport.Offset = {
        (float)swapChain.Extent.width - gGizmo.viewportExtent,
        0,
    };
    pipelineParams.Viewport.Extent = {(float)gGizmo.viewportExtent,
                                      (float)gGizmo.viewportExtent};
    pipelineParams.Viewport.ScissorOffset = {
        (int)pipelineParams.Viewport.Offset.X,
        (int)pipelineParams.Viewport.Offset.Y,
    };
    pipelineParams.Viewport.ScissorExtent = {(int)gGizmo.viewportExtent,
                                             (int)gGizmo.viewportExtent};

    pipelineParams.Rasterizer.PolygonMode = VK_POLYGON_MODE_FILL;
    pipelineParams.Rasterizer.CullMode = VK_CULL_MODE_BACK_BIT;

    pipelineParams.DepthStencil.DepthTestEnable = true;
    pipelineParams.DepthStencil.DepthWriteEnable = true;
    pipelineParams.PipelineLayout = _standardPipelineLayout.Handle;
    pipelineParams.RenderPass = renderPass;

    gGizmo.Pipeline = createPipeline(_renderer, pipelineParams);
  }
}

void cleanupReloadableResources(
    const Renderer &_renderer, SwapChain &_swapChain, VkRenderPass &_renderPass,
    VkPipeline &_graphicsPipeline,
    std::vector<VkFramebuffer> &_swapChainFramebuffers) {
  vkDestroyPipeline(_renderer.Device, gGizmo.Pipeline, nullptr);
  gGizmo.Pipeline = VK_NULL_HANDLE;

  for (VkFramebuffer fb : _swapChainFramebuffers) {
    vkDestroyFramebuffer(_renderer.Device, fb, nullptr);
  }
  _swapChainFramebuffers.clear();
  vkDestroyPipeline(_renderer.Device, _graphicsPipeline, nullptr);
  _graphicsPipeline = VK_NULL_HANDLE;
  vkDestroyRenderPass(_renderer.Device, _renderPass, nullptr);
  _renderPass = VK_NULL_HANDLE;
  destroySwapChain(_renderer, _swapChain);
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void onWindowResize(SDL_Window *_window, const Renderer &_renderer,
                    const Shader &_vertShader, const Shader &_fragShader,
                    const StandardPipelineLayout &_standardPipelineLayout,
                    SwapChain &_swapChain, VkRenderPass &_renderPass,
                    VkPipeline &_graphicsPipeline,
                    std::vector<VkFramebuffer> &_swapChainFramebuffers) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(
      _renderer.Device); // Ensure that device finished using swap chain.

  cleanupReloadableResources(_renderer, _swapChain, _renderPass,
                             _graphicsPipeline, _swapChainFramebuffers);

  initReloadableResources(_renderer, width, height, nullptr, _vertShader,
                          _fragShader, _standardPipelineLayout, &_swapChain,
                          &_renderPass, &_graphicsPipeline,
                          &_swapChainFramebuffers);
}

} // namespace bb

int main(int _argc, char **_argv) {
  using namespace bb;

  SetProcessDPIAware();
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  BB_VK_ASSERT(volkInitialize());

  SDL_Init(SDL_INIT_VIDEO);
  int width = 1280;
  int height = 720;
  SDL_Window *window = SDL_CreateWindow(
      "Bibim Renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  SDL_SysWMinfo sysinfo = {};
  SDL_VERSION(&sysinfo.version);
  SDL_GetWindowWMInfo(window, &sysinfo);

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
      // std::swap(v.Pos.Y, v.Pos.Z);
      // v.Pos.Z *= -1.f;
      v.UV = aiVector3DToFloat2(shaderBallMesh->mTextureCoords[0][vi]);
      // const aiVector3D &tangent = shaderBallMesh->mTangents[face]
      // v.Normal =
      v.Normal = aiVector3DToFloat3(shaderBallMesh->mNormals[vi]);
      // std::swap(v.Normal.Y, v.Normal.Z);
      // v.Normal.Z *= -1.f;
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

  Renderer renderer = createRenderer(window);

  std::string shaderRootPath = "../src/shaders";

  Shader brdfVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.vert.spv")));
  Shader brdfFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.frag.spv")));

  gGizmo.VertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.vert.spv")));
  gGizmo.FragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.frag.spv")));

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = renderer.QueueFamilyIndex;
  cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool transientCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(renderer.Device, &cmdPoolCreateInfo, nullptr,
                                   &transientCmdPool));

  std::vector<PBRMaterial> pbrMaterials;
  pbrMaterials.push_back(createPBRMaterialFromFiles(
      renderer, transientCmdPool,
      createAbsolutePath("pbr/hardwood_brown_planks")));

  StandardPipelineLayout standardPipelineLayout =
      createStandardPipelineLayout(renderer);

  // Create a descriptor pool corresponding to the standard pipeline layout
  VkDescriptorPool descriptorPool;
  {
    std::unordered_map<VkDescriptorType, uint32_t> numDescriptorsTable;

    for (DescriptorFrequency frequency = DescriptorFrequency::PerFrame;
         frequency < DescriptorFrequency::COUNT;
         frequency = (DescriptorFrequency)((int)frequency + 1)) {
      const DescriptorSetLayout &descriptorSetLayout =
          standardPipelineLayout.DescriptorSetLayouts[frequency];

      uint32_t numDescriptorSets = numFrames;
      if (frequency == DescriptorFrequency::PerMaterial) {
        numDescriptorSets *= pbrMaterials.size();
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
    descriptorPoolCreateInfo.maxSets = (uint32_t)(
        standardPipelineLayout.DescriptorSetLayouts.size() * numFrames);
    BB_VK_ASSERT(vkCreateDescriptorPool(
        renderer.Device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));
  }

  SwapChain swapChain;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  initReloadableResources(renderer, width, height, nullptr, brdfVertShader,
                          brdfFragShader, standardPipelineLayout, &swapChain,
                          &renderPass, &graphicsPipeline,
                          &swapChainFramebuffers);

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
  gGizmo.numIndices = gizmoIndices.size();

  // Imgui descriptor pool and descriptor sets
  VkDescriptorPool imguiDescriptorPool = {};
  {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 10},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
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
    frames.push_back(createFrame(renderer, standardPipelineLayout,
                                 descriptorPool, pbrMaterials));
  }

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
  ImGui_ImplVulkan_Init(&initInfo, renderPass);

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
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

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

#if 0
    ImGui::ShowDemoWindow();
#endif

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

    VkResult acquireNextImageResult =
        vkAcquireNextImageKHR(renderer.Device, swapChain.Handle, UINT64_MAX,
                              currentFrame.ImagePresentedSemaphore,
                              VK_NULL_HANDLE, &currentSwapChainImageIndex);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          renderer.PhysicalDevice, renderer.Surface,
          &renderer.SwapChainSupportDetails.Capabilities);

      onWindowResize(window, renderer, brdfVertShader, brdfFragShader,
                     standardPipelineLayout, swapChain, renderPass,
                     graphicsPipeline, swapChainFramebuffers);
      continue;
    }

    VkFramebuffer currentSwapChainFramebuffer =
        swapChainFramebuffers[currentSwapChainImageIndex];

    vkWaitForFences(renderer.Device, 1, &currentFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentFrame.FrameAvailableFence);

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)frames.size();

    static float angle = -90;
    angle += 30.f * dt;
    if (angle > 360) {
      angle -= 360;
    }

    FrameUniformBlock frameUniformBlock = {};
    frameUniformBlock.NumLights = 3;
    Light *light = &frameUniformBlock.Lights[0];
    light->Dir = {-1, -1, 0};
    light->Type = LightType::Directional;
    light->Color = {23.47f, 21.31f, 20.79f};
    light->Intensity = 0.1f;
    ++light;
    light->Pos = {0, 2, 0};
    light->Type = LightType::Point;
    light->Color = {1, 0, 0};
    light->Intensity = 200;
    ++light;
    light->Pos = {4, 2, 0};
    light->Dir = {0, -1, 0};
    light->Type = LightType::Spot;
    light->Color = {0, 1, 0};
    light->Intensity = 200;
    light->InnerCutOff = degToRad(30);
    light->OuterCutOff = degToRad(25);

    {
      void *data;
      vkMapMemory(renderer.Device, currentFrame.FrameUniformBuffer.Memory, 0,
                  sizeof(FrameUniformBlock), 0, &data);
      memcpy(data, &frameUniformBlock, sizeof(FrameUniformBlock));
      vkUnmapMemory(renderer.Device, currentFrame.FrameUniformBuffer.Memory);
    }

    ViewUniformBlock viewUniformBlock = {};
    viewUniformBlock.ViewMat = cam.getViewMatrix();
    viewUniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    viewUniformBlock.ViewPos = cam.Pos;

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

    if (ImGui::Begin("Material")) {
      if (selectedInstanceIndex >= 0) {
        InstanceBlock &currentInstance = instanceData[selectedInstanceIndex];

        guiMaterialPicker(fmt::format("Instance {}", selectedInstanceIndex),
                          currentInstance);
      }
    }
    ImGui::End();

    for (int i = 0; i < instanceData.size(); i++) {
      instanceData[i].ModelMat = Mat4::translate({(float)(i * 2), -1, 2}) *
                                 Mat4::rotateY(angle) * Mat4::rotateX(-90) *
                                 Mat4::scale({0.01f, 0.01f, 0.01f});
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
    recordCommand(currentFrame.CmdBuffer, renderPass,
                  currentSwapChainFramebuffer, swapChain.Extent,
                  graphicsPipeline, shaderBallVertexBuffer, instanceBuffer,
                  shaderBallIndexBuffer, standardPipelineLayout, currentFrame,
                  shaderBallIndices, numInstances);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &currentFrame.ImagePresentedSemaphore;
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &currentFrame.RenderFinishedSemaphore;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentFrame.CmdBuffer;

    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo,
                               currentFrame.FrameAvailableFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.RenderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain.Handle;
    presentInfo.pImageIndices = &currentSwapChainImageIndex;

    VkResult queuePresentResult =
        vkQueuePresentKHR(renderer.Queue, &presentInfo);
    if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        queuePresentResult == VK_SUBOPTIMAL_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          renderer.PhysicalDevice, renderer.Surface,
          &renderer.SwapChainSupportDetails.Capabilities);

      onWindowResize(window, renderer, brdfVertShader, brdfFragShader,
                     standardPipelineLayout, swapChain, renderPass,
                     graphicsPipeline, swapChainFramebuffers);
    }
  }

  vkDeviceWaitIdle(renderer.Device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  for (Frame &frame : frames) {
    destroyFrame(renderer, frame);
  }

  vkDestroyDescriptorPool(renderer.Device, descriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, imguiDescriptorPool, nullptr);

  destroyBuffer(renderer, gGizmo.IndexBuffer);
  destroyBuffer(renderer, gGizmo.VertexBuffer);
  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, instanceBuffer);

  cleanupReloadableResources(renderer, swapChain, renderPass, graphicsPipeline,
                             swapChainFramebuffers);

  destroyStandardPipelineLayout(renderer, standardPipelineLayout);

  for (PBRMaterial &material : pbrMaterials) {
    destroyPBRMaterial(renderer, material);
  }
  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);

  destroyShader(renderer, gGizmo.VertShader);
  destroyShader(renderer, gGizmo.FragShader);
  destroyShader(renderer, brdfVertShader);
  destroyShader(renderer, brdfFragShader);
  destroyRenderer(renderer);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}