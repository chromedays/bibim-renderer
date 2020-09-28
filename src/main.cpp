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

struct PBRMaterial {
  static constexpr int NumImages = 6;

  Image AlbedoMap;
  Image MetallicMap;
  Image RoughnessMap;
  Image AOMap;
  Image NormalMap;
  Image HeightMap;
};

PBRMaterial createPBRMaterialFromFiles(const Renderer &_renderer,
                                       VkCommandPool _transientCmdPool,
                                       const std::string &_rootPath) {
  // TODO(ilgwon): Convert _rootPath to absolute path if it's not already.
  PBRMaterial result = {};
  result.AlbedoMap = createImageFromFile(_renderer, _transientCmdPool,
                                         joinPaths(_rootPath, "albedo.png"));
  result.MetallicMap = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "metallic.png"));
  result.RoughnessMap = createImageFromFile(
      _renderer, _transientCmdPool, joinPaths(_rootPath, "roughness.png"));
  result.AOMap = createImageFromFile(_renderer, _transientCmdPool,
                                     joinPaths(_rootPath, "ao.png"));
  result.NormalMap = createImageFromFile(_renderer, _transientCmdPool,
                                         joinPaths(_rootPath, "normal.png"));
  result.HeightMap = createImageFromFile(_renderer, _transientCmdPool,
                                         joinPaths(_rootPath, "height.png"));
  return result;
}

void destroyPBRMaterial(const Renderer &_renderer, PBRMaterial &_material) {
  destroyImage(_renderer, _material.HeightMap);
  destroyImage(_renderer, _material.NormalMap);
  destroyImage(_renderer, _material.AOMap);
  destroyImage(_renderer, _material.RoughnessMap);
  destroyImage(_renderer, _material.MetallicMap);
  destroyImage(_renderer, _material.AlbedoMap);
}

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
                  const std::vector<PBRMaterial> &_pbrMaterials) {
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

  std::vector<VkDescriptorImageInfo>
      descriptorImageInfos[PBRMaterial::NumImages];
  for (std::vector<VkDescriptorImageInfo> &descriptorImageInfo :
       descriptorImageInfos) {
    descriptorImageInfo.reserve(_pbrMaterials.size());
  }

  for (const PBRMaterial &material : _pbrMaterials) {
    VkDescriptorImageInfo materialDesc = {};
    materialDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    materialDesc.imageView = material.AlbedoMap.View;
    descriptorImageInfos[0].push_back(materialDesc);
    materialDesc.imageView = material.MetallicMap.View;
    descriptorImageInfos[1].push_back(materialDesc);
    materialDesc.imageView = material.RoughnessMap.View;
    descriptorImageInfos[2].push_back(materialDesc);
    materialDesc.imageView = material.AOMap.View;
    descriptorImageInfos[3].push_back(materialDesc);
    materialDesc.imageView = material.NormalMap.View;
    descriptorImageInfos[4].push_back(materialDesc);
    materialDesc.imageView = material.HeightMap.View;
    descriptorImageInfos[5].push_back(materialDesc);
  }

  VkWriteDescriptorSet descriptorWrites[1 + PBRMaterial::NumImages] = {};
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = result.DescriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &descriptorBufferInfo;
  for (int i = 0; i < PBRMaterial::NumImages; ++i) {
    VkWriteDescriptorSet &write = descriptorWrites[i + 1];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = result.DescriptorSet;
    write.dstBinding = i + 2;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.descriptorCount = (uint32_t)_pbrMaterials.size();
    write.pImageInfo = descriptorImageInfos[i].data();
  }
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
                   const Buffer &_vertexBuffer, const Buffer &_instanceBuffer,
                   const Buffer &_indexBuffer, VkPipelineLayout _pipelineLayout,
                   VkDescriptorSet _descriptorSet,
                   const std::vector<uint32_t> &_indices,
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
                          _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);

  vkCmdDrawIndexed(_cmdBuffer, (uint32_t)_indices.size(), _numInstances, 0, 0,
                   0);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);

  vkCmdEndRenderPass(_cmdBuffer);

  BB_VK_ASSERT(vkEndCommandBuffer(_cmdBuffer));
}

void initReloadableResources(
    const Renderer &_renderer, uint32_t _width, uint32_t _height,
    const SwapChain *_oldSwapChain, const Shader &_vertShader,
    const Shader &_fragShader, VkPipelineLayout _pipelineLayout,
    SwapChain *_outSwapChain, VkRenderPass *_outRenderPass,
    VkPipeline *_outGraphicsPipeline,
    std::vector<VkFramebuffer> *_outSwapChainFramebuffers) {

  SwapChain swapChain =
      createSwapChain(_renderer, _width, _height, _oldSwapChain);

  VkPipelineShaderStageCreateInfo shaderStages[2] = {};
  shaderStages[0] = _vertShader.getStageInfo();
  shaderStages[1] = _fragShader.getStageInfo();

  std::array<VkVertexInputBindingDescription, 2> bindingDescs =
      Vertex::getBindingDescs();
  std::array<VkVertexInputAttributeDescription, 17> attributeDescs =
      Vertex::getAttributeDescs();
  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount =
      (uint32_t)bindingDescs.size();
  vertexInputState.pVertexBindingDescriptions = bindingDescs.data();
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
                    const Shader &_vertShader, const Shader &_fragShader,
                    VkPipelineLayout _pipelineLayout, SwapChain &_swapChain,
                    VkRenderPass &_renderPass, VkPipeline &_graphicsPipeline,
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
                          _fragShader, _pipelineLayout, &_swapChain,
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

  Renderer renderer = createRenderer(window);

  std::string shaderRootPath = "../src/shaders";

  Shader brdfVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.vert.spv")));
  Shader brdfFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "brdf.frag.spv")));

  VkSampler nearestSampler;
  VkSampler bilinearSampler;
  {
    VkSamplerCreateInfo samplerCreateInfo = {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.f;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCreateInfo.mipLodBias = 0.f;
    samplerCreateInfo.minLod = 0.f;
    samplerCreateInfo.maxLod = 0.f;

    BB_VK_ASSERT(vkCreateSampler(renderer.Device, &samplerCreateInfo, nullptr,
                                 &nearestSampler));

    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    BB_VK_ASSERT(vkCreateSampler(renderer.Device, &samplerCreateInfo, nullptr,
                                 &bilinearSampler));
  }

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

  VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[8] = {};

  // Uniform Block
  descriptorSetLayoutBindings[0].binding = 0;
  descriptorSetLayoutBindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorSetLayoutBindings[0].descriptorCount = 1;
  descriptorSetLayoutBindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  // Immutable Samplers (Nearest and Bilinear)
  VkSampler samplers[2] = {nearestSampler, bilinearSampler};
  descriptorSetLayoutBindings[1].binding = 1;
  descriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  descriptorSetLayoutBindings[1].descriptorCount =
      (uint32_t)std::size(samplers);
  descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptorSetLayoutBindings[1].pImmutableSamplers = samplers;

  // PBR Textures (Albedo, Metallic, Roughness, AO, Normal, Height)
  for (int i = 0; i < PBRMaterial::NumImages; ++i) {
    VkDescriptorSetLayoutBinding &binding = descriptorSetLayoutBindings[i + 2];
    binding.binding = i + 2;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = (uint32_t)pbrMaterials.size();
    binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }

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

  SwapChain swapChain;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  initReloadableResources(renderer, width, height, nullptr, brdfVertShader,
                          brdfFragShader, pipelineLayout, &swapChain,
                          &renderPass, &graphicsPipeline,
                          &swapChainFramebuffers);

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

  Buffer quadVertexBuffer = createBuffer(renderer, sizeBytes32(quadVertices),
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  Buffer quadIndexBuffer = createBuffer(renderer, sizeBytes32(quadIndices),
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
      createBuffer(renderer, sizeBytes32(shaderBallVertices),
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
      createBuffer(renderer, sizeBytes32(shaderBallIndices),
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

  VkDescriptorPoolSize descriptorPoolSizes[3] = {};
  descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorPoolSizes[0].descriptorCount = numFrames;
  descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptorPoolSizes[1].descriptorCount =
      numFrames * PBRMaterial::NumImages * pbrMaterials.size();
  descriptorPoolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  descriptorPoolSizes[2].descriptorCount = numFrames * 2;
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
                                 pbrMaterials));
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
                     pipelineLayout, swapChain, renderPass, graphicsPipeline,
                     swapChainFramebuffers);
      continue;
    }

    VkFramebuffer currentSwapChainFramebuffer =
        swapChainFramebuffers[currentSwapChainImageIndex];

    vkWaitForFences(renderer.Device, 1, &currentFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentFrame.FrameAvailableFence);

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)frames.size();

    static float angle = 0;
    // angle += 30.f * dt;
    if (angle > 360) {
      angle -= 360;
    }
    UniformBlock uniformBlock = {};
    uniformBlock.ViewMat = cam.getViewMatrix();
    uniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    uniformBlock.ViewPos = cam.Pos;
    uniformBlock.NumLights = 3;
    Light *light = &uniformBlock.lights[0];
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
      vkMapMemory(renderer.Device, currentFrame.UniformBuffer.Memory, 0,
                  sizeof(UniformBlock), 0, &data);
      memcpy(data, &uniformBlock, sizeof(UniformBlock));
      vkUnmapMemory(renderer.Device, currentFrame.UniformBuffer.Memory);
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
                                 Mat4::rotateY(angle) *
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
                  shaderBallIndexBuffer, pipelineLayout,
                  currentFrame.DescriptorSet, shaderBallIndices, numInstances);

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
                     pipelineLayout, swapChain, renderPass, graphicsPipeline,
                     swapChainFramebuffers);
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

  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, instanceBuffer);
  destroyBuffer(renderer, quadIndexBuffer);
  destroyBuffer(renderer, quadVertexBuffer);

  cleanupReloadableResources(renderer, swapChain, renderPass, graphicsPipeline,
                             swapChainFramebuffers);

  vkDestroyPipelineLayout(renderer.Device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderer.Device, descriptorSetLayout, nullptr);

  for (PBRMaterial &material : pbrMaterials) {
    destroyPBRMaterial(renderer, material);
  }
  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);
  vkDestroySampler(renderer.Device, bilinearSampler, nullptr);
  vkDestroySampler(renderer.Device, nearestSampler, nullptr);

  destroyShader(renderer, brdfVertShader);
  destroyShader(renderer, brdfFragShader);
  destroyRenderer(renderer);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}