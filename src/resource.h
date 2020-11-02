#pragma once
#include <string>
#include <vector>

namespace bb {

inline static const char nativePathSeparator = '\\';
bool isAbsolutePath(std::string_view _path);
std::string joinPaths(std::string_view _a, std::string_view _b);
std::string getFileName(std::string_view _path);

void initResourceRoot();

std::string createCommonResourcePath(std::string_view _relPath);
std::string createShaderPath(std::string_view _relPath);

} // namespace bb