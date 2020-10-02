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
constexpr int numGbufferPass = 5;

struct PBRMaterial {
  static constexpr int ImageCount = 6;

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

Frame createGbufferFrame(const Renderer &_renderer, VkDescriptorPool _descriptorPool,
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

  result.UniformBuffer = createBuffer(_renderer, sizeof(GbufferUniformBlock),
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
  descriptorBufferInfo.range = sizeof(GbufferUniformBlock);

  std::vector<VkDescriptorImageInfo>
      descriptorImageInfos[PBRMaterial::ImageCount];
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

  VkWriteDescriptorSet descriptorWrites[1 + PBRMaterial::ImageCount] = {};
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = result.DescriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &descriptorBufferInfo;
  for (int i = 0; i < PBRMaterial::ImageCount; ++i) {
    VkWriteDescriptorSet &write = descriptorWrites[i + 1]; // +1 for Uniform buffer
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = result.DescriptorSet;
    write.dstBinding = i + 2; // +2 for what?
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

Frame createBrdfFrame(const Renderer &_renderer, VkDescriptorPool _descriptorPool,
                  VkDescriptorSetLayout _descriptorSetLayout, std::vector<VkImageView> _brdfImageView) {
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

  result.UniformBuffer = createBuffer(_renderer, sizeof(BrdfUniformBlock),
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
  descriptorBufferInfo.range = sizeof(BrdfUniformBlock);

  std::vector<VkDescriptorImageInfo>
      descriptorImageInfos[numGbufferPass];
  for (std::vector<VkDescriptorImageInfo> &descriptorImageInfo :
       descriptorImageInfos) {
    descriptorImageInfo.reserve(numGbufferPass);
  }


  

  VkDescriptorImageInfo descImageInfo = {};
  descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // materialDesc.imageView = gBuffer;
  // descriptorImageInfos[0].push_back(materialDesc);
  // materialDesc.imageView = material.MetallicMap.View;
  // descriptorImageInfos[1].push_back(materialDesc);
  // materialDesc.imageView = material.RoughnessMap.View;
  // descriptorImageInfos[2].push_back(materialDesc);
  // materialDesc.imageView = material.AOMap.View;
  // descriptorImageInfos[3].push_back(materialDesc);
  // materialDesc.imageView = material.NormalMap.View;
  // descriptorImageInfos[4].push_back(materialDesc);

  VkWriteDescriptorSet descriptorWrites[1 + PBRMaterial::ImageCount] = {};
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = result.DescriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &descriptorBufferInfo;
  for (int i = 0; i < PBRMaterial::ImageCount; ++i) {
    VkWriteDescriptorSet &write = descriptorWrites[i + 1];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = result.DescriptorSet;
    write.dstBinding = i + 2;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.descriptorCount = (uint32_t)numGbufferPass;
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


struct RenderPass
{
  VkRenderPass Handle;
  VkRenderPassBeginInfo BeginInfo = {};
  std::function< void(VkCommandBuffer, VkDescriptorSet)> Command;
};

void recordCommand(std::vector<Frame> _currentFrames,  std::vector<RenderPass*>& _renderPasses,
                   std::vector<VkFramebuffer> _swapChainFramebuffers) {


  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = 0;
  cmdBeginInfo.pInheritanceInfo = nullptr;

  

  BB_ASSERT(_renderPasses.size() == _swapChainFramebuffers.size() && _renderPasses.size() == _currentFrames.size());

  for(int i = 0; i < _renderPasses.size(); i++)
  {
    BB_VK_ASSERT(vkBeginCommandBuffer(_currentFrames[i].CmdBuffer, &cmdBeginInfo));
    _renderPasses[i]->BeginInfo.framebuffer = _swapChainFramebuffers[i];
    _renderPasses[i]->Command(_currentFrames[i].CmdBuffer, _currentFrames[i].DescriptorSet);
    BB_VK_ASSERT(vkEndCommandBuffer(_currentFrames[i].CmdBuffer));
  }
  //ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), _cmdBuffer);
  
}

struct PipelineParams
{
  std::vector<const Shader *> Shaders;
  VkPipelineLayout PipelineLayout;
};


void initReloadableResources(
    const Renderer &_renderer, uint32_t _width, uint32_t _height,
    const SwapChain *_oldSwapChain, const PipelineParams& _gBufferPipelineParams,
    const PipelineParams& _brdfPipelineParams,
    SwapChain *_outSwapChain, VkRenderPass *_outGbufferRenderPass, VkRenderPass *_outBrdfRenderPass,
    VkPipeline *_outGbufferGraphicsPipeline, VkPipeline *_outBrdfGraphicsPipeline,
    std::vector<VkFramebuffer> *_outGbufferSwapChainFramebuffers, std::vector<VkFramebuffer> *_outBrdfSwapChainFramebuffers) {

  SwapChain swapChain =
      createSwapChain(_renderer, _width, _height, _oldSwapChain);

  std::vector<VkPipelineShaderStageCreateInfo> gBufferShaderStages;
  std::vector<VkPipelineShaderStageCreateInfo> brdfShaderStages;
  
  for(const Shader* shader : _gBufferPipelineParams.Shaders)
    gBufferShaderStages.push_back(shader->getStageInfo());

  for(const Shader* shader : _brdfPipelineParams.Shaders)
    brdfShaderStages.push_back(shader->getStageInfo());

  std::array<VkVertexInputBindingDescription, 2> bindingDescs =
      Vertex::getBindingDescs();
  std::array<VkVertexInputAttributeDescription, 16> attributeDescs =
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

  // 0 = position
  // 1 = normal
  // 2 = Albedo
  // 3 = Metallic
  //     Roughness
  //     AO
  //     Height

  VkAttachmentReference gBufferColorAttachmentRef[numGbufferPass] = {};
  gBufferColorAttachmentRef[0].attachment = 0;
  gBufferColorAttachmentRef[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  gBufferColorAttachmentRef[1].attachment = 1;
  gBufferColorAttachmentRef[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  gBufferColorAttachmentRef[2].attachment = 2;
  gBufferColorAttachmentRef[2].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  gBufferColorAttachmentRef[3].attachment = 3;
  gBufferColorAttachmentRef[3].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  gBufferColorAttachmentRef[4].attachment = 4;
  gBufferColorAttachmentRef[4].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference gBufferDepthAttachmentRef = {};
  gBufferDepthAttachmentRef.attachment = 5;
  gBufferDepthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference brdfColorAttachmentRef = {};
  brdfColorAttachmentRef.attachment = 0;
  brdfColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference brdfDepthAttachmentRef = {};
  brdfDepthAttachmentRef.attachment = 1;
  brdfDepthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


  VkSubpassDescription gBufferSubpass = {};
  gBufferSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  gBufferSubpass.colorAttachmentCount = (uint32_t)std::size(gBufferColorAttachmentRef);
  gBufferSubpass.pColorAttachments = gBufferColorAttachmentRef;
  gBufferSubpass.pDepthStencilAttachment = &gBufferDepthAttachmentRef;

  VkSubpassDescription brdfSubpass = {};
  brdfSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  brdfSubpass.colorAttachmentCount = 1;
  brdfSubpass.pColorAttachments = &brdfColorAttachmentRef;
  brdfSubpass.pDepthStencilAttachment = &brdfDepthAttachmentRef;



  VkSubpassDependency subpassDependency = {};
  subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpassDependency.dstSubpass = 0;
  subpassDependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.srcAccessMask = 0;
  subpassDependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


  VkRenderPassCreateInfo gBufferRenderPassCreateInfo = {};
  gBufferRenderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  VkAttachmentDescription gBufferAttachments[] = {colorAttachment,colorAttachment,colorAttachment,colorAttachment,colorAttachment, depthAttachment};
  gBufferRenderPassCreateInfo.attachmentCount = (uint32_t)std::size(gBufferAttachments);
  gBufferRenderPassCreateInfo.pAttachments = gBufferAttachments;
  gBufferRenderPassCreateInfo.subpassCount = 1;
  gBufferRenderPassCreateInfo.pSubpasses = &gBufferSubpass;
  gBufferRenderPassCreateInfo.dependencyCount = 1;
  gBufferRenderPassCreateInfo.pDependencies = &subpassDependency;

  VkRenderPassCreateInfo brdfRenderPassCreateInfo = {};
  brdfRenderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  VkAttachmentDescription brdfAttachments[] = {colorAttachment, depthAttachment};
  brdfRenderPassCreateInfo.attachmentCount = (uint32_t)std::size(brdfAttachments);
  brdfRenderPassCreateInfo.pAttachments = brdfAttachments;
  brdfRenderPassCreateInfo.subpassCount = 1;
  brdfRenderPassCreateInfo.pSubpasses = &brdfSubpass;
  brdfRenderPassCreateInfo.dependencyCount = 1;
  brdfRenderPassCreateInfo.pDependencies = &subpassDependency;


  VkRenderPass gBufferRenderPass;
  BB_VK_ASSERT(vkCreateRenderPass(_renderer.Device, &gBufferRenderPassCreateInfo,
                                  nullptr, &gBufferRenderPass));

  VkRenderPass brdfRenderPass;
  BB_VK_ASSERT(vkCreateRenderPass(_renderer.Device, &brdfRenderPassCreateInfo,
                                  nullptr, &brdfRenderPass));



  VkGraphicsPipelineCreateInfo gBufferPipelineCreateInfo = {};
  gBufferPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gBufferPipelineCreateInfo.stageCount = _gBufferPipelineParams.Shaders.size();
  gBufferPipelineCreateInfo.pStages = gBufferShaderStages.data();
  gBufferPipelineCreateInfo.pVertexInputState = &vertexInputState;
  gBufferPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  gBufferPipelineCreateInfo.pViewportState = &viewportState;
  gBufferPipelineCreateInfo.pRasterizationState = &rasterizationState;
  gBufferPipelineCreateInfo.pMultisampleState = &multisampleState;
  gBufferPipelineCreateInfo.pDepthStencilState = &depthStencilState;
  gBufferPipelineCreateInfo.pColorBlendState = &colorBlendState;
  gBufferPipelineCreateInfo.pDynamicState = nullptr;
  gBufferPipelineCreateInfo.layout = _gBufferPipelineParams.PipelineLayout;
  gBufferPipelineCreateInfo.renderPass = gBufferRenderPass;
  gBufferPipelineCreateInfo.subpass = 0;
  gBufferPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  gBufferPipelineCreateInfo.basePipelineIndex = -1;
  
  VkPipelineColorBlendAttachmentState gBufferColorBlendStateAttachment[] = {colorBlendAttachmentState,colorBlendAttachmentState,
                                                                      colorBlendAttachmentState,colorBlendAttachmentState,colorBlendAttachmentState};
  colorBlendState.pAttachments = gBufferColorBlendStateAttachment;
  colorBlendState.attachmentCount = (uint32_t)std::size(gBufferColorBlendStateAttachment);
  gBufferPipelineCreateInfo.pColorBlendState = &colorBlendState;
  gBufferPipelineCreateInfo.stageCount = _gBufferPipelineParams.Shaders.size();
  gBufferPipelineCreateInfo.pStages = gBufferShaderStages.data();
  gBufferPipelineCreateInfo.layout = _gBufferPipelineParams.PipelineLayout;
  gBufferPipelineCreateInfo.renderPass = gBufferRenderPass;
  
  VkPipeline gBufferGraphicsPipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_renderer.Device, VK_NULL_HANDLE, 1,
                                         &gBufferPipelineCreateInfo, nullptr,
                                         &gBufferGraphicsPipeline));

  VkGraphicsPipelineCreateInfo brdfPipelineCreateInfo = gBufferPipelineCreateInfo;
  colorBlendState.pAttachments = &colorBlendAttachmentState;
  colorBlendState.attachmentCount = 1;
  brdfPipelineCreateInfo.stageCount = _brdfPipelineParams.Shaders.size();
  brdfPipelineCreateInfo.pStages = brdfShaderStages.data();
  brdfPipelineCreateInfo.layout = _brdfPipelineParams.PipelineLayout;
  brdfPipelineCreateInfo.renderPass = brdfRenderPass;

  VkPipeline brdfGraphicsPipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_renderer.Device, VK_NULL_HANDLE, 1,
                                         &brdfPipelineCreateInfo, nullptr,
                                         &brdfGraphicsPipeline));


  std::vector<VkFramebuffer> gBufferSwapChainFramebuffers(swapChain.NumColorImages);
  std::vector<VkFramebuffer> brdfSwapChainFramebuffers(swapChain.NumColorImages);
  
  // TODO: Create & get image view for gBuffer.
  for (uint32_t i = 0; i < swapChain.NumColorImages; ++i) {
    // VkImageView gBufferAttachments[] = {swapChain.ColorImageViews[i],
    //                                     swapChain.ColorImageViews[i],
    //                                     swapChain.ColorImageViews[i],
    //                                     swapChain.ColorImageViews[i],
    //                                     swapChain.ColorImageViews[i],
    //                                     swapChain.DepthImageView};

    VkImageView brdfAttachments[] = {swapChain.ColorImageViews[i],
                                     swapChain.DepthImageView};

    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = gBufferRenderPass;
    fbCreateInfo.attachmentCount = (uint32_t)std::size(gBufferAttachments);
    fbCreateInfo.pAttachments = gBufferAttachments;
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_renderer.Device, &fbCreateInfo, nullptr,
                                     &gBufferSwapChainFramebuffers[i]));

    fbCreateInfo.renderPass = brdfRenderPass;
    fbCreateInfo.attachmentCount = (uint32_t)std::size(brdfAttachments);
    fbCreateInfo.pAttachments = brdfAttachments;
    BB_VK_ASSERT(vkCreateFramebuffer(_renderer.Device, &fbCreateInfo, nullptr,
                                     &brdfSwapChainFramebuffers[i]));
  }

  *_outSwapChain = std::move(swapChain);
  *_outGbufferSwapChainFramebuffers = std::move(gBufferSwapChainFramebuffers);
  *_outBrdfSwapChainFramebuffers = std::move(brdfSwapChainFramebuffers);
  *_outGbufferRenderPass = gBufferRenderPass;
  *_outBrdfRenderPass = brdfRenderPass;
  *_outGbufferGraphicsPipeline = gBufferGraphicsPipeline;
  *_outBrdfGraphicsPipeline = brdfGraphicsPipeline;
}

void cleanupReloadableResources(
    const Renderer &_renderer, SwapChain &_swapChain, 
    VkRenderPass &_gBufferRenderPass, VkRenderPass &_brdfRenderPass,
    VkPipeline &_gBufferGraphicsPipeline, VkPipeline &_brdfGraphicsPipeline,
    std::vector<VkFramebuffer> &_gBufferSwapChainFramebuffers, std::vector<VkFramebuffer> &_brdfSwapChainFramebuffers) {
  for (VkFramebuffer fb : _gBufferSwapChainFramebuffers) {
    vkDestroyFramebuffer(_renderer.Device, fb, nullptr);
  }

  for (VkFramebuffer fb : _brdfSwapChainFramebuffers) {
    vkDestroyFramebuffer(_renderer.Device, fb, nullptr);
  }
  _gBufferSwapChainFramebuffers.clear();
  _brdfSwapChainFramebuffers.clear();
  vkDestroyPipeline(_renderer.Device, _gBufferGraphicsPipeline, nullptr);
  vkDestroyPipeline(_renderer.Device, _brdfGraphicsPipeline, nullptr);
  _gBufferGraphicsPipeline = _brdfGraphicsPipeline = VK_NULL_HANDLE;
  vkDestroyRenderPass(_renderer.Device, _gBufferRenderPass, nullptr);
  vkDestroyRenderPass(_renderer.Device, _brdfRenderPass, nullptr);
  _gBufferRenderPass = _brdfRenderPass = VK_NULL_HANDLE;
  destroySwapChain(_renderer, _swapChain);
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void onWindowResize(SDL_Window *_window, const Renderer &_renderer,
                    const PipelineParams& _gBufferPipelineParams, const PipelineParams& _brdfPipelineParams, SwapChain &_swapChain,
                    VkRenderPass &_gBufferRenderPass, VkRenderPass &_brdfRenderPass,
                    VkPipeline &_gBufferGraphicsPipeline, VkPipeline &_brdfGraphicsPipeline,
                    std::vector<VkFramebuffer> &_gBufferSwapChainFramebuffers, std::vector<VkFramebuffer> &_brdfSwapChainFramebuffers) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(
      _renderer.Device); // Ensure that device finished using swap chain.

  cleanupReloadableResources(_renderer, _swapChain, _gBufferRenderPass, _brdfRenderPass,
                             _gBufferGraphicsPipeline, _brdfGraphicsPipeline, 
                             _gBufferSwapChainFramebuffers, _brdfSwapChainFramebuffers);

  initReloadableResources(_renderer, width, height, nullptr, _gBufferPipelineParams, _brdfPipelineParams, &_swapChain,
                          &_gBufferRenderPass, &_brdfRenderPass, &_gBufferGraphicsPipeline, &_brdfGraphicsPipeline,
                          &_gBufferSwapChainFramebuffers, &_brdfSwapChainFramebuffers);
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
      shaderBallVertices.push_back(v);
    }
  }

  std::vector<uint32_t> shaderBallIndices;
  shaderBallIndices.resize(shaderBallVertices.size());
  std::iota(shaderBallIndices.begin(), shaderBallIndices.end(), 0);

  Renderer renderer = createRenderer(window);

  std::string shaderRootPath = "../src/shaders";

  Shader gBufferVertShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "gBuffer.vert.spv")));
  Shader gBufferFragShader = createShaderFromFile(
      renderer, createAbsolutePath(joinPaths(shaderRootPath, "gBuffer.frag.spv")));
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
      renderer, transientCmdPool, createAbsolutePath("pbr/branches_twisted")));

  VkDescriptorSetLayoutBinding gBufferDescriptorSetLayoutBindings[8] = {};
  // Uniform Block
  gBufferDescriptorSetLayoutBindings[0].binding = 0;
  gBufferDescriptorSetLayoutBindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  gBufferDescriptorSetLayoutBindings[0].descriptorCount = 1;
  gBufferDescriptorSetLayoutBindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  // Immutable Samplers (Nearest and Bilinear)
  VkSampler samplers[2] = {nearestSampler, bilinearSampler};
  gBufferDescriptorSetLayoutBindings[1].binding = 1;
  gBufferDescriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  gBufferDescriptorSetLayoutBindings[1].descriptorCount =
      (uint32_t)std::size(samplers);
  gBufferDescriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  gBufferDescriptorSetLayoutBindings[1].pImmutableSamplers = samplers;
  // PBR Textures (Albedo, Metallic, Roughness, AO, Normal, Height)
  for (int i = 0; i < PBRMaterial::ImageCount; ++i) {
    VkDescriptorSetLayoutBinding &binding = gBufferDescriptorSetLayoutBindings[i + 2];
    binding.binding = i + 2;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = (uint32_t)pbrMaterials.size();
    binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutBinding brdfDescriptorSetLayoutBindings[8] = {};

  // Uniform Block
  brdfDescriptorSetLayoutBindings[0].binding = 0;
  brdfDescriptorSetLayoutBindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  brdfDescriptorSetLayoutBindings[0].descriptorCount = 1;
  brdfDescriptorSetLayoutBindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  // Immutable Samplers (Nearest and Bilinear)
  brdfDescriptorSetLayoutBindings[1].binding = 1;
  brdfDescriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  brdfDescriptorSetLayoutBindings[1].descriptorCount =
      (uint32_t)std::size(samplers);
  brdfDescriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  brdfDescriptorSetLayoutBindings[1].pImmutableSamplers = samplers;

  // PBR Textures (Albedo, Metallic, Roughness, AO, Normal, Height)
  for (int i = 0; i < PBRMaterial::ImageCount; ++i) {
    VkDescriptorSetLayoutBinding &binding = brdfDescriptorSetLayoutBindings[i + 2];
    binding.binding = i + 2;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = (uint32_t)pbrMaterials.size();
    binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo brdfDescriptorSetLayoutCreateInfo = {};
  brdfDescriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  brdfDescriptorSetLayoutCreateInfo.bindingCount =
      (uint32_t)std::size(brdfDescriptorSetLayoutBindings);
  brdfDescriptorSetLayoutCreateInfo.pBindings = brdfDescriptorSetLayoutBindings;

  VkDescriptorSetLayoutCreateInfo gBufferDescriptorSetLayoutCreateInfo = {};
  gBufferDescriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  gBufferDescriptorSetLayoutCreateInfo.bindingCount =
      (uint32_t)std::size(gBufferDescriptorSetLayoutBindings);
  gBufferDescriptorSetLayoutCreateInfo.pBindings = gBufferDescriptorSetLayoutBindings;

  VkDescriptorSetLayout brdfDescriptorSetLayout;
  BB_VK_ASSERT(vkCreateDescriptorSetLayout(renderer.Device,
                                           &brdfDescriptorSetLayoutCreateInfo,
                                           nullptr, &brdfDescriptorSetLayout));

  VkDescriptorSetLayout gBufferDescriptorSetLayout;
  BB_VK_ASSERT(vkCreateDescriptorSetLayout(renderer.Device,
                                           &gBufferDescriptorSetLayoutCreateInfo,
                                           nullptr, &gBufferDescriptorSetLayout));

  VkPipelineLayoutCreateInfo gBufferPipelineLayoutCreateInfo = {};
  gBufferPipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  gBufferPipelineLayoutCreateInfo.setLayoutCount = 1;
  gBufferPipelineLayoutCreateInfo.pSetLayouts = &gBufferDescriptorSetLayout;
  gBufferPipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  gBufferPipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout gBufferPipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(
      renderer.Device, &gBufferPipelineLayoutCreateInfo, nullptr, &gBufferPipelineLayout));

  VkPipelineLayoutCreateInfo brdfPipelineLayoutCreateInfo = {};
  brdfPipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  brdfPipelineLayoutCreateInfo.setLayoutCount = 1;
  brdfPipelineLayoutCreateInfo.pSetLayouts = &brdfDescriptorSetLayout;
  brdfPipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  brdfPipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout brdfPipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(
      renderer.Device, &brdfPipelineLayoutCreateInfo, nullptr, &brdfPipelineLayout));

  SwapChain swapChain;
  RenderPass gBufferRenderPass;
  RenderPass brdfRenderPass;
  VkPipeline gBufferGraphicsPipeline;
  VkPipeline brdfGraphicsPipeline;
  std::vector<VkFramebuffer> gBufferSwapChainFramebuffers;
  std::vector<VkFramebuffer> brdfSwapChainFramebuffers;

  PipelineParams brdfPipelineParam;
  brdfPipelineParam.Shaders.push_back(&brdfVertShader);
  brdfPipelineParam.Shaders.push_back(&brdfFragShader);
  brdfPipelineParam.PipelineLayout = brdfPipelineLayout;

  PipelineParams gBufferPipelineParam;
  gBufferPipelineParam.Shaders.push_back(&gBufferVertShader);
  gBufferPipelineParam.Shaders.push_back(&gBufferFragShader);
  gBufferPipelineParam.PipelineLayout = gBufferPipelineLayout;

  std::vector<PipelineParams> pipelineParams;
  pipelineParams.push_back(brdfPipelineParam);
  pipelineParams.push_back(gBufferPipelineParam);

  initReloadableResources(renderer, width, height, nullptr, gBufferPipelineParam, brdfPipelineParam, 
                          &swapChain, &gBufferRenderPass.Handle, &brdfRenderPass.Handle, 
                          &gBufferGraphicsPipeline, &brdfGraphicsPipeline,
                          &gBufferSwapChainFramebuffers, &brdfSwapChainFramebuffers);

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

  VkDescriptorPoolSize gBufferDescriptorPoolSizes[3] = {};
  gBufferDescriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  gBufferDescriptorPoolSizes[0].descriptorCount = numFrames;

  gBufferDescriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  gBufferDescriptorPoolSizes[1].descriptorCount =
      numFrames * PBRMaterial::ImageCount * pbrMaterials.size();
      
  gBufferDescriptorPoolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER; // 1 for nearest, 1 for bilinear
  gBufferDescriptorPoolSizes[2].descriptorCount = numFrames * 2;

  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
  descriptorPoolCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.poolSizeCount =
      (uint32_t)std::size(gBufferDescriptorPoolSizes);
  descriptorPoolCreateInfo.pPoolSizes = gBufferDescriptorPoolSizes;
  descriptorPoolCreateInfo.maxSets = numFrames;

  VkDescriptorPool gBufferDescriptorPool;
  BB_VK_ASSERT(vkCreateDescriptorPool(
      renderer.Device, &descriptorPoolCreateInfo, nullptr, &gBufferDescriptorPool));

  VkDescriptorPoolSize brdfDescriptorPoolSizes[3] = {};
  brdfDescriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  brdfDescriptorPoolSizes[0].descriptorCount = numFrames;

  brdfDescriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  brdfDescriptorPoolSizes[1].descriptorCount = numGbufferPass; // 5 for Position, normal, Albedo, MRAH and Index
      
  brdfDescriptorPoolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER; // 1 for nearest, 1 for bilinear
  brdfDescriptorPoolSizes[2].descriptorCount = numFrames * 2;


  descriptorPoolCreateInfo.poolSizeCount =
      (uint32_t)std::size(brdfDescriptorPoolSizes);
  descriptorPoolCreateInfo.pPoolSizes = brdfDescriptorPoolSizes;
  descriptorPoolCreateInfo.maxSets = numFrames;

  VkDescriptorPool brdfDescriptorPool;
  BB_VK_ASSERT(vkCreateDescriptorPool(
      renderer.Device, &descriptorPoolCreateInfo, nullptr, &brdfDescriptorPool));

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
  std::vector<Frame> gBufferFrames;
  for (int i = 0; i < numFrames; ++i) {
    gBufferFrames.push_back(createGbufferFrame(renderer, gBufferDescriptorPool, gBufferDescriptorSetLayout,
                                 pbrMaterials));
  }

  std::vector<VkImageView> brdfImageView;

  brdfImageView.push_back()

  std::vector<Frame> brdfFrames;
  for (int i = 0; i < numFrames; ++i) {
    brdfFrames.push_back(createBrdfFrame(renderer, brdfDescriptorPool, brdfDescriptorSetLayout));
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
  ImGui_ImplVulkan_Init(&initInfo, brdfRenderPass.Handle);

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


  gBufferRenderPass.BeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  gBufferRenderPass.BeginInfo.renderPass = gBufferRenderPass.Handle;
  gBufferRenderPass.BeginInfo.framebuffer = nullptr;
  gBufferRenderPass.BeginInfo.renderArea.offset = {0, 0};
  gBufferRenderPass.BeginInfo.renderArea.extent = swapChain.Extent;

  VkClearValue clearValues[2] = {0.f, 0.f, 0.f, 1.f};
  clearValues[0].color = {0, 0, 0, 1};
  clearValues[1].depthStencil = {0, 0};
  gBufferRenderPass.BeginInfo.clearValueCount = (uint32_t)std::size(clearValues);
  gBufferRenderPass.BeginInfo.pClearValues = clearValues;

  brdfRenderPass.BeginInfo = gBufferRenderPass.BeginInfo;
  brdfRenderPass.BeginInfo.renderPass = brdfRenderPass.Handle;



  std::function< void(VkCommandBuffer, VkDescriptorSet)> gBufferCommand = [&](VkCommandBuffer _cmdBuffer, VkDescriptorSet _descSet)
  {
    vkCmdBeginRenderPass(_cmdBuffer, &gBufferRenderPass.BeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      gBufferGraphicsPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(_cmdBuffer, 0, 1, &shaderBallVertexBuffer.Handle, &offset);
    vkCmdBindVertexBuffers(_cmdBuffer, 1, 1, &instanceBuffer.Handle, &offset);
    vkCmdBindIndexBuffer(_cmdBuffer, shaderBallIndexBuffer.Handle, 0,
                        VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            gBufferPipelineLayout, 0, 1, &_descSet, 0, nullptr);

    vkCmdDrawIndexed(_cmdBuffer, (uint32_t)shaderBallIndices.size(), numInstances, 0, 0, 0);
  };

  std::vector<RenderPass*> renderPasses;
  renderPasses.push_back(&gBufferRenderPass);
  renderPasses.push_back(&brdfRenderPass);

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

    Frame &currentGbufferFrame = gBufferFrames[currentFrameIndex];
    Frame &currentBrdfFrame = brdfFrames[currentFrameIndex];

    VkResult acquireNextGbufferImageResult =
        vkAcquireNextImageKHR(renderer.Device, swapChain.Handle, UINT64_MAX,
                              currentGbufferFrame.ImagePresentedSemaphore,
                              VK_NULL_HANDLE, &currentSwapChainImageIndex);

    if (acquireNextGbufferImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          renderer.PhysicalDevice, renderer.Surface,
          &renderer.SwapChainSupportDetails.Capabilities);

      onWindowResize(window, renderer, brdfPipelineParam, gBufferPipelineParam, swapChain, gBufferRenderPass.Handle, brdfRenderPass.Handle,
       gBufferGraphicsPipeline, brdfGraphicsPipeline, gBufferSwapChainFramebuffers, brdfSwapChainFramebuffers);
      continue;
    }
    vkAcquireNextImageKHR(renderer.Device, swapChain.Handle, UINT64_MAX,
                              currentBrdfFrame.ImagePresentedSemaphore,
                              VK_NULL_HANDLE, &currentSwapChainImageIndex);

    // TODO: determining frameBuffer
    VkFramebuffer currentGbufferSwapChainFramebuffer =
        gBufferSwapChainFramebuffers[currentSwapChainImageIndex];

    VkFramebuffer currentBrdfSwapChainFramebuffer =
        brdfSwapChainFramebuffers[currentSwapChainImageIndex];

    std::vector<VkFramebuffer> currentFrameBuffers;
    currentFrameBuffers.push_back(currentGbufferSwapChainFramebuffer);
    currentFrameBuffers.push_back(currentBrdfSwapChainFramebuffer);


    

    vkWaitForFences(renderer.Device, 1, &currentBrdfFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentBrdfFrame.FrameAvailableFence);

    currentFrameIndex = (currentFrameIndex + 1) % (uint32_t)gBufferFrames.size();

    static float angle = 0;
    angle += 30.f * dt;
    if (angle > 360) {
      angle -= 360;
    }
    GbufferUniformBlock gBufferUniformBlock = {};
    gBufferUniformBlock.ViewMat = cam.getViewMatrix();
    gBufferUniformBlock.ProjMat =
        Mat4::perspective(60.f, (float)width / (float)height, 0.1f, 1000.f);
    gBufferUniformBlock.ViewPos = cam.Pos;

    BrdfUniformBlock brdfUniformBlock = {};
    brdfUniformBlock.NumLights = 3;
    Light *light = &brdfUniformBlock.lights[0];
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
      vkMapMemory(renderer.Device, currentGbufferFrame.UniformBuffer.Memory, 0,
                  sizeof(GbufferUniformBlock), 0, &data);
      memcpy(data, &gBufferUniformBlock, sizeof(GbufferUniformBlock));
      vkUnmapMemory(renderer.Device, currentGbufferFrame.UniformBuffer.Memory);
    }

    {
      void *data;
      vkMapMemory(renderer.Device, currentBrdfFrame.UniformBuffer.Memory, 0,
                  sizeof(BrdfUniformBlock), 0, &data);
      memcpy(data, &brdfUniformBlock, sizeof(BrdfUniformBlock));
      vkUnmapMemory(renderer.Device, currentBrdfFrame.UniformBuffer.Memory);
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

    vkResetCommandPool(renderer.Device, currentGbufferFrame.CmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    vkResetCommandPool(renderer.Device, currentBrdfFrame.CmdPool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    ImGui::Render();
    
    std::vector<Frame> currentFrames;
    currentFrames.push_back(currentGbufferFrame);
    currentFrames.push_back(currentBrdfFrame);
    recordCommand(currentFrames, renderPasses, currentFrameBuffers);

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
    submitInfo.pCommandBuffers = &currentGbufferFrame.CmdBuffer;

    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo,
                               currentGbufferFrame.FrameAvailableFence));


    vkWaitForFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence,
                    VK_TRUE, UINT64_MAX);
    vkResetFences(renderer.Device, 1, &currentGbufferFrame.FrameAvailableFence);
    

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentBrdfFrame.RenderFinishedSemaphore;
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

      onWindowResize(window, renderer, brdfPipelineParam, gBufferPipelineParam, swapChain, gBufferRenderPass.Handle, brdfRenderPass.Handle,
       gBufferGraphicsPipeline, brdfGraphicsPipeline, gBufferSwapChainFramebuffers, brdfSwapChainFramebuffers);
    }


    submitInfo.pWaitSemaphores = &currentBrdfFrame.ImagePresentedSemaphore;
    submitInfo.pSignalSemaphores = &currentBrdfFrame.RenderFinishedSemaphore;
    submitInfo.pCommandBuffers = &currentBrdfFrame.CmdBuffer;

    BB_VK_ASSERT(vkQueueSubmit(renderer.Queue, 1, &submitInfo,
                               currentBrdfFrame.FrameAvailableFence));
  }

  vkDeviceWaitIdle(renderer.Device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  for (Frame &frame : gBufferFrames) {
    destroyFrame(renderer, frame);
  }
  for (Frame &frame : brdfFrames) {
    destroyFrame(renderer, frame);
  }

  vkDestroyDescriptorPool(renderer.Device, gBufferDescriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, brdfDescriptorPool, nullptr);
  vkDestroyDescriptorPool(renderer.Device, imguiDescriptorPool, nullptr);

  destroyBuffer(renderer, shaderBallIndexBuffer);
  destroyBuffer(renderer, shaderBallVertexBuffer);
  destroyBuffer(renderer, instanceBuffer);
  destroyBuffer(renderer, quadIndexBuffer);
  destroyBuffer(renderer, quadVertexBuffer);

  cleanupReloadableResources(renderer, swapChain, gBufferRenderPass.Handle, brdfRenderPass.Handle,
                             gBufferGraphicsPipeline, brdfGraphicsPipeline, 
                             gBufferSwapChainFramebuffers, brdfSwapChainFramebuffers);

  vkDestroyPipelineLayout(renderer.Device, brdfPipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderer.Device, brdfDescriptorSetLayout, nullptr);

  for (PBRMaterial &material : pbrMaterials) {
    destroyPBRMaterial(renderer, material);
  }
  vkDestroyCommandPool(renderer.Device, transientCmdPool, nullptr);
  vkDestroySampler(renderer.Device, bilinearSampler, nullptr);
  vkDestroySampler(renderer.Device, nearestSampler, nullptr);

  destroyShader(renderer, brdfVertShader);
  destroyShader(renderer, brdfFragShader);
  destroyShader(renderer, gBufferVertShader);
  destroyShader(renderer, gBufferFragShader);
  destroyRenderer(renderer);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}