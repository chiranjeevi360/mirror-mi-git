#ifndef MIOPEN_GUARD_MLOPEN_LOAD_FILE_HPP
#define MIOPEN_GUARD_MLOPEN_LOAD_FILE_HPP

#include <filesystem>
#include <string>

namespace miopen {

std::string LoadFile(const std::filesystem::path& path);

} // namespace miopen

#endif
