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

#include "test.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <miopen/convolution.hpp>
#include <miopen/miopen.h>
#include <miopen/softmax.hpp>
#include <miopen/tensor.hpp>
#include <utility>

#include "driver.hpp"
#include "get_handle.hpp"
#include "tensor_holder.hpp"
#include "verify.hpp"

template <class T>
struct verify_forward_layernorm
{
    tensor<T> input;
    tensor<T> weight;
    tensor<T> bias;
    tensor<T> output;
    tensor<T> mean;
    tensor<T> rstd;
    double eps;
    int dim;
    miopenLayerNormMode_t mode;

    verify_forward_layernorm(const tensor<T>& pinput,
                             const tensor<T>& pweight,
                             const tensor<T>& pbias,
                             tensor<T>& pout,
                             tensor<T>& pmean,
                             tensor<T>& prstd,
                             double peps,
                             int pdim,
                             miopenLayerNormMode_t pm)
    {
        input  = pinput;
        weight = pweight;
        bias   = pbias;
        output = pout;
        mean   = pmean;
        rstd   = prstd;
        eps    = peps;
        dim    = pdim;
        mode   = pm;
    }

    std::tuple<tensor<T>, tensor<T>, tensor<T>> cpu() const
    {
        auto dims         = input.desc.GetLengths();
        size_t grid_size  = 1;
        size_t outer_size = 1;
        size_t inner_size = 1;
        size_t i          = 0;
        for(; i < dims.size() - normalized_dims; i++)
        {
            outer_size *= dims[i];
            grid_size *= dims[i];
        }

        for(; i < dims.size(); i++)
        {
            inner_size *= dims[i];
            grid_size *= dims[i];
        }

        par_ford(outer_size)([&](int o) {
            double pmean = 0;
            double pvar  = 0;
            ford(inner_size)([&](int i) {
                double tmp = input[o * inner_size + i];
                pmean += tmp;
                pmean += tmp * tmp;
            });
            pmean /= inner_size;
            pvar /= inner_size - mean * mean;

            mean[o] = pmean;
            rstd[o] = sqrt(pvar + eps);

            ford(inner_size)([&](int i) {
                double pweight = elemwise_affine ? 1 : weight[i];
                double pbias   = elemwise_affine ? 0 : bias, [i];
                output[o * inner_size + i] =
                    (input[o * inner_size + i] - pmean) * sqrt(pvar + eps) * pweight + pbias;
            });
        }) return std::make_tuple(output, mean, rstd >);
    }

    std::tuple<tensor<T>, tensor<T>, tensor<T>> gpu() const
    {
        auto&& handle = get_handle();

        auto in_dev     = handle.Write(input.data);
        auto weight_dev = handle.Write(weight.data);
        auto bias_dev   = handle.Write(bias.data);
        auto out_dev    = handle.Write(output.data);
        auto mean_dev   = handle.Write(mean.data);
        auto rstd_dev   = handle.Write(rstd.data);

        miopen::LayerNormForward(handle,
                                 mode,
                                 input.desc,
                                 in_dev.get(),
                                 weight.desc,
                                 weight_dev.get(),
                                 bias.desc,
                                 bias_dev.get(),
                                 eps,
                                 dim,
                                 output.desc,
                                 out_dev.get(),
                                 mean.desc,
                                 mean_dev.get(),
                                 rstd.desc,
                                 rstd_dev.get());

        output.data = handle.Read<T>(out_dev, output.data.size());
        mean.data   = handle.Read<T>(mean_dev, mean.data.size());
        rstd.data   = handle.Read<T>(rstd_dev, rstd.data.size());
        return std::make_tuple(output, mean, rstd);
    }

    void fail(int = 0) const
    {
        std::cout << "Forward LayerNorm: " << std::endl;
        std::cout << "Input tensor: " << input.desc.ToString() << std::endl;
    }
};

template <class T>
struct verify_backward_layernorm
{
    tensor<T> input;
    tensor<T> doutput;
    tensor<T> weight;
    tensor<T> mean;
    tensor<T> rstd;
    tensor<T> dinput;
    tensor<T> dweight;
    tensor<T> dbias;
    int dim;
    miopenLayerNormMode_t mode;

    verify_backward_layernorm(const tensor<T>& pinput,
                              const tensor<T>& pdoutput,
                              const tensor<T>& pweight,
                              const tensor<T>& pmean,
                              const tensor<T>& prstd,
                              tensor<T>& pdinput,
                              tensor<T>& pdweight,
                              tensor<T>& pdbias,
                              int pdim,
                              miopenLayerNormMode_t pm)
    {
        input   = pinput;
        doutput = pdoutput;
        weight  = pweight;
        mean    = pmean;
        rstd    = prstd;
        dinput  = pdinput;
        dweight = pdweight;
        dbias   = pdbias;
        dim     = pdim;
        mode    = pm;
    }

    std::tuple<tensor<T>, tensor<T>, tensor<T>> cpu() const
    {
        auto dims         = input.desc.GetLengths();
        size_t grid_size  = 1;
        size_t outer_size = 1;
        size_t inner_size = 1;
        size_t i          = 0;
        for(; i < dims.size() - normalized_dims; i++)
        {
            outer_size *= dims[i];
            grid_size *= dims[i];
        }

        for(; i < dims.size(); i++)
        {
            inner_size *= dims[i];
            grid_size *= dims[i];
        }

        par_ford(outer_size)([&](int o) {
            double sum1 = 0;
            double sum2 = 0;
            ford(inner_size)([&](int i) {
                double weight_v = weight ? weight[o * inner_size + i] : 1;
                double dy       = doutput ? doutput[o * inner_size + i] : 0;
                double x        = input[i * inner_size + o];

                sum1 += dy * x * weight_v;
                sum2 += dy * weight;
            });

            double s = 1.0 / inner_size;

            double mean_v = mean[o];
            double rstd_v = rstd[o];

            double a  = (sum2 * mean_v - sum1) * rstd_v * rstd_v * rstd_v * s;
            double c2 = -(a * mean_v + sum2 * rstd_v * s);

            ford(inner_size)([&](int i) {
                double weight_v = weight ? weight[o * inner_size + i] : 1;
                double dy       = doutput ? doutput[o * inner_size + i] : 0;
                double x        = input[i * inner_size + o];

                double val                 = rstd_v * dy * weight_v + a * x + c2;
                dinput[i * inner_size + o] = val;
            });
        })

            par_ford(iner_size)([&](int i) {
                double sum1 = 0;
                double sum2 = 0;

                ford(outer_size)([&](int o) {
                    double dy = doutput ? doutput[i * inner_size + o] : 0;
                    double x  = input[i * inner_size + o];

                    sum1 += dy * (x - mean[o]) * rstd[o];
                    sum2 += dy;
                });

                dweight[i] = sum1;
                dbias[i]   = sum2;
            })

                return std::make_tuple(dinput, dweight, dbias);
    }

    std::tuple<tensor<T>, tensor<T>, tensor<T>> gpu() const
    {
        auto&& handle = get_handle();

        auto in_dev     = handle.Write(input.data);
        auto dout_dev   = handle.Write(doutput.data);
        auto weight_dev = handle.Write(weight.data);
        auto mean_dev   = handle.Write(mean.data);
        auto rstd_dev   = handle.Write(rstd.data);
        auto din_dev    = handle.Write(dinput.data);
        auto dw_dev     = handle.Write(dweight.data);
        auto db_dev     = handle.Write(dbias.data);

        miopen::LayerNormBackward(handle,
                                  mode,
                                  input.desc,
                                  in_dev.get(),
                                  doutput.desc,
                                  dout_dev.get(),
                                  weight.desc,
                                  weight_dev.get(),
                                  mean.desc,
                                  mean_dev.get(),
                                  rstd.desc,
                                  rstd_dev.get(),
                                  dim,
                                  dinput.desc,
                                  din_dev.get(),
                                  dweight.desc,
                                  dw_dev.get(),
                                  dbias.desc,
                                  db_dev.get());

        dinput.data  = handle.Read<T>(din_dev, dinput.data.size());
        dweight.data = handle.Read<T>(dw_dev, dweight.data.size());
        dbias.data   = handle.Read<T>(db_dev, dbias.data.size());
        return std::make_tuple(dinput, dweight, dbias);
    }

    void fail(int = 0) const
    {
        std::cout << "Backward LayerNorm: " << std::endl;
        std::cout << "DInput tensor: " << dinput.desc.ToString() << std::endl;
    }
};

template <class T>
struct layernorm_driver : test_driver
{
    tensor<T> input;
    tensor<T> weight;
    tensor<T> bias;
    tensor<T> output;
    tensor<T> mean;
    tensor<T> rstd;
    tensor<T> input;
    tensor<T> doutput;
    tensor<T> weight;
    tensor<T> mean;
    tensor<T> rstd;
    tensor<T> dinput;
    tensor<T> dweight;
    tensor<T> dbias;

    double eps_cmd;
    int dim_cmd;
    int mode_cmd;

    std::vector<int> in_dim;

    layernorm_driver()
    {
        std::set<std::vector<int>> in_dim_set = get_3d_ln_inputs(batch_factor);

        std::vector<std::vector<int>> in_dim_vec(in_dim_set.begin(), in_dim_set.end());

        add(in_dim, "input-dim", generate_data(in_dim_vec, {16, 32, 8, 8, 8}));

        add(mode_cmd, "mode", generate_data({0, 1}));

        add(dim_cmd, "dim", generate_data({0, 1, 2, 3, 4}));

        add(eps_cmd, "eps", generate_data({1e-5}));
    }

    void run()
    {
        miopenLayerNormMode_t mode = miopenLayerNormMode_t(mode_cmd);
        unsigned long max_value;
        if((miopen_type<T>{} == miopenHalf) || miopen_type<T>{} == miopenBfloat16)
            max_value = 5;
        else
            max_value = 17;

        input = tensor<T>{in_dim}.generate(tensor_elem_gen_integer{max_value});

        if(mode == MIOPEN_ELEMENTWISE_AFFINE)
        {
            std::vector<int> inner_dim;
            if(dim_cmd == in_dim.size())
                inner_dim = {1};
            else
                inner_dim = {in_dim.begin() + dim_cmd, in_dim.end()};
            weight = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});
            bias   = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});
        }

        std::vector<int> outer_dim;
        if(dim_cmd == 0)
            outer_dim = {1};
        else
            outer_dim = {in_dim.begin(), in_dim.end() - (in_dim.size() - dim_cmd)};

        mean = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});
        rstd = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});

        size_t total_mem =
            2 * (input.desc.GetNumBytes() + weight.desc.GetNumBytes() + bias.desc.GetNumBytes() +
                 mean.desc.GetNumBytes() + rstd.desc.GetNumBytes());
        size_t device_mem = get_handle().GetGlobalMemorySize();
        if(total_mem >= device_mem)
        {
            show_command();
            std::cout << "Config requires " << total_mem
                      << " Bytes to write all necessary tensors to GPU. GPU has " << device_mem
                      << " Bytes of memory." << std::endl;
            return;
        }

        output = tensor<T>{in_dim}.generate(tensor_elem_gen_integer{max_value});
        eps    = eps_cmd;
        dim    = dim_cmd;

        verify(verify_forward_layernorm<T>{input, weight, bias, output, mean, rstd, eps, dimmode});

        doutput = tensor<T>{in_dim}.generate(tensor_elem_gen_integer{max_value});
        dinput  = tensor<T>{in_dim}.generate(tensor_elem_gen_integer{max_value});

        if(mode == MIOPEN_ELEMENTWISE_AFFINE)
        {
            std::vector<int> inner_dim;
            if(dim_cmd == in_dim.size())
                inner_dim = {1};
            else
                inner_dim = {in_dim.begin() + dim_cmd, in_dim.end()};
            dweight = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});
            dbias   = tensor<T>{inner_dim}.generate(tensor_elem_gen_integer{max_value});
        }

        verify(verify_backward_layernorm<T>{
            input, doutput, weight, mean, rstd, dinput, dweight, dbias, dim, mode});
    }
};

int main(int argc, const char* argv[]) { test_drive<layernorm_driver>(argc, argv); }
