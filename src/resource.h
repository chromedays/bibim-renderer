#pragma once
#include "render.h"
#include <string>
#include <string_view>
#include <vector>

namespace bb {

inline static const char nativePathSeparator = '\\';
bool isAbsolutePath(std::string_view _path);
std::string joinPaths(std::string_view _a, std::string_view _b);
std::string getFileName(std::string_view _path);

void initResourceRoot();

std::string createCommonResourcePath(std::string_view _relPath);
std::string createShaderPath(std::string_view _relPath);

struct ImageLoadFromFileTask {
  const struct Renderer *Renderer;
  std::string FilePath;
  Image *TargetImage;

  Int2 ImageDims;
  Buffer StagingBuffer;
};

void runImageLoadTask(ImageLoadFromFileTask &_task);

struct ImageLoader {
  std::vector<ImageLoadFromFileTask *> Tasks;
};

void destroyImageLoader(ImageLoader &_loader);
void enqueueImageLoadTask(ImageLoader &_loader, const Renderer &_renderer,
                          std::string_view _filePath, Image &_targetImage);
void finalizeAllImageLoads(ImageLoader &_loader, const Renderer &_renderer,
                           VkCommandPool _cmdPool);

} // namespace bb