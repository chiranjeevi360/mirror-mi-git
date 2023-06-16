#include <hip/hip_runtime.h>
#include <hip/hip_fp16.h>
// Copied over from naive_conv.cpp
#ifdef __HIPCC_RTC__
#ifdef WORKAROUND_ISSUE_HIPRTC_TRUE_TYPE
/// Definitions from <cstdint>, <cmath> conflict with
/// /opt/rocm/include/hip/amd_detail/amd_hip_vector_types.h.

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef float float_t;

// std::conditional requires type_traits which has a few other things
// which result in collision with amd_hip_vector_types.h

namespace std {
template <bool predicate, typename X, typename Y>
struct conditional;

template <typename X, typename Y>
struct conditional<true, X, Y>
{
    using type = X;
};

template <typename X, typename Y>
struct conditional<false, X, Y>
{
    using type = Y;
};

template <bool predicate, typename X, typename Y>
using conditional_t = typename conditional<predicate, X, Y>::type;
} // namespace std
#else
#include <cstdint> // int8_t, int16_t
#include <cmath>   // float_t
#endif
#endif // __HIPCC_RTC__

#include <limits> // std::numeric_limits

#define MIOPEN_ENABLE_F8_DEVICE_CODE 1
#include "hip_float8.h"

struct Numerics
{
    float sum;
    float absSum;
    float min;
    float max;
};

struct CheckNumericsResult
{
    Numerics n;

    bool hasZero;
    bool hasNan;
    bool hasInf;
};

__device__ void thread_redux(Numerics* stats, size_t wid)
{
    const auto lid = threadIdx.x;
    if(lid < wid)
    {
        stats[lid].sum += stats[lid + wid].sum;
        stats[lid].absSum += stats[lid + wid].absSum;
        stats[lid].min = fmin(stats[lid].min, stats[lid + wid].min);
        stats[lid].max = fmax(stats[lid].max, stats[lid + wid].max);
    }
}
#if 0
__device__ void atomicMax(float* __restrict__ target, float val)
{
    float current, expected, next;

    current = *target;
    do
    {
        expected = current;
        next = fmax(current, val);
        if(next == current)
            break;
        const auto i_expected = *(reinterpret_cast<unsigned int*>(&expected));
        const auto i_next     = *(reinterpret_cast<unsigned int*>(&next));
        auto i_current = atomicCAS(reinterpret_cast<unsigned int*>(target), i_expected, i_next);
        current        = *(reinterpret_cast<float*>(&i_current));
    } while(current != expected);
}

__device__ void atomicMin(float* __restrict__ target, float val)
{
    float current, expected, next;

    current = *target;
    do
    {
        expected = current;
        next     = fmin(current, val);
        if(next == current)
            break;
        const auto i_expected = *(reinterpret_cast<unsigned int*>(&expected));
        const auto i_next     = *(reinterpret_cast<unsigned int*>(&next));
        auto i_current = atomicCAS(reinterpret_cast<unsigned int*>(target), i_expected, i_next);
        current        = *(reinterpret_cast<float*>(&i_current));
    } while(current != expected);
}
#endif
template <typename T, typename U>
__device__ void
check_numerics(const T* C_d, size_t sz, CheckNumericsResult* abnormal, bool computeStats)
{
    __shared__ Numerics stats[256];
    U sum    = 0;
    U absSum = 0;
    T minV   = std::numeric_limits<T>::max();
    T maxV   = std::numeric_limits<T>::min();

    size_t offset = (blockIdx.x * blockDim.x + threadIdx.x);
    size_t stride = blockDim.x * gridDim.x;

    for(size_t i = offset; i < sz; i += stride)
    {
        T val = C_d[i];
        sum += static_cast<U>(val);
        const auto abs_val = fabs(static_cast<U>(val));
        absSum += abs_val;
        minV = : min(minV, val);
        maxV = max(maxV, val);
        if(abs_val <= static_cast<U>(0.0f))
            abnormal->hasZero = true;
        if(isnan(static_cast<U>(val)))
            abnormal->hasNan = true;
        if(isinf(static_cast<U>(val)))
            abnormal->hasInf = true;
    }
    if(computeStats)
    {
        stats[threadIdx.x].sum    = static_cast<float>(sum);
        stats[threadIdx.x].absSum = static_cast<float>(absSum);
        stats[threadIdx.x].min    = static_cast<float>(minV);
        stats[threadIdx.x].max    = static_cast<float>(maxV);
        __syncthreads();
        for(int idx = 128; idx > 0; idx = idx >> 1)
        {
            thread_redux(stats, idx);
            __syncthreads();
        }
        if(threadIdx.x == 0)
        {
            atomicAdd(&abnormal->n.sum, stats[0].sum);
            atomicAdd(&abnormal->n.absSum, stats[0].absSum);
            atomicMin(&abnormal->n.min, stats[0].min);
            atomicMax(&abnormal->n.max, stats[0].max);
        }
    }
}

extern "C" __global__ void check_numerics_fp32(const void* __restrict__ C_d,
                                               size_t sz,
                                               CheckNumericsResult* __restrict__ abnormal,
                                               bool computeStats)
{
    check_numerics<float, float>(reinterpret_cast<const float*>(C_d), sz, abnormal, computeStats);
}

extern "C" __global__ void check_numerics_fp16(const void* __restrict__ C_d,
                                               size_t sz,
                                               CheckNumericsResult* __restrict__ abnormal,
                                               bool computeStats)
{
    check_numerics<_Float16, float>(reinterpret_cast<const half*>(C_d), sz, abnormal, computeStats);
}

extern "C" __global__ void check_numerics_bf16(const void* __restrict__ C_d,
                                               size_t sz,
                                               CheckNumericsResult* __restrict__ abnormal,
                                               bool computeStats)
{
    check_numerics<hip_bfloat16, float>(
        reinterpret_cast<const hip_bfloat16*>(C_d), sz, abnormal, computeStats);
}

extern "C" __global__ void check_numerics_fp8(const void* __restrict__ C_d,
                                              size_t sz,
                                              CheckNumericsResult* __restrict__ abnormal,
                                              bool computeStats)
{
    check_numerics<miopen_f8::hip_f8<miopen_f8::hip_f8_type::fp8>, float>(
        reinterpret_cast<const miopen_f8::hip_f8<miopen_f8::hip_f8_type::fp8>*>(C_d),
        sz,
        abnormal,
        computeStats);
}

extern "C" __global__ void check_numerics_bf8(const void* __restrict__ C_d,
                                              size_t sz,
                                              CheckNumericsResult* __restrict__ abnormal,
                                              bool computeStats)
{
    check_numerics<miopen_f8::hip_f8<miopen_f8::hip_f8_type::bf8>, float>(
        reinterpret_cast<const miopen_f8::hip_f8<miopen_f8::hip_f8_type::bf8>*>(C_d),
        sz,
        abnormal,
        computeStats);
}
