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
#include <miopen/miopen.h>
#include <gtest/gtest.h>
#include "../conv2d.hpp"
#include "get_handle.hpp"

using TestCase = std::tuple<std::vector<std::string>, std::string>;

MIOPEN_DECLARE_ENV_VAR_BOOL(MIOPEN_TEST_ALL)

static bool SkipTest(void) { return miopen::IsDisabled(MIOPEN_TEST_ALL{}); }

void GetArgs(const TestCase& param, std::vector<std::string>& tokens)
{
    auto env_vars = std::get<0>(param);
    for(auto& elem : env_vars)
    {
        putenv(elem.data());
    }

    auto cmd = std::get<1>(param);

    std::stringstream ss(cmd);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    while(begin != end)
        tokens.push_back(*begin++);
}

class Conv2dFloat : public testing::TestWithParam<std::vector<TestCase>>
{
};

void Run2dDriver(miopenDataType_t prec)
{

    std::vector<TestCase> params;
    switch(prec)
    {
    case miopenFloat: params = Conv2dFloat::GetParam(); break;
    case miopenHalf:
    case miopenInt8:
    case miopenBFloat16:
    case miopenInt32:
    case miopenDouble:
    case miopenFloat8:
    case miopenBFloat8:
        FAIL() << "miopenHalf, miopenInt8, miopenBFloat16, miopenInt32, "
                  "miopenDouble, miopenFloat8, miopenBFloat8 "
                  "data type not supported by conv_igemm_dynamic_xdlops_nhwc_nchw test";

    default: params = Conv2dFloat::GetParam();
    }

    for(const auto& test_value : params)
    {
        std::vector<std::string> tokens;
        GetArgs(test_value, tokens);
        std::vector<const char*> ptrs;

        std::transform(tokens.begin(),
                       tokens.end(),
                       std::back_inserter(ptrs),
                       [](const std::string& str) { return str.data(); });

        testing::internal::CaptureStderr();
        test_drive<conv2d_driver>(ptrs.size(), ptrs.data());
        auto capture = testing::internal::GetCapturedStderr();
        std::cout << capture;
    }
};

bool IsTestSupportedForDevice(const miopen::Handle& handle)
{
    std::string devName = handle.GetDeviceName();
    if(devName == "gfx908")
        return true;
    else
        return false;
}

TEST_P(Conv2dFloat, FloatTest)
{
    const auto& handle = get_handle();
    if(IsTestSupportedForDevice(handle) && !SkipTest())
    {
        Run2dDriver(miopenFloat);
    }
    else
    {
        GTEST_SKIP();
    }
};

std::vector<TestCase> GetTestCases(const std::string& precision)
{

    std::vector<std::string> env = {"MIOPEN_DEBUG_CONV_WINOGRAD=0",
                                    "MIOPEN_DEBUG_CONV_FFT=0",
                                    "MIOPEN_DEBUG_CONV_DIRECT=0",
                                    "MIOPEN_DEBUG_CONV_GEMM=0",
                                    "MIOPEN_DEBUG_CONV_IMPLICIT_GEMM=1",
                                    "MIOPEN_LOG_LEVEL=5"};

    std::string v          = " --verbose";
    std::string dis_fwd    = " --disable-forward";
    std::string dis_bk_wei = " --disable-backward-weights";

    const std::vector<TestCase> test_cases = {
        // clang-format off
    TestCase{env, precision + v + " --input 32 256 38 38 --weights 256 256 1 1 --pads_strides_dilations 0 0 1 1 1 1" + dis_fwd + dis_bk_wei}
        // clang-format on
    };
    return test_cases;
}

INSTANTIATE_TEST_SUITE_P(RegressionMi100, Conv2dFloat, testing::Values(GetTestCases("--float")));
