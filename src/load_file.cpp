#include <miopen/load_file.hpp>
#include <sstream>
#include <fstream>

namespace miopen {

std::string LoadFile(const std::filesystem::path& path)
{
    const std::ifstream t(path);
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

} // namespace miopen
