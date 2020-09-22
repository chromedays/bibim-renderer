#include "util.h"
#include "gui.h"
#include "enum_array.h"
#include "vector_math.h"
#include "camera.h"
#include "input.h"
#include "render.h"
#include "type_conversion.h"
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
#include "external/stb_image.h"
#include "vulkan/vulkan_core.h"
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

struct UniformBlock {
  Mat4 ModelMat;
  Mat4 InvModelMat;
  Mat4 ViewMat;
  Mat4 ProjMat;
  alignas(16) Float3 ViewPos;
  alignas(16) Float3 Albedo;
  float Metallic;
  float Roughness;
  float AO;
  int VisualizeOption;
};

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
                  VkImageView _imageView, VkSampler _sampler) {
  Frame result = {};

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = _renderer.QueueFamilyIndex;
  cmdPoolCreateInfo.flags = 0;

  BB_VK_ASSERT(vkCreateCommandPool(_renderer.Device, &cmdPoolCreateInfo,
                                   nullptr, &result.CmdPool));

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = 1;
  cmdBufferAllocInfo.commandPool = result.CmdPool;
  BB_VK_ASSERT(vkAllocateCommandBuffers(_renderer.Device, &cmdBufferAllocInfo,
                                        &result.CmdBuffer));

  result.UniformBuffer = createBuffer(_renderer, sizeof(UniformBlock),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  result.DescriptorPool = _descriptorPool;

  VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = _descriptorPool;
  descriptorSetAllocInfo.descriptorSetCount = 1;
  descriptorSetAllocInfo.pSetLayouts = &_descriptorSetLayout;
  BB_VK_ASSERT(vkAllocateDescriptorSets(
      _renderer.Device, &descriptorSetAllocInfo, &result.DescriptorSet));

  VkDescriptorBufferInfo descriptorBufferInfo = {};
  descriptorBufferInfo.buffer = result.UniformBuffer.Handle;
  descriptorBufferInfo.offset = 0;
  descriptorBufferInfo.range = sizeof(UniformBlock);

  VkDescriptorImageInfo descriptorImageInfo = {};
  descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  descriptorImageInfo.imageView = _imageView;
  descriptorImageInfo.sampler = _sampler;

  VkWriteDescriptorSet descriptorWrites[2] = {};
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = result.DescriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &descriptorBufferInfo;
  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = result.DescriptorSet;
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &descriptorImageInfo;
  vkUpdateDescriptorSets(_renderer.Device,
                         (uint32_t)std::size(descriptorWrites),
                         descriptorWrites, 0, nullptr);

  VkSemaphoreCreateInfo semaphoreCreateInfo = {};
  semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  BB_VK_ASSERT(vkCreateSemaphore(_renderer.Device, &semaphoreCreateInfo,
                                 nullptr, &result.RenderFinishedSemaphore));
  BB_VK_ASSERT(vkCreateSemaphore(_renderer.Device, &semaphoreCreateInfo,
                                 nullptr, &result.ImagePresentedSemaphore));

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  BB_VK_ASSERT(vkCreateFence(_renderer.Device, &fenceCreateInfo, nullptr,
                             &result.FrameAvailableFence));

  return result;
}

void destroyFrame(const Renderer &_renderer, Frame &_frame) {
  vkDestroySemaphore(_renderer.Device, _frame.ImagePresentedSemaphore, nullptr);
  vkDestroySemaphore(_renderer.Device, _frame.RenderFinishedSemaphore, nullptr);
  vkDestroyFence(_renderer.Device, _frame.FrameAvailableFence, nullptr);
  destroyBuffer(_renderer, _frame.UniformBuffer);
  vkDestroyCommandPool(_renderer.Device, _frame.CmdPool, nullptr);
  _frame = {};
}

void recordCommand(VkCommandBuffer _cmdBuffer, VkRenderPass _renderPass,
                   VkFramebuffer _swapChainFramebuffer,
                   VkExtent2D _swapChainExtent, VkPipeline _graphicsPipeline,
                   const Buffer &_vertexBuffer, const Buffer &_indexBuffer,
                   VkImage textureImage, VkPipelineLayout _pipelineLayout,
                   VkDescriptorSet _descriptorSet,
                   const std::vector<uint32_t> &_indices) {

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
  vkCmdBindIndexBuffer(_cmdBuffer, _indexBuffer.Handle, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
  vkCmdDrawIndexed(_cmdBuffer, (uint32_t)_indices.size(), 1, 0, 0, 0);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);

  vkCmdEndRenderPass(_cmdBuffer);

  BB_VK_ASSERT(vkEndCommandBuffer(_cmdBuffer));
}

void initReloadableResources(
    const Renderer &_renderer, uint32_t _width, uint32_t _height,
    const SwapChain *_oldSwapChain, const Shader &_shader,
    VkPipelineLayout _pipelineLayout, SwapChain *_outSwapChain,
    VkRenderPass *_outRenderPass, VkPipeline *_outGraphicsPipeline,
    std::vector<VkFramebuffer> *_outSwapChainFramebuffers) {

  SwapChain swapChain =
      createSwapChain(_renderer, _width, _height, _oldSwapChain);

  VkPipelineShaderStageCreateInfo shaderStages[2] = {};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = _shader.Vert;
  shaderStages[0].pName = "main";
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = _shader.Frag;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription bindingDesc = Vertex::getBindingDesc();
  std::array<VkVertexInputAttributeDescription, 3> attributeDescs =
      Vertex::getAttributeDescs();
  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount = 1;
  vertexInputState.pVertexBindingDescriptions = &bindingDesc;
  vertexInputState.vertexAttributeDescriptionCount =
      (uint32_t)attributeDescs.size();
  vertexInputState.pVertexAttributeDescriptions = attributeDescs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
  inputAssemblyState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssemblyState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.width = (float)swapChain.Extent.width;
  viewport.height = (float)swapChain.Extent.height;
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapChain.Extent;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizationState = {};
  rasterizationState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.depthClampEnable = VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.lineWidth = 1.f;
  rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.depthBiasConstantFactor = 0.f;
  rasterizationState.depthBiasClamp = 0.f;
  rasterizationState.depthBiasSlopeFactor = 0.f;

  VkPipelineMultisampleStateCreateInfo multisampleState = {};
  multisampleState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.sampleShadingEnable = VK_FALSE;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampleState.minSampleShading = 1.f;
  multisampleState.pSampleMask = nullptr;
  multisampleState.alphaToCoverageEnable = VK_FALSE;
  multisampleState.alphaToOneEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
  depthStencilState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  depthStencilState.depthBoundsTestEnable = VK_FALSE;
  depthStencilState.minDepthBounds = 1.f;
  depthStencilState.maxDepthBounds = 0.f;
  depthStencilState.stencilTestEnable = VK_FALSE;
  // depthStencilState.front = {};
  // depthStencilState.back = {};

  VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
  colorBlendAttachmentState.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachmentState.blendEnable = VK_FALSE;
  colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlendState = {};
  colorBlendState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.logicOpEnable = VK_FALSE;
  colorBlendState.logicOp = VK_LOGIC_OP_COPY;
  colorBlendState.attachmentCount = 1;
  colorBlendState.pAttachments = &colorBlendAttachmentState;
  colorBlendState.blendConstants[0] = 0.f;
  colorBlendState.blendConstants[1] = 0.f;
  colorBlendState.blendConstants[2] = 0.f;
  colorBlendState.blendConstants[3] = 0.f;

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

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.stageCount = 2;
  pipelineCreateInfo.pStages = shaderStages;
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;
  pipelineCreateInfo.pDynamicState = nullptr;
  pipelineCreateInfo.layout = _pipelineLayout;
  pipelineCreateInfo.renderPass = renderPass;
  pipelineCreateInfo.subpass = 0;
  pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCreateInfo.basePipelineIndex = -1;

  VkPipeline graphicsPipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_renderer.Device, VK_NULL_HANDLE, 1,
                                         &pipelineCreateInfo, nullptr,
                                         &graphicsPipeline));

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

  *_outSwapChain = std::move(swapChain);
  *_outSwapChainFramebuffers = std::move(swapChainFramebuffers);
  *_outRenderPass = renderPass;
  *_outGraphicsPipeline = graphicsPipeline;
}

void cleanupReloadableResources(
    const Renderer &_renderer, SwapChain &_swapChain, VkRenderPass &_renderPass,
    VkPipeline &_graphicsPipeline,
    std::vector<VkFramebuffer> &_swapChainFramebuffers) {
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
                    const Shader &_shader, VkPipelineLayout _pipelineLayout,
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

  initReloadableResources(_renderer, width, height, nullptr, _shader,
                          _pipelineLayout, &_swapChain, &_renderPass,
                          &_graphicsPipeline, &_swapChainFramebuffers);
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

  std::string resourceRootPath = SDL_GetBasePath();
  resourceRootPath += "\\..\\..\\resources\\";

  Assimp::Importer importer;
  const aiScene *shaderBallScene =
      importer.ReadFile(resourceRootPath + "ShaderBall.fbx",
                        aiProcess_Triangulate | aiProcess_FlipUVs);
  const aiMesh *shaderBallMesh = shaderBallScene->mMeshes[0];
  std::vector<Vertex> shaderBallVertices;
  shaderBallVertices.reserve(shaderBallMesh->mNumFaces * 3);
  for (unsigned int i = 0; i < shaderBallMesh->mNumFaces; ++i) {
    const aiFace &face = shaderBallMesh->mFaces[i];
    BB_ASSERT(face.mNumIndices == 3);

    for (int j = 0; j < 3; ++j) {
      Vertex v = {};
      v.Pos = aiVector3DToFloat3(shaderBallMesh->mVertices[face.mIndices[j]]);
      std::swap(v.Pos.Y, v.Pos.Z);
      v.Pos.Z *= -1.f;
      v.UV.X = shaderBallMesh->mTextureCoords[0][face.mIndices[j]].x;
      v.UV.Y = shaderBallMesh->mTextureCoords[0][face.mIndices[j]].y;
      v.Normal = aiVector3DToFloat3(shaderBallMesh->mNormals[face.mIndices[j]]);
      std::swap(v.Normal.Y, v.Normal.Z);
      v.Normal.Z *= -1.f;
      shaderBallVertices.push_back(v);
    }
  }

  std::vector<uint32_t> shaderBallIndices;
  shaderBallIndices.resize(shaderBallVertices.size());
  std::iota(shaderBallIndices.begin(), shaderBallIndices.end(), 0);

  Renderer renderer = createRenderer(window);

  std::string shaderRootPath = resourceRootPath + "..\\src\\shaders\\";

  Shader testShader =
      createShaderFromFile(renderer, shaderRootPath + "test.vert.spv",
                           shaderRootPath + "test.frag.spv");
  Shader brdfShader =
      createShaderFromFile(renderer, shaderRootPath + "brdf.vert.spv",
                           shaderRootPath + "brdf.frag.spv");

  VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[2] = {};
  descriptorSetLayoutBindings[0].binding = 0;
  descriptorSetLayoutBindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorSetLayoutBindings[0].descriptorCount = 1;
  descriptorSetLayoutBindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
  descriptorSetLayoutBindings[1].binding = 1;
  descriptorSetLayoutBindings[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorSetLayoutBindings[1].descriptorCount = 1;
  descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount =
      (uint32_t)std::size(descriptorSetLayoutBindings);
  descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;

  VkDescriptorSetLayout descriptorSetLayout;
  BB_VK_ASSERT(vkCreateDescriptorSetLayout(renderer.Device,
                                           &descriptorSetLayoutCreateInfo,
                                           nullptr, &descriptorSetLayout));

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCreateInfo.setLayoutCount = 1;
  pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout pipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(
      renderer.Device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = renderer.QueueFamilyIndex;
  cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool transientCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(renderer.Device, &cmdPoolCreateInfo, nullptr,
                                   &transientCmdPool));

  SwapChain swapChain;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  initReloadableResources(renderer, width, height, nullptr, brdfShader,
                          pipelineLayout, &swapChain, &renderPass,
                          &graphicsPipeline, &swapChainFramebuffers);

  // clang-format off
  std::vector<Vertex> quadVertices = {
      {{-0.5f, -0.5f, 0}, {0, 0}},
      {{0.5f, -0.5f, 0},  {1, 0}},
      {{0.5f, 0.5f, 0},  {1, 1}},
      {{-0.5f, 0.5f, 0}, {0, 1}},

      {{-0.5f, -0.5f, -0.5f}, {0, 0}},
      {{0.5f, -0.5f, -0.5f},  {1, 0}},
      {{0.5f, 0.5f, -0.5f}, {1, 1}},
      {{-0.5f, 0.5f, -0.5f},  {0, 1}}};

  std::vector<uint32_t> quadIndices = {
    4, 5, 6, 6, 7, 4,
    0, 1, 2, 2, 3, 0,
  };
  // clang-format on

  Buffer quadVertexBuffer = createBuffer(renderer, size_bytes32(quadVertices),
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  Buffer quadIndexBuffer = createBuffer(renderer, size_bytes32(quadIndices),
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  {
    Buffer vertexStagingBuffer =
        createStagingBuffer(renderer, quadVertexBuffer);

    Buffer indexStagingBuffer = createStagingBuffer(renderer, quadIndexBuffer);
    void *data;
    vkMapMemory(renderer.Device, vertexStagingBuffer.Memory, 0,
                vertexStagingBuffer.Size, 0, &data);
    memcpy(data, quadVertices.data(), vertexStagingBuffer.Size);
    vkUnmapMemory(renderer.Device, vertexStagingBuffer.Memory);
    vkMapMemory(renderer.Device, indexStagingBuffer.Memory, 0,
                indexStagingBuffer.Size, 0, &data);
    memcpy(data, quadIndices.data(), indexStagingBuffer.Size);
    vkUnmapMemory(renderer.Device, indexStagingBuffer.Memory);

    copyBuffer(renderer, transientCmdPool, quadVertexBuffer,
               vertexStagingBuffer, vertexStagingBuffer.Size);
    copyBuffer(renderer, transientCmdPool, quadIndexBuffer, indexStagingBuffer,
               indexStagingBuffer.Size);

    destroyBuffer(renderer, vertexStagingBuffer);
    destroyBuffer(renderer, indexStagingBuffer);
  }

  Buffer shaderBallVertexBuffer =
      createBuffer(renderer, size_bytes32(shaderBallVertices),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  {
    void *data;
    vkMapMemory(renderer.Device, shaderBallVertexBuffer.Memory, 0,
                shaderBallVertexBuffer.Size, 0, &data);
    memcpy(data, shaderBallVertices.data(), shaderBallVertexBuffer.Size);
    vkUnmapMemory(renderer.Device, shaderBallVertexBuffer.Memory);
  }
  Buffer shaderBallIndexBuffer =
      createBuffer(renderer, size_bytes32(shaderBallIndices),
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  {
    void *data;
    vkMapMemory(renderer.Device, shaderBallIndexBuffer.Memory, 0,
                shaderBallIndexBuffer.Size, 0, &data);
    memcpy(data, shaderBallIndices.data(), shaderBallIndexBuffer.Size);
    vkUnmapMemory(renderer.Device, shaderBallIndexBuffer.Memory);
  }

  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  {
    std::string textureFilePath = resourceRootPath + "\\uv_debug.png";
    Int2 textureDims = {};
    int numChannels;
    stbi_uc *pixels = stbi_load(textureFilePath.c_str(), &textureDims.X,
                                &textureDims.Y, &numChannels, STBI_rgb_alpha);
    BB_ASSERT(pixels);

    VkDeviceSize textureSize = textureDims.X * textureDims.Y * 4;

    Buffer textureStagingBuffer =
        createBuffer(renderer, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *data;
    vkMapMemory(renderer.Device, textureStagingBuffer.Memory, 0, textureSize, 0,
                &data);
    memcpy(data, pixels, textureSize);
    vkUnmapMemory(renderer.Device, textureStagingBuffer.Memory);
    stbi_image_free(pixels);

    VkImageCreateInfo textureImageCreateInfo = {};
    textureImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    textureImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    textureImageCreateInfo.extent.width = (uint32_t)textureDims.X;
    textureImageCreateInfo.extent.height = (uint32_t)textureDims.Y;
    textureImageCreateInfo.extent.depth = 1;
    textureImageCreateInfo.mipLevels = 1;
    textureImageCreateInfo.arrayLayers = 1;
    textureImageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    textureImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    textureImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureImageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    textureImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    textureImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    textureImageCreateInfo.flags = 0;

    BB_VK_ASSERT(vkCreateImage(renderer.Device, &textureImageCreateInfo,
                               nullptr, &textureImage));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(renderer.Device, textureImage,
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
                                  &textureImageMemory));

    BB_VK_ASSERT(vkBindImageMemory(renderer.Device, textureImage,
                                   textureImageMemory, 0));

    VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandPool = transientCmdPool;
    cmdBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    BB_VK_ASSERT(vkAllocateCommandBuffers(renderer.Device, &cmdBufferAllocInfo,
                                          &cmdBuffer));

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = int2ToExtent3D(textureDims);
    vkCmdCopyBufferToImage(cmdBuffer, textureStagingBuffer.Handle, textureImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo, VK_NULL_HANDLE));
    BB_VK_ASSERT(vkQueueWaitIdle(renderer.Queue));
    vkFreeCommandBuffers(renderer.Device, transientCmdPool, 1, &cmdBuffer);

    destroyBuffer(renderer, textureStagingBuffer);
  }

  VkImageView textureImageView;
  VkImageViewCreateInfo textureImageViewCreateInfo = {};
  textureImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  textureImageViewCreateInfo.image = textureImage;
  textureImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  textureImageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  textureImageViewCreateInfo.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  textureImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
  textureImageViewCreateInfo.subresourceRange.levelCount = 1;
  textureImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
  textureImageViewCreateInfo.subresourceRange.layerCount = 1;
  BB_VK_ASSERT(vkCreateImageView(renderer.Device, &textureImageViewCreateInfo,
                                 nullptr, &textureImageView));

  VkSampler textureSampler;
  VkSamplerCreateInfo textureSamplerCreateInfo = {};
  textureSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  textureSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
  textureSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  textureSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  textureSamplerCreateInfo.anisotropyEnable = VK_TRUE;
  textureSamplerCreateInfo.maxAnisotropy = 16.f;
  textureSamplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
  textureSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
  textureSamplerCreateInfo.compareEnable = VK_FALSE;
  textureSamplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  textureSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  textureSamplerCreateInfo.mipLodBias = 0.f;
  textureSamplerCreateInfo.minLod = 0.f;
  textureSamplerCreateInfo.maxLod = 0.f;
  BB_VK_ASSERT(vkCreateSampler(renderer.Device, &textureSamplerCreateInfo,
                               nullptr, &textureSampler));

  const int numFrames = 1;

  VkDescriptorPoolSize descriptorPoolSizes[2] = {};
  descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorPoolSizes[0].descriptorCount = numFrames;
  descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorPoolSizes[1].descriptorCount = numFrames;
  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
  descriptorPoolCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.poolSizeCount =
      (uint32_t)std::size(descriptorPoolSizes);
  descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes;
  descriptorPoolCreateInfo.maxSets = numFrames;

  VkDescriptorPool descriptorPool;
  BB_VK_ASSERT(vkCreateDescriptorPool(
      renderer.Device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

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
    frames.push_back(createFrame(renderer, descriptorPool, descriptorSetLayout,
                                 textureImageView, textureSampler));
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

#if 1
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

      onWindowResize(window, renderer, brdfShader, pipelineLayout, swapChain,
                     renderPass, graphicsPipeline, swapChainFramebuffers);
      continue;
    }

    VkFramebuffer currentSwapChainFramebuffer =
        swapChainFramebuffers[currentSwapChainImageIndex];

    vkWaitForFences(renderer.Device, 1, &currentFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentFrame.FrameAvailableFence);

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)frames.size();

    static float angle = 0;
    angle += 30.f * dt;
    if (angle > 360) {
      angle -= 360;
    }
    UniformBlock uniformBlock = {};
    uniformBlock.ModelMat = Mat4::translate({0, -1, 2}) * Mat4::rotateY(angle) *
                            Mat4::scale({0.01f, 0.01f, 0.01f});
    uniformBlock.InvModelMat = uniformBlock.ModelMat.inverse();
    uniformBlock.ViewMat = cam.getViewMatrix();
    uniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    uniformBlock.ViewPos = cam.Pos;
    static float albedo[3] = {1, 1, 1};
    ImGui::ColorPicker3("Albedo", albedo);
    uniformBlock.Albedo = {albedo[0], albedo[1], albedo[2]};
    static float metallic = 0;
    ImGui::SliderFloat("Metallic", &metallic, 0, 1);
    uniformBlock.Metallic = metallic;
    static float roughness = 0.5f;
    ImGui::SliderFloat("Roughness", &roughness, 0.1f, 1);
    uniformBlock.Roughness = roughness;
    static float ao = 1;
    ImGui::SliderFloat("AO", &ao, 0, 1);
    uniformBlock.AO = ao;

    static int visualizeOption = 0;
    int i = 0;
    for (auto option : {"N", "H", "D", "F", "G"}) {
      if (ImGui::Selectable(option, visualizeOption == i)) {
        visualizeOption = i;
      }
      ++i;
    }
    uniformBlock.VisualizeOption = visualizeOption;

    {
      void *data;
      vkMapMemory(renderer.Device, currentFrame.UniformBuffer.Memory, 0,
                  sizeof(UniformBlock), 0, &data);
      memcpy(data, &uniformBlock, sizeof(UniformBlock));
      vkUnmapMemory(renderer.Device, currentFrame.UniformBuffer.Memory);
    }

    vkResetCommandPool(renderer.Device, currentFrame.CmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    ImGui::Render();
    recordCommand(currentFrame.CmdBuffer, renderPass,
                  currentSwapChainFramebuffer, swapChain.Extent,
                  graphicsPipeline, shaderBallVertexBuffer,
                  shaderBallIndexBuffer, textureImage, pipelineLayout,
                  currentFrame.DescriptorSet, shaderBallIndices);

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

      onWindowResize(window, renderer, brdfShader, pipelineLayout, swapChain,
                     renderPass, graphicsPipeline, swapChainFramebuffers);
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
  ;
  vkDestroySampler(renderer.Device, textureSampler, nullptr);
  vkDestroyImageView(renderer.Device, textureImageView, nullptr);
  vkDestroyImage(renderer.Device, textureImage, nullptr);
  vkFreeMemory(renderer.Device, textureImageMemory, nullptr);
  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, quadIndexBuffer);
  destroyBuffer(renderer, quadVertexBuffer);

  cleanupReloadableResources(renderer, swapChain, renderPass, graphicsPipeline,
                             swapChainFramebuffers);

  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);

  vkDestroyPipelineLayout(renderer.Device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderer.Device, descriptorSetLayout, nullptr);
  destroyShader(renderer, brdfShader);
  destroyShader(renderer, testShader);
  destroyRenderer(renderer);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}