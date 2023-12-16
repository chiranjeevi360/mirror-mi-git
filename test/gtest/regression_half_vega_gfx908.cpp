/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
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
#include <tuple>
#include <string_view>

#include "gtest_common.hpp"

#include "../conv2d.hpp"

auto GetTestCases(void)
{
    const auto env = std::tuple{
        std::pair{ENV(MIOPEN_FIND_MODE), std::string_view("normal")},
        std::pair{ENV(MIOPEN_DEBUG_FIND_ONLY_SOLVER), std::string_view("ConvOclDirectFwd1x1")}};

    const std::string v =
        " --verbose --disable-backward-data --disable-backward-weights "
        "--disable-verification-cache --cmode conv --pmode default --group-count 1";

    return std::vector{
        // clang-format off
    std::pair{env, v + " --input 1 16 7 7 --weights 16 16 1 1 --pads_strides_dilations 0 0 1 1 1 1"}
        // clang-format on
    };
}

using TestCase = decltype(GetTestCases())::value_type;

class Conv2dHalf : public HalfTestCase<std::vector<TestCase>>
{
};

bool IsTestSupportedForDevice()
{
    // Issue #894.
    // Can't be enabled for GFX10 due to WORKAROUND_SWDEV_271887
    using e_mask = enabled<Gpu::Default>;
    using d_mask = disabled<Gpu::gfx90A>;
    return IsTestSupportedForDevice<d_mask, e_mask>();
}

TEST_P(Conv2dHalf, HalfTest)
{
    if(IsTestSupportedForDevice())
    {
        invoke_with_params<conv2d_driver, Conv2dHalf>(default_check);
    }
    else
    {
        GTEST_SKIP();
    }
};

INSTANTIATE_TEST_SUITE_P(RegressionHalfVegaGfx908, Conv2dHalf, testing::Values(GetTestCases()));
