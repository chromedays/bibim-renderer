#pragma once
#include <string>
#include <vector>

namespace bb {

inline static const char nativePathSeparator = '\\';
std::string createAbsolutePath(std::string_view _relPath);
bool isAbsolutePath(std::string_view _path);
std::string joinPaths(std::string_view _a, std::string_view _b);
std::string getFileName(std::string_view _path);
void initResourceRoot();

} // namespace bb