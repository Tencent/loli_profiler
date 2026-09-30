#ifndef LOLI_COMPARELAUNCH_H
#define LOLI_COMPARELAUNCH_H
#include <filesystem>
#include <string>
namespace gui {
bool LaunchComparison(const std::filesystem::path& mainExecutable, std::string& error);
}
#endif
