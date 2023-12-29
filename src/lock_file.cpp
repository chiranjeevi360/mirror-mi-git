/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <miopen/errors.hpp>
#include <miopen/lock_file.hpp>
#include <miopen/logger.hpp>
#include <miopen/md5.hpp>

namespace miopen {

inline void LogFsError(const std::filesystem::filesystem_error& ex, const std::string& from)
{
    // clang-format off
    MIOPEN_LOG_E_FROM(from, "File system operation error in LockFile. "
                            "Error code: " << ex.code() << ". "
                            "Description: '" << ex.what() << "'");
    // clang-format on
}

std::filesystem::path LockFilePath(const std::filesystem::path& filename_)
{
    try
    {
        const auto directory = std::filesystem::temp_directory_path() / "miopen-lockfiles";

        if(!std::filesystem::exists(directory))
        {
            std::filesystem::create_directories(directory);
            std::filesystem::permissions(directory, std::filesystem::perms::all);
        }
        const auto hash = md5(filename_.parent_path().string());
        const auto file = directory / (hash + "_" + filename_.filename().string() + ".lock");

        return file;
    }
    catch(const std::filesystem::filesystem_error& ex)
    {
        LogFsError(ex, MIOPEN_GET_FN_NAME());
        throw;
    }
}

LockFile::LockFile(const std::filesystem::path& path_, PassKey) : path(path_)
{
    try
    {
        if(!std::filesystem::exists(path))
        {
            if(!std::ofstream{path})
                MIOPEN_THROW("Error creating file <" + path.string() + "> for locking.");
            std::filesystem::permissions(path, std::filesystem::perms::all);
        }
        flock = path.string().c_str();
    }
    catch(const std::filesystem::filesystem_error& ex)
    {
        LogFsError(ex, MIOPEN_GET_FN_NAME());
        throw;
    }
    catch(const boost::interprocess::interprocess_exception& ex)
    {
        LogFlockError(ex, "lock initialization", MIOPEN_GET_FN_NAME());
        throw;
    }
}

LockFile& LockFile::Get(const std::filesystem::path& path)
{
    // NOLINTNEXTLINE (cppcoreguidelines-avoid-non-const-global-variables)
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    { // To guarantee that construction won't be called if not required.
        auto found = LockFiles().find(path);

        if(found != LockFiles().end())
            return found->second;
    }

    auto emplaced = LockFiles().emplace(std::piecewise_construct,
                                        std::forward_as_tuple(path),
                                        std::forward_as_tuple(path, PassKey{}));
    return emplaced.first->second;
}
} // namespace miopen
