#ifndef MIOPEN_GUARD_MLOPEN_TMP_DIR_HPP
#define MIOPEN_GUARD_MLOPEN_TMP_DIR_HPP

#include <string_view>
#include <boost/filesystem/path.hpp>

namespace miopen {

struct TmpDir
{
    boost::filesystem::path path;
    explicit TmpDir(std::string_view prefix = "");

    TmpDir(TmpDir&&) = default;
    TmpDir& operator = (TmpDir&&) = default;

    boost::filesystem::path operator / (std::string_view other) const { return path / other; }

    operator const boost::filesystem::path& () const { return path; }
    operator std::string () const { return path.string(); }

    int Execute(const boost::filesystem::path& exec, std::string_view args) const;

    ~TmpDir();
};

} // namespace miopen

#endif
