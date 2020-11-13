#include "resource.h"
#include "util.h"
#include "vector_math.h"
#include "render.h"
#include "type_conversion.h"
#include "external/stb_image.h"
#include "external/SDL2/SDL.h"
#include "external/toml.h"
#include <string_view>
#ifdef BB_WINDOWS
#include <Windows.h>
#endif

namespace bb {

static std::string gCommonResourceRoot;
static std::string gShaderRoot;

static bool isSeparator(char _ch) { return (_ch == '\\') || (_ch == '/'); }

static void replaceSeparatorsWithNative(std::string &_str, int _offset = 0) {
  std::replace_if(_str.begin() + _offset, _str.end(), isSeparator,
                  nativePathSeparator);
}

static std::string_view trimSeparators(std::string_view _str) {
  bool isValidPath = false;
  int trimBegin = 0;
  int trimEnd = 0;
  for (int i = 0; i < (int)_str.length(); ++i) {
    if (!isSeparator(_str[i])) {
      trimBegin = i;
      isValidPath = true;
      break;
    }
  }
  if (!isValidPath) {
    return {};
  }
  for (int i = (int)_str.length() - 1; i >= 0; --i) {
    if (!isSeparator(_str[i])) {
      trimEnd = i + 1;
      break;
    }
  }

  return _str.substr(trimBegin, trimEnd - trimBegin);
}

// Removes ".." s
static std::string simplifyPath(std::string_view _path) {
  std::vector<std::string_view> dirs;
  dirs.reserve(_path.size());

  int lastAppendedIndex = 0;
  for (int i = 0; i <= (int)_path.length(); ++i) {
    if (i == _path.length() || isSeparator(_path[i])) {
      std::string_view dir =
          _path.substr(lastAppendedIndex, i - lastAppendedIndex);
      lastAppendedIndex = i + 1;
      if (dir == "..") {
        dirs.pop_back();
      } else {
        dirs.push_back(dir);
      }
    }
  }

  std::string result;
  result.reserve(_path.size());
  size_t i = 0;
  for (const std::string_view &dir : dirs) {
    result += dir;
    if (i < dirs.size() - 1) {
      result += nativePathSeparator;
    }
    ++i;
  }

  return result;
}

bool isAbsolutePath(std::string_view _path) {
  return (_path.length() >= 2) && (_path[1] == ':');
}

std::string joinPaths(std::string_view _a, std::string_view _b) {
  std::string joined;
  joined.reserve((_a.size() + _b.size()) * 2);
  _a = trimSeparators(_a);
  _b = trimSeparators(_b);
  joined = _a;
  joined += nativePathSeparator;
  joined += _b;
  replaceSeparatorsWithNative(joined);
  joined = simplifyPath(joined);
  return joined;
}

std::string getFileName(std::string_view _path) {
  int i = (int)_path.length() - 1;
  for (; i >= 0; --i) {
    if (isSeparator(_path[i])) {
      break;
    }
  }

  std::string fileName(_path.substr(i + 1));

  return fileName;
}

void initResourceRoot() {
  std::string exeDir;
  {
    char *temp = SDL_GetBasePath();
    exeDir = temp;
    SDL_free(temp);
  }

  if (!endsWith(exeDir, nativePathSeparator)) {
    exeDir += nativePathSeparator;
  }

  std::string configPath = exeDir + "config.toml";
  FILE *configFile = fopen(configPath.c_str(), "r");
  toml_table_t *config = toml_parse_file(configFile, nullptr, 0);
  fclose(configFile);

  toml_table_t *tomlResourcePath = toml_table_in(config, "resource_path");
  auto getString = [&](toml_table_t *_table, const char *_key) {
    toml_raw_t raw = toml_raw_in(_table, _key);
    char *cstr;
    toml_rtos(raw, &cstr);
    std::string str = cstr;
    free(cstr);
    return str;
  };

  gCommonResourceRoot =
      joinPaths(exeDir, getString(tomlResourcePath, "common_root"));
  gShaderRoot = joinPaths(exeDir, getString(tomlResourcePath, "shader_root"));

  toml_free(config);
}

std::string createCommonResourcePath(std::string_view _relPath) {
  std::string absPath = joinPaths(gCommonResourceRoot, _relPath);
  return absPath;
}

std::string createShaderPath(std::string_view _relPath) {
  std::string absPath = joinPaths(gShaderRoot, _relPath);
  return absPath;
}

void runImageLoadTask(ImageLoadFromFileTask &_task) {
  int numChannels;
  stbi_uc *pixels = stbi_load(_task.FilePath.c_str(), &_task.ImageDims.X,
                              &_task.ImageDims.Y, &numChannels, STBI_rgb_alpha);
  if (!pixels) {
    return;
  }

  BB_DEFER(stbi_image_free(pixels));

  VkDeviceSize textureSize = _task.ImageDims.X * _task.ImageDims.Y * 4;
  const Renderer &renderer = *_task.Renderer;

  _task.StagingBuffer =
      createBuffer(renderer, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  {
    void *data;
    vkMapMemory(renderer.Device, _task.StagingBuffer.Memory, 0, textureSize, 0,
                &data);
    memcpy(data, pixels, textureSize);
    vkUnmapMemory(renderer.Device, _task.StagingBuffer.Memory);
  }

  Image *targetImage = _task.TargetImage;

  VkImageCreateInfo imageCreateInfo = {};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.extent.width = (uint32_t)_task.ImageDims.X;
  imageCreateInfo.extent.height = (uint32_t)_task.ImageDims.Y;
  imageCreateInfo.extent.depth = 1;
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.flags = 0;

  BB_VK_ASSERT(vkCreateImage(renderer.Device, &imageCreateInfo, nullptr,
                             &targetImage->Handle));

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(renderer.Device, targetImage->Handle,
                               &memRequirements);

  VkMemoryAllocateInfo textureImageMemoryAllocateInfo = {};
  textureImageMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  textureImageMemoryAllocateInfo.allocationSize = memRequirements.size;
  textureImageMemoryAllocateInfo.memoryTypeIndex =
      findMemoryType(renderer, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  BB_VK_ASSERT(vkAllocateMemory(renderer.Device,
                                &textureImageMemoryAllocateInfo, nullptr,
                                &targetImage->Memory));

  BB_VK_ASSERT(vkBindImageMemory(renderer.Device, targetImage->Handle,
                                 targetImage->Memory, 0));
}

void destroyImageLoader(ImageLoader &_loader) {
  for (ImageLoadFromFileTask *task : _loader.Tasks) {
    delete task;
  }
  _loader.Tasks.clear();
}

void enqueueImageLoadTask(ImageLoader &_loader, const Renderer &_renderer,
                          std::string_view _filePath, Image &_targetImage) {
  ImageLoadFromFileTask *task = new ImageLoadFromFileTask();
  task->Renderer = &_renderer;
  task->FilePath = _filePath;
  task->TargetImage = &_targetImage;

  _loader.Tasks.push_back(task);
}

void finalizeAllImageLoads(ImageLoader &_loader, const Renderer &_renderer,
                           VkCommandPool _cmdPool) {
  std::vector<HANDLE> threads;
  std::vector<DWORD> threadIds;
  threads.resize(_loader.Tasks.size());
  threadIds.resize(_loader.Tasks.size());

  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  int numCPUs = sysinfo.dwNumberOfProcessors;
  int batch = MAXIMUM_WAIT_OBJECTS;

  for (size_t i = 0; i < _loader.Tasks.size(); ++i) {
    threads[i % batch] = CreateThread(
        nullptr, 0,
        [](LPVOID _param) -> DWORD {
          ImageLoadFromFileTask *task = (ImageLoadFromFileTask *)_param;
          runImageLoadTask(*task);
          return 0;
        },
        _loader.Tasks[i], 0, &threadIds[i % batch]);
    if ((i + 1) % batch == 0) {
      WaitForMultipleObjects(batch, threads.data(), TRUE, INFINITE);
    } else if (i == _loader.Tasks.size() - 1) {
      WaitForMultipleObjects((i + 1) % batch, threads.data(), TRUE, INFINITE);
    }
  }

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = _cmdPool;
  cmdBufferAllocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  BB_VK_ASSERT(
      vkAllocateCommandBuffers(_renderer.Device, &cmdBufferAllocInfo, &cmd));

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  BB_VK_ASSERT(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

  for (size_t i = 0; i < _loader.Tasks.size(); ++i) {
    ImageLoadFromFileTask &task = *_loader.Tasks[i];
    if (task.TargetImage->Handle == VK_NULL_HANDLE) {
      continue;
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = task.TargetImage->Handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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
    region.imageExtent = int2ToExtent3D(task.ImageDims);
    vkCmdCopyBufferToImage(cmd, task.StagingBuffer.Handle,
                           task.TargetImage->Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
  }

  BB_VK_ASSERT(vkEndCommandBuffer(cmd));

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  BB_VK_ASSERT(vkQueueSubmit(_renderer.Queue, 1, &submitInfo, VK_NULL_HANDLE));
  BB_VK_ASSERT(vkQueueWaitIdle(_renderer.Queue));
  vkFreeCommandBuffers(_renderer.Device, _cmdPool, 1, &cmd);

  for (size_t i = 0; i < _loader.Tasks.size(); ++i) {
    ImageLoadFromFileTask &task = *_loader.Tasks[i];

    if (task.TargetImage->Handle == VK_NULL_HANDLE) {
      continue;
    }

    destroyBuffer(_renderer, task.StagingBuffer);

    VkImageViewCreateInfo imageViewCreateInfo = {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = task.TargetImage->Handle;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    BB_VK_ASSERT(vkCreateImageView(_renderer.Device, &imageViewCreateInfo,
                                   nullptr, &task.TargetImage->View));
  }

  for (HANDLE thread : threads) {
    CloseHandle(thread);
  }

  for (ImageLoadFromFileTask *task : _loader.Tasks) {
    delete task;
  }

  _loader.Tasks.clear();
}

} // namespace bb