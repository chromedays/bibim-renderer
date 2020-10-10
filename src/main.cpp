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

void recordCommand(VkCommandBuffer _cmdBuffer, VkRenderPass _forwardRenderPass, VkRenderPass _deferredRenderPass,
                   VkFramebuffer _forwardFramebuffer, VkFramebuffer _deferredFramebuffer,
                   VkPipeline _forwardPipeline, VkPipeline _gBufferPipeline, VkPipeline _brdfPipeline,
                   VkExtent2D _swapChainExtent, 
                   const Buffer &_vertexBuffer, const Buffer &_instanceBuffer,
                   const Buffer &_indexBuffer,
                   const StandardPipelineLayout &_forwardPipelineLayout,
                   const StandardPipelineLayout &_gBufferPipelineLayout,
                   const StandardPipelineLayout &_brdfPipelineLayout,
                   const Frame &_forwardFrame, const Frame& _gBufferFrame, const Frame& _brdfFrame, const std::vector<uint32_t> &_indices,
                   uint32_t _numInstances) {

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = 0;
  cmdBeginInfo.pInheritanceInfo = nullptr;

  BB_VK_ASSERT(vkBeginCommandBuffer(_cmdBuffer, &cmdBeginInfo));

  VkRenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _deferredRenderPass;
  renderPassInfo.framebuffer = _deferredFramebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapChainExtent;

  VkClearValue clearValues[((int)GBufferAttachmentType::COUNT +2)] = {}; // +1 for depth, 1 for lighting pass
  clearValues[0].color = {0, 0, 0, 1};  // Position
  clearValues[1].color = {0, 0, 0, 1};  // Normal
  clearValues[2].color = {0, 0, 0, 1};  // Albedo
  clearValues[3].color = {0, 0, 0, 1};  // MRAH
  clearValues[4].color = {0, 0, 0, 1};  // Material index
  clearValues[5].depthStencil = {0, 0}; // Depth
  clearValues[6].color = {0, 0, 0, 1};  // Lighting pass
  renderPassInfo.clearValueCount = (uint32_t)std::size(clearValues);
  renderPassInfo.pClearValues = clearValues;

  vkCmdBeginRenderPass(_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _gBufferPipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(_cmdBuffer, 0, 1, &_vertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(_cmdBuffer, 1, 1, &_instanceBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(_cmdBuffer, _indexBuffer.Handle, 0,
                        VK_INDEX_TYPE_UINT32);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _gBufferPipelineLayout.Handle, 0, 1,
                          &_gBufferFrame.FrameDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _gBufferPipelineLayout.Handle, 1, 1,
                          &_gBufferFrame.ViewDescriptorSet, 0, nullptr);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _gBufferPipelineLayout.Handle, 2, 1,
                          &_gBufferFrame.MaterialDescriptorSets[0], 0, nullptr);

  vkCmdDrawIndexed(_cmdBuffer, (uint32_t)_indices.size(), _numInstances, 0, 0,
                    0);


  vkCmdNextSubpass(_cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _brdfPipeline);

  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _brdfPipelineLayout.Handle, 0, 1,
                          &_brdfFrame.FrameDescriptorSet, 0, nullptr);


  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        _brdfPipelineLayout.Handle, 1, 1,
                        &_brdfFrame.ViewDescriptorSet, 0, nullptr);


  vkCmdDraw(_cmdBuffer, 3, 1, 0, 0);



  vkCmdEndRenderPass(_cmdBuffer);



  renderPassInfo.renderPass = _forwardRenderPass;
  renderPassInfo.framebuffer = _forwardFramebuffer;

  vkCmdBeginRenderPass(_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);


  vkCmdEndRenderPass(_cmdBuffer);
  // TODO: Draw forward pass on the rendered scene from deferred rendering.
  // TODO: Add imgui.
  // TODO: gGizmo.
  // vkCmdBeginRenderPass(_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  // ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);
  // vkCmdEndRenderPass(_cmdBuffer);
  
  BB_VK_ASSERT(vkEndCommandBuffer(_cmdBuffer));
}

void initReloadableResources(
    const Renderer &_renderer, uint32_t _width, uint32_t _height,
    const SwapChain *_oldSwapChain, 
    const PipelineParams &_forwardPipelineParams,
    const PipelineParams &_gBufferPipelineParams, 
    const PipelineParams &_brdfPipelineParams,
    SwapChain *_outSwapChain,
    RenderPass *_outForwardRenderPass, 
    RenderPass *_outDeferredRenderPass,
    VkPipeline *_outForwardPipeline,
    VkPipeline *_outGbufferPipeline,
    VkPipeline *_outBrdfPipeline,
    std::vector<VkFramebuffer> *_outForwardSwapChainFramebuffers,
    std::vector<VkFramebuffer> *_outDeferredSwapChainFramebuffers,
    EnumArray<GBufferAttachmentType, Image> *_outDeferredAttachmentImages) {

  SwapChain swapChain =
      createSwapChain(_renderer, _width, _height, _oldSwapChain);

  RenderPass forwardRenderPass = createForwardRenderPass(_renderer, swapChain);
  RenderPass deferredRenderPass = createDeferredRenderPass(_renderer, swapChain);

  std::vector<VkFramebuffer> forwardFramebuffers(swapChain.NumColorImages);
  std::vector<VkFramebuffer> deferredFramebuffers(swapChain.NumColorImages);




  // Create forward framebuffer
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = forwardRenderPass.Handle;
    VkImageView attachments[] = {swapChain.ColorImageViews[i],
                                 swapChain.DepthImageView};
    fbCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
    fbCreateInfo.pAttachments = attachments;
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_renderer.Device, &fbCreateInfo, nullptr,
                                     &forwardFramebuffers[i]));
  }

  // Cretae gBuffer framebuffer attachment
  //std::vector<Image> gBufferAttachmentImages((int)GBufferAttachmentType::COUNT);

  EnumArray<GBufferAttachmentType, Image> deferredAttachmentImages;

  VkImageCreateInfo gBufferAttachmentImageCreateInfo = {};
  gBufferAttachmentImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  gBufferAttachmentImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  gBufferAttachmentImageCreateInfo.extent.width = swapChain.Extent.width;
  gBufferAttachmentImageCreateInfo.extent.height = swapChain.Extent.height;
  gBufferAttachmentImageCreateInfo.extent.depth = 1;
  gBufferAttachmentImageCreateInfo.mipLevels = 1;
  gBufferAttachmentImageCreateInfo.arrayLayers = 1;
  gBufferAttachmentImageCreateInfo.format = swapChain.ColorFormat;
  gBufferAttachmentImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  gBufferAttachmentImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  gBufferAttachmentImageCreateInfo.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  gBufferAttachmentImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  gBufferAttachmentImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  gBufferAttachmentImageCreateInfo.flags = 0;

  for (GBufferAttachmentType type = GBufferAttachmentType::Position;
       type < GBufferAttachmentType::COUNT;
       type = (GBufferAttachmentType)((int)type +1)) 
       {
         Image& currentImage = deferredAttachmentImages[type]; 
    BB_VK_ASSERT(vkCreateImage(_renderer.Device,
                               &gBufferAttachmentImageCreateInfo, nullptr,
                               &currentImage.Handle));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(
        _renderer.Device, currentImage.Handle, &memRequirements);

    VkMemoryAllocateInfo textureImageMemoryAllocateInfo = {};
    textureImageMemoryAllocateInfo.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    textureImageMemoryAllocateInfo.allocationSize = memRequirements.size;
    textureImageMemoryAllocateInfo.memoryTypeIndex =
        findMemoryType(_renderer, memRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    BB_VK_ASSERT(vkAllocateMemory(_renderer.Device,
                                  &textureImageMemoryAllocateInfo, nullptr,
                                  &currentImage.Memory));

    BB_VK_ASSERT(vkBindImageMemory(_renderer.Device,
                                   currentImage.Handle,
                                   currentImage.Memory, 0));

    VkImageViewCreateInfo imageViewCreateInfo = {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = currentImage.Handle;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = swapChain.ColorFormat;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    BB_VK_ASSERT(vkCreateImageView(_renderer.Device, &imageViewCreateInfo,
                                   nullptr, &currentImage.View));
  }

  // Create deferred framebuffer
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = deferredRenderPass.Handle;
    VkImageView attachments[] = {deferredAttachmentImages[GBufferAttachmentType::Position].View,
                                 deferredAttachmentImages[GBufferAttachmentType::Normal].View,
                                 deferredAttachmentImages[GBufferAttachmentType::Albedo].View,
                                 deferredAttachmentImages[GBufferAttachmentType::MRAH].View,
                                 deferredAttachmentImages[GBufferAttachmentType::MaterialIndex].View,
                                 swapChain.DepthImageView,
                                 swapChain.ColorImageViews[i]};
    fbCreateInfo.attachmentCount = (uint32_t)std::size(attachments);
    fbCreateInfo.pAttachments = attachments;
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_renderer.Device, &fbCreateInfo, nullptr,
                                     &deferredFramebuffers[i]));
  }
  
  
  // PBR Forward Pipeline
  PipelineParams pipelineParams = _forwardPipelineParams;
  {
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
    pipelineParams.PipelineLayout = _forwardPipelineParams.PipelineLayout;
    pipelineParams.RenderPass = forwardRenderPass.Handle;
    *_outForwardPipeline = createPipeline(_renderer, pipelineParams);
  }

  // Gbuffer pipeline
  {
    pipelineParams.Shaders = _gBufferPipelineParams.Shaders;
    pipelineParams.PipelineLayout = _gBufferPipelineParams.PipelineLayout; // TODO: actually same with forward
    pipelineParams.RenderPass = deferredRenderPass.Handle;
    *_outGbufferPipeline = createPipeline(_renderer, pipelineParams, (uint32_t)GBufferAttachmentType::COUNT);
  }

  // BRDF pipeline
  {
    pipelineParams.VertexInput.NumBindings = 0;
    pipelineParams.VertexInput.Bindings = nullptr;
    
    pipelineParams.VertexInput.NumAttributes = 0;
    pipelineParams.VertexInput.Attributes = nullptr;
    

    pipelineParams.Shaders = _brdfPipelineParams.Shaders;
    pipelineParams.PipelineLayout = _brdfPipelineParams.PipelineLayout; // TODO: actually same with forward
    pipelineParams.RenderPass = deferredRenderPass.Handle;
    *_outBrdfPipeline = createPipeline(_renderer, pipelineParams, 1, 1);
  }


  *_outSwapChain = std::move(swapChain);
  *_outForwardRenderPass = forwardRenderPass;
  *_outDeferredRenderPass = deferredRenderPass;

  //*_outForwardGraphicsPipeline = createPipeline(_renderer, _forwardPipelineParams);
  //*_outDeferredGraphicsPipeline = createPipeline(_renderer, _deferredPipelineParams, (int)GBufferAttachmentType::COUNT);

  *_outForwardSwapChainFramebuffers = std::move(forwardFramebuffers);
  *_outDeferredSwapChainFramebuffers = std::move(deferredFramebuffers);

  *_outDeferredAttachmentImages = std::move(deferredAttachmentImages);

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
    pipelineParams.PipelineLayout = _forwardPipelineParams.PipelineLayout;
    pipelineParams.RenderPass = forwardRenderPass.Handle;

    gGizmo.Pipeline = createPipeline(_renderer, pipelineParams);
  }
}

void cleanupReloadableResources(
    const Renderer& _renderer,
    SwapChain& _swapChain,
    RenderPass& _forwardRenderPass, 
    RenderPass& _deferredRenderPass,
    VkPipeline& _forwardGraphicsPipeline,
    VkPipeline& _gBufferGraphicsPipeline,
    VkPipeline& _brdfGraphicsPipeline,
    std::vector<VkFramebuffer>& _forwardSwapChainFramebuffers,
    std::vector<VkFramebuffer>& _deferredSwapChainFramebuffers,
    EnumArray<GBufferAttachmentType, Image>& _deferredAttachmentImages) {

  vkDestroyPipeline(_renderer.Device, gGizmo.Pipeline, nullptr);
  gGizmo.Pipeline = VK_NULL_HANDLE;

  for (VkFramebuffer fb : _forwardSwapChainFramebuffers) {
    vkDestroyFramebuffer(_renderer.Device, fb, nullptr);
  }

  for (VkFramebuffer fb : _deferredSwapChainFramebuffers) {
    vkDestroyFramebuffer(_renderer.Device, fb, nullptr);
  }

  _forwardSwapChainFramebuffers.clear();
  _deferredSwapChainFramebuffers.clear();

  vkDestroyPipeline(_renderer.Device, _forwardGraphicsPipeline, nullptr);
  vkDestroyPipeline(_renderer.Device, _gBufferGraphicsPipeline, nullptr);
  vkDestroyPipeline(_renderer.Device, _brdfGraphicsPipeline, nullptr);

  _forwardGraphicsPipeline = VK_NULL_HANDLE;
  _gBufferGraphicsPipeline = VK_NULL_HANDLE;
  _brdfGraphicsPipeline = VK_NULL_HANDLE;
  
  vkDestroyRenderPass(_renderer.Device, _forwardRenderPass.Handle, nullptr);
  vkDestroyRenderPass(_renderer.Device, _deferredRenderPass.Handle, nullptr);
  
  _forwardRenderPass.Handle = VK_NULL_HANDLE;
  _deferredRenderPass.Handle = VK_NULL_HANDLE;
  
  destroySwapChain(_renderer, _swapChain);
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void onWindowResize(SDL_Window *_window, const Renderer &_renderer, 
    const PipelineParams &_forwardPipelineParams,
    const PipelineParams &_gBufferPipelineParams, 
    const PipelineParams &_brdfPipelineParams, 
    SwapChain &_swapChain,
    RenderPass &_forwardRenderPass, 
    RenderPass &_deferredRenderPass,
    VkPipeline &_forwardGraphicsPipeline,
    VkPipeline &_gBufferGraphicsPipeline,
    VkPipeline &_brdfGraphicsPipeline,
    std::vector<VkFramebuffer> &_forwardSwapChainFramebuffers,
    std::vector<VkFramebuffer> &_deferredSwapChainFramebuffers,
    EnumArray<GBufferAttachmentType, Image> &_deferredAttachmentImages) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(
      _renderer.Device); // Ensure that device finished using swap chain.

  cleanupReloadableResources(_renderer, _swapChain, _forwardRenderPass, _deferredRenderPass, _forwardGraphicsPipeline, _gBufferGraphicsPipeline,
                            _brdfGraphicsPipeline, _forwardSwapChainFramebuffers, _deferredSwapChainFramebuffers, _deferredAttachmentImages);
                             


  initReloadableResources(_renderer, width, height, nullptr, _forwardPipelineParams, _gBufferPipelineParams, _brdfPipelineParams,
                          &_swapChain, &_forwardRenderPass, &_deferredRenderPass, &_forwardGraphicsPipeline, &_gBufferGraphicsPipeline, &_brdfGraphicsPipeline,
                          &_forwardSwapChainFramebuffers, &_deferredSwapChainFramebuffers, &_deferredAttachmentImages);
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

  Shader gBufferVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "gbuffer.vert.spv")));
  Shader gBufferFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "gbuffer.frag.spv")));

  Shader brdfVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.vert.spv")));
  Shader brdfFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.frag.spv")));

  Shader forwardBrdfVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "forwardBrdf.vert.spv")));
  Shader forwardBrdfFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "forwardBrdf.frag.spv")));

  gGizmo.VertShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.vert.spv")));
  gGizmo.FragShader = createShaderFromFile(
      renderer,
      createAbsolutePath(joinPaths(shaderRootPath, "gizmo.frag.spv")));

  VkCommandPoolCreateInfo transientCmdPoolCreateInfo = {};
  transientCmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  transientCmdPoolCreateInfo.queueFamilyIndex = renderer.QueueFamilyIndex;
  transientCmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool transientCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(renderer.Device, &transientCmdPoolCreateInfo, nullptr,
                                   &transientCmdPool));

  std::vector<PBRMaterial> pbrMaterials;
  pbrMaterials.push_back(createPBRMaterialFromFiles(
      renderer, transientCmdPool,
      createAbsolutePath("pbr/hardwood_brown_planks")));

  StandardPipelineLayout standardPipelineLayout =
      createStandardPipelineLayout(renderer);
  StandardPipelineLayout gBufferPipelineLayout = 
      createGbufferPipelineLayout(renderer);
  StandardPipelineLayout brdfPipelineLayout = 
      createBrdfPipelineLayout(renderer);

  // Create a descriptor pool corresponding to the standard pipeline layout
  VkDescriptorPool descriptorPool;
  {
    std::unordered_map<VkDescriptorType, uint32_t> numDescriptorsTable;

    for (DescriptorFrequency frequency = DescriptorFrequency::PerFrame;
         frequency < DescriptorFrequency::COUNT;
         frequency = (DescriptorFrequency)((int)frequency + 1)) {

      const DescriptorSetLayout &forwardDescriptorSetLayout =
          standardPipelineLayout.DescriptorSetLayouts[frequency];

      const DescriptorSetLayout &gBufferDescriptorSetLayout =
          gBufferPipelineLayout.DescriptorSetLayouts[frequency];

      uint32_t numDescriptorSets = numFrames;
      if (frequency == DescriptorFrequency::PerMaterial) {
        numDescriptorSets *= pbrMaterials.size();
      }

      for (auto [type, numDescriptors] :
           forwardDescriptorSetLayout.NumDescriptorsTable) {
        numDescriptorsTable[type] += numDescriptors * numDescriptorSets;
      }

      for (auto [type, numDescriptors] :
           gBufferDescriptorSetLayout.NumDescriptorsTable) {
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
        (standardPipelineLayout.DescriptorSetLayouts.size() + gBufferPipelineLayout.DescriptorSetLayouts.size()) * numFrames); 
    BB_VK_ASSERT(vkCreateDescriptorPool(
        renderer.Device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));
  }

  // TODO: seperate it.
  VkDescriptorPool brdfDescriptorPool = {};
  {
    std::unordered_map<VkDescriptorType, uint32_t> numDescriptorsTable;

    // TODO: use 
    for (DescriptorFrequency frequency = DescriptorFrequency::PerFrame;
         frequency < DescriptorFrequency::COUNT;
         frequency = (DescriptorFrequency)((int)frequency + 1)) {
      const DescriptorSetLayout &descriptorSetLayout =
          standardPipelineLayout.DescriptorSetLayouts[frequency];

      uint32_t numDescriptorSets = numFrames;


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
        renderer.Device, &descriptorPoolCreateInfo, nullptr, &brdfDescriptorPool));
  }

  SwapChain swapChain;

  RenderPass forwardRenderPass;
  RenderPass deferredRenderPass;

  VkPipeline forwardPipeline;
  VkPipeline gBufferPipeline;
  VkPipeline brdfPipeline;

  std::vector<VkFramebuffer> forwardFramebuffers;
  std::vector<VkFramebuffer> defrerredFramebuffers;
  EnumArray<GBufferAttachmentType, Image> deferredAttachmentImages;


  const Shader* forwardShaders[] = {&forwardBrdfVertShader, &forwardBrdfFragShader};
  PipelineParams forwardPipelineParam;
  forwardPipelineParam.Shaders = forwardShaders;
  forwardPipelineParam.NumShaders = std::size(forwardShaders);
  forwardPipelineParam.PipelineLayout = standardPipelineLayout.Handle;


  const Shader* gBufferShaders[] = {&gBufferVertShader, &gBufferFragShader};
  PipelineParams gBufferPipelineParam;
  gBufferPipelineParam.Shaders = gBufferShaders;
  gBufferPipelineParam.NumShaders = std::size(gBufferShaders);
  gBufferPipelineParam.PipelineLayout = gBufferPipelineLayout.Handle;


  const Shader* brdfShaders[] = {&brdfVertShader, &brdfFragShader};
  PipelineParams brdfPipelineParam;
  brdfPipelineParam.Shaders = brdfShaders;
  brdfPipelineParam.NumShaders = std::size(brdfShaders);
  brdfPipelineParam.PipelineLayout = brdfPipelineLayout.Handle;




  initReloadableResources(renderer, width, height, nullptr, forwardPipelineParam, gBufferPipelineParam, brdfPipelineParam,
                          &swapChain, &forwardRenderPass, &deferredRenderPass, &forwardPipeline, &gBufferPipeline, &brdfPipeline,
                           &forwardFramebuffers, &defrerredFramebuffers, &deferredAttachmentImages);

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


  VkCommandPool drawCmdPool;
  VkCommandBuffer drawCmdBuffer;

  VkCommandPoolCreateInfo drawCmdPoolCreateInfo = {};
  drawCmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  drawCmdPoolCreateInfo.queueFamilyIndex = renderer.QueueFamilyIndex;
  drawCmdPoolCreateInfo.flags = 0;

  BB_VK_ASSERT(vkCreateCommandPool(renderer.Device, &drawCmdPoolCreateInfo,
                                   nullptr, &drawCmdPool));

  VkCommandBufferAllocateInfo drawCmdBufferAllocInfo = {};
  drawCmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  drawCmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  drawCmdBufferAllocInfo.commandBufferCount = 1;
  drawCmdBufferAllocInfo.commandPool = drawCmdPool;
  BB_VK_ASSERT(vkAllocateCommandBuffers(renderer.Device, &drawCmdBufferAllocInfo,
                                        &drawCmdBuffer));




  std::vector<Frame> forwardFrames;
  for (int i = 0; i < numFrames; ++i) {
    forwardFrames.push_back(createFrame(renderer, standardPipelineLayout,
                                 descriptorPool, pbrMaterials));
  }

  std::vector<Frame> gBufferFrames;
  for (int i = 0; i < numFrames; ++i) {
    gBufferFrames.push_back(createFrame(renderer, gBufferPipelineLayout,
                                 descriptorPool, pbrMaterials));
  }

  std::vector<Frame> brdfFrames;
  for (int i = 0; i < numFrames; ++i) {
    brdfFrames.push_back(createBrdfFrame(renderer, brdfPipelineLayout,
                                 brdfDescriptorPool, deferredAttachmentImages));
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
  ImGui_ImplVulkan_Init(&initInfo, forwardRenderPass.Handle);

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

    // TODO
    Frame &currentForwardFrame = forwardFrames[currentFrameIndex];
    Frame &currentGbufferFrame = gBufferFrames[currentFrameIndex];
    Frame &currentBrdfFrame = brdfFrames[currentFrameIndex];


    VkResult acquireNextImageResult =
        vkAcquireNextImageKHR(renderer.Device, swapChain.Handle, UINT64_MAX,
                              currentGbufferFrame.ImagePresentedSemaphore,
                              VK_NULL_HANDLE, &currentSwapChainImageIndex);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          renderer.PhysicalDevice, renderer.Surface,
          &renderer.SwapChainSupportDetails.Capabilities);

      onWindowResize(window, renderer, forwardPipelineParam, gBufferPipelineParam, brdfPipelineParam, swapChain,
                    forwardRenderPass, deferredRenderPass, forwardPipeline, gBufferPipeline, brdfPipeline,
                    forwardFramebuffers, defrerredFramebuffers, deferredAttachmentImages);

      continue;
    }

    VkFramebuffer currentForwardFramebuffer =
        forwardFramebuffers[currentSwapChainImageIndex];

    VkFramebuffer currentDeferredFramebuffer =
        defrerredFramebuffers[currentSwapChainImageIndex];

        

    // vkWaitForFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence,
    //                 VK_TRUE, UINT64_MAX);
    // vkResetFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence);

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)forwardFrames.size();

    static float angle = -90;
    //angle += 30.f * dt;
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
      vkMapMemory(renderer.Device, currentBrdfFrame.FrameUniformBuffer.Memory, 0,
                  sizeof(FrameUniformBlock), 0, &data);
      memcpy(data, &frameUniformBlock, sizeof(FrameUniformBlock));
      vkUnmapMemory(renderer.Device, currentBrdfFrame.FrameUniformBuffer.Memory);
    }

    ViewUniformBlock viewUniformBlock = {};
    viewUniformBlock.ViewMat = cam.getViewMatrix();
    viewUniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    viewUniformBlock.ViewPos = cam.Pos;

    {
      void *data;
      vkMapMemory(renderer.Device, currentGbufferFrame.ViewUniformBuffer.Memory, 0,
                  sizeof(ViewUniformBlock), 0, &data);
      memcpy(data, &viewUniformBlock, sizeof(ViewUniformBlock));
      vkUnmapMemory(renderer.Device, currentGbufferFrame.ViewUniformBuffer.Memory);


      vkMapMemory(renderer.Device, currentBrdfFrame.ViewUniformBuffer.Memory, 0,
                  sizeof(ViewUniformBlock), 0, &data);
      memcpy(data, &viewUniformBlock, sizeof(ViewUniformBlock));
      vkUnmapMemory(renderer.Device, currentBrdfFrame.ViewUniformBuffer.Memory);
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
    
    vkResetCommandPool(renderer.Device, drawCmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    ImGui::Render();
    recordCommand(drawCmdBuffer, forwardRenderPass.Handle, deferredRenderPass.Handle,
                  currentForwardFramebuffer, currentDeferredFramebuffer, 
                  forwardPipeline, gBufferPipeline, brdfPipeline, swapChain.Extent,
                  shaderBallVertexBuffer, instanceBuffer,
                  shaderBallIndexBuffer, standardPipelineLayout, gBufferPipelineLayout, brdfPipelineLayout,
                  currentForwardFrame, currentGbufferFrame, currentBrdfFrame,
                  shaderBallIndices, numInstances);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &currentGbufferFrame.ImagePresentedSemaphore;
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &currentGbufferFrame.RenderFinishedSemaphore;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &drawCmdBuffer;

    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo,
                               currentGbufferFrame.FrameAvailableFence));

    vkWaitForFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentGbufferFrame.RenderFinishedSemaphore;
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

      onWindowResize(window, renderer, forwardPipelineParam, gBufferPipelineParam, brdfPipelineParam, swapChain,
                    forwardRenderPass, deferredRenderPass, forwardPipeline, gBufferPipeline, brdfPipeline,
                    forwardFramebuffers, defrerredFramebuffers, deferredAttachmentImages);
    }

  }

  vkDeviceWaitIdle(renderer.Device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  vkDestroyCommandPool(renderer.Device, drawCmdPool, nullptr);

  for (Frame &frame : forwardFrames) {
    destroyFrame(renderer, frame);
  }

  for (Frame &frame : gBufferFrames) {
    destroyFrame(renderer, frame);
  }

  for (Frame &frame : brdfFrames) {
    destroyFrame(renderer, frame);
  }

  vkDestroyDescriptorPool(renderer.Device, descriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, imguiDescriptorPool, nullptr);

  destroyBuffer(renderer, gGizmo.IndexBuffer);
  destroyBuffer(renderer, gGizmo.VertexBuffer);
  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, instanceBuffer);

  cleanupReloadableResources(renderer, swapChain, forwardRenderPass, deferredRenderPass, forwardPipeline, gBufferPipeline, brdfPipeline, 
                              forwardFramebuffers, defrerredFramebuffers, deferredAttachmentImages);

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