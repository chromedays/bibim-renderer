#pragma once
#include "render.h"
#include "external/volk.h"
#include <vector>

namespace bb {

struct GizmoPipeline {
  VkDescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout PipelineLayout;
  VkPipeline Handle;
  VkRenderPass RenderPass;

  Image OffscreenImage;
};

GizmoPipeline createGizmoPipeline(const Renderer &_renderer);
void destroyGizmoPipeline(GizmoPipeline &_pipeline);

} // namespace bb