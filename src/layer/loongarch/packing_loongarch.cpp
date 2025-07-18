// yala is pleased to support the open source community by making ncnn available.
//
//
// Copyright (C) 2022 yala <zhaojunchao@loongson.cn>;<junchao82@qq.com>. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "packing_loongarch.h"

#if __loongarch_sx
#include <lsxintrin.h>
#endif // __loongarch_sx

namespace ncnn {

Packing_loongarch::Packing_loongarch()
{
    support_packing = true;
}

int Packing_loongarch::forward(const Mat& bottom_blob, Mat& top_blob, const Option& opt) const
{
    int elembits = bottom_blob.elembits();

    if (elembits == 8)
        return forward_int8(bottom_blob, top_blob, opt);

    if (use_padding)
    {
        return Packing::forward(bottom_blob, top_blob, opt);
    }

    if (elembits != 32)
    {
        // non-fp32 type
        return Packing::forward(bottom_blob, top_blob, opt);
    }

    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;

    if (elempack == out_elempack)
    {
        top_blob = bottom_blob;
        return 0;
    }

    bool pack1to4 = elempack == 1 && out_elempack == 4;
    bool pack4to1 = elempack == 4 && out_elempack == 1;

    if (!pack1to4 && !pack4to1)
    {
        return Packing::forward(bottom_blob, top_blob, opt);
    }

    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int d = bottom_blob.d;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;

    if (!use_padding)
    {
        // identity if use_padding not allowed
        if (dims == 1 && w * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if (dims == 2 && h * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if ((dims == 3 || dims == 4) && channels * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
    }

    if (dims == 1)
    {
        top_blob = bottom_blob;
        top_blob.w = w * elempack / out_elempack;
        top_blob.cstep = bottom_blob.cstep * elempack / out_elempack;
        top_blob.elemsize = elemsize / elempack * out_elempack;
        top_blob.elempack = out_elempack;
        return 0;
    }

    if (dims == 2)
    {
        int outh = h * elempack / out_elempack;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        top_blob.create(w, outh, out_elemsize, out_elempack, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (pack1to4)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i = 0; i < outh; i++)
            {
                const float* r0 = bottom_blob.row(i * 4);
                const float* r1 = bottom_blob.row(i * 4 + 1);
                const float* r2 = bottom_blob.row(i * 4 + 2);
                const float* r3 = bottom_blob.row(i * 4 + 3);

                float* outptr = top_blob.row(i);

                int j = 0;
#if __loongarch_sx
                for (; j + 3 < w; j += 4)
                {
                    // transpose 4x4
                    __m128i _r0 = __lsx_vld(r0, 0);
                    __m128i _r1 = __lsx_vld(r1, 0);
                    __m128i _r2 = __lsx_vld(r2, 0);
                    __m128i _r3 = __lsx_vld(r3, 0);

                    __m128i _r01r = __lsx_vilvl_w(_r1, _r0);
                    __m128i _r01l = __lsx_vilvh_w(_r1, _r0);
                    __m128i _r23r = __lsx_vilvl_w(_r3, _r2);
                    __m128i _r23l = __lsx_vilvh_w(_r3, _r2);
                    __m128i _r0123_0 = __lsx_vilvl_d(_r23r, _r01r);
                    __m128i _r0123_1 = __lsx_vilvh_d(_r23r, _r01r);
                    __m128i _r0123_2 = __lsx_vilvl_d(_r23l, _r01l);
                    __m128i _r0123_3 = __lsx_vilvh_d(_r23l, _r01l);

                    __lsx_vst(_r0123_0, outptr, 0);
                    __lsx_vst(_r0123_1, outptr + 4, 0);
                    __lsx_vst(_r0123_2, outptr + 4 * 2, 0);
                    __lsx_vst(_r0123_3, outptr + 4 * 3, 0);

                    r0 += 4;
                    r1 += 4;
                    r2 += 4;
                    r3 += 4;
                    outptr += 16;
                }
#endif // __loongarch_sx
                for (; j < w; j++)
                {
                    outptr[0] = *r0++;
                    outptr[1] = *r1++;
                    outptr[2] = *r2++;
                    outptr[3] = *r3++;

                    outptr += 4;
                }
            }
        }
        if (pack4to1)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i = 0; i < h; i++)
            {
                const float* r0 = bottom_blob.row(i);

                float* outptr0 = top_blob.row(i * 4);
                float* outptr1 = top_blob.row(i * 4 + 1);
                float* outptr2 = top_blob.row(i * 4 + 2);
                float* outptr3 = top_blob.row(i * 4 + 3);

                int j = 0;
#if __loongarch_sx
                for (; j + 3 < w; j += 4)
                {
                    // transpose 4x4
                    __m128i _r0 = __lsx_vld(r0, 0);
                    __m128i _r1 = __lsx_vld(r0 + 4, 0);
                    __m128i _r2 = __lsx_vld(r0 + 4 * 2, 0);
                    __m128i _r3 = __lsx_vld(r0 + 4 * 3, 0);

                    __m128i _r01r = __lsx_vilvl_w(_r1, _r0);
                    __m128i _r01l = __lsx_vilvh_w(_r1, _r0);
                    __m128i _r23r = __lsx_vilvl_w(_r3, _r2);
                    __m128i _r23l = __lsx_vilvh_w(_r3, _r2);
                    __m128i _r0123_0 = __lsx_vilvl_d(_r23r, _r01r);
                    __m128i _r0123_1 = __lsx_vilvh_d(_r23r, _r01r);
                    __m128i _r0123_2 = __lsx_vilvl_d(_r23l, _r01l);
                    __m128i _r0123_3 = __lsx_vilvh_d(_r23l, _r01l);

                    __lsx_vst(_r0123_0, outptr0, 0);
                    __lsx_vst(_r0123_1, outptr1, 0);
                    __lsx_vst(_r0123_2, outptr2, 0);
                    __lsx_vst(_r0123_3, outptr3, 0);

                    r0 += 16;
                    outptr0 += 4;
                    outptr1 += 4;
                    outptr2 += 4;
                    outptr3 += 4;
                }
#endif // __loongarch_sx
                for (; j < w; j++)
                {
                    *outptr0++ = r0[0];
                    *outptr1++ = r0[1];
                    *outptr2++ = r0[2];
                    *outptr3++ = r0[3];

                    r0 += 4;
                }
            }
        }

        return 0;
    }

    if (dims == 3 || dims == 4)
    {
        int size = w * h * d;
        int outc = channels * elempack / out_elempack;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        if (dims == 3)
            top_blob.create(w, h, outc, out_elemsize, out_elempack, opt.blob_allocator);
        else // if (dims == 4)
            top_blob.create(w, h, d, outc, out_elemsize, out_elempack, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (pack1to4)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q = 0; q < outc; q++)
            {
                const float* r0 = bottom_blob.channel(q * 4);
                const float* r1 = bottom_blob.channel(q * 4 + 1);
                const float* r2 = bottom_blob.channel(q * 4 + 2);
                const float* r3 = bottom_blob.channel(q * 4 + 3);

                float* outptr = top_blob.channel(q);

                int i = 0;
#if __loongarch_sx
                for (; i + 3 < size; i += 4)
                {
                    // transpose 4x4
                    __m128i _r0 = __lsx_vld(r0, 0);
                    __m128i _r1 = __lsx_vld(r1, 0);
                    __m128i _r2 = __lsx_vld(r2, 0);
                    __m128i _r3 = __lsx_vld(r3, 0);

                    __m128i _r01r = __lsx_vilvl_w(_r1, _r0);
                    __m128i _r01l = __lsx_vilvh_w(_r1, _r0);
                    __m128i _r23r = __lsx_vilvl_w(_r3, _r2);
                    __m128i _r23l = __lsx_vilvh_w(_r3, _r2);
                    __m128i _r0123_0 = __lsx_vilvl_d(_r23r, _r01r);
                    __m128i _r0123_1 = __lsx_vilvh_d(_r23r, _r01r);
                    __m128i _r0123_2 = __lsx_vilvl_d(_r23l, _r01l);
                    __m128i _r0123_3 = __lsx_vilvh_d(_r23l, _r01l);

                    __lsx_vst(_r0123_0, outptr, 0);
                    __lsx_vst(_r0123_1, outptr + 4, 0);
                    __lsx_vst(_r0123_2, outptr + 4 * 2, 0);
                    __lsx_vst(_r0123_3, outptr + 4 * 3, 0);

                    r0 += 4;
                    r1 += 4;
                    r2 += 4;
                    r3 += 4;
                    outptr += 16;
                }
#endif // __loongarch_sx
                for (; i < size; i++)
                {
                    outptr[0] = *r0++;
                    outptr[1] = *r1++;
                    outptr[2] = *r2++;
                    outptr[3] = *r3++;

                    outptr += 4;
                }
            }
        }
        if (pack4to1)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q = 0; q < channels; q++)
            {
                const float* r0 = bottom_blob.channel(q);

                float* outptr0 = top_blob.channel(q * 4);
                float* outptr1 = top_blob.channel(q * 4 + 1);
                float* outptr2 = top_blob.channel(q * 4 + 2);
                float* outptr3 = top_blob.channel(q * 4 + 3);

                int i = 0;
#if __loongarch_sx
                for (; i + 3 < size; i += 4)
                {
                    // transpose 4x4
                    __m128i _r0 = __lsx_vld(r0, 0);
                    __m128i _r1 = __lsx_vld(r0 + 4, 0);
                    __m128i _r2 = __lsx_vld(r0 + 4 * 2, 0);
                    __m128i _r3 = __lsx_vld(r0 + 4 * 3, 0);

                    __m128i _r01r = __lsx_vilvl_w(_r1, _r0);
                    __m128i _r01l = __lsx_vilvh_w(_r1, _r0);
                    __m128i _r23r = __lsx_vilvl_w(_r3, _r2);
                    __m128i _r23l = __lsx_vilvh_w(_r3, _r2);
                    __m128i _r0123_0 = __lsx_vilvl_d(_r23r, _r01r);
                    __m128i _r0123_1 = __lsx_vilvh_d(_r23r, _r01r);
                    __m128i _r0123_2 = __lsx_vilvl_d(_r23l, _r01l);
                    __m128i _r0123_3 = __lsx_vilvh_d(_r23l, _r01l);

                    __lsx_vst(_r0123_0, outptr0, 0);
                    __lsx_vst(_r0123_1, outptr1, 0);
                    __lsx_vst(_r0123_2, outptr2, 0);
                    __lsx_vst(_r0123_3, outptr3, 0);

                    r0 += 16;
                    outptr0 += 4;
                    outptr1 += 4;
                    outptr2 += 4;
                    outptr3 += 4;
                }
#endif // __loongarch_sx
                for (; i < size; i++)
                {
                    *outptr0++ = r0[0];
                    *outptr1++ = r0[1];
                    *outptr2++ = r0[2];
                    *outptr3++ = r0[3];

                    r0 += 4;
                }
            }
        }

        return 0;
    }

    return 0;
}

int Packing_loongarch::forward_int8(const Mat& bottom_blob, Mat& top_blob, const Option& opt) const
{
    if (use_padding)
    {
        return Packing::forward(bottom_blob, top_blob, opt);
    }

    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;

    if (elempack == out_elempack)
    {
        top_blob = bottom_blob;
        return 0;
    }

    bool pack1to8 = elempack == 1 && out_elempack == 8;
    bool pack8to1 = elempack == 8 && out_elempack == 1;

    if (!pack1to8 && !pack8to1)
    {
        return Packing::forward(bottom_blob, top_blob, opt);
    }

    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int d = bottom_blob.d;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;

    if (!use_padding)
    {
        // identity if use_padding not allowed
        if (dims == 1 && w * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if (dims == 2 && h * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if ((dims == 3 || dims == 4) && channels * elempack % out_elempack != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
    }

    if (dims == 1)
    {
        top_blob = bottom_blob;
        top_blob.w = w * elempack / out_elempack;
        top_blob.cstep = bottom_blob.cstep * elempack / out_elempack;
        top_blob.elemsize = elemsize / elempack * out_elempack;
        top_blob.elempack = out_elempack;
        return 0;
    }

    if (dims == 2)
    {
        int outh = h * elempack / out_elempack;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        top_blob.create(w, outh, out_elemsize, out_elempack, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (pack1to8)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i = 0; i < outh; i++)
            {
                const signed char* r0 = bottom_blob.row<const signed char>(i * 8);
                const signed char* r1 = bottom_blob.row<const signed char>(i * 8 + 1);
                const signed char* r2 = bottom_blob.row<const signed char>(i * 8 + 2);
                const signed char* r3 = bottom_blob.row<const signed char>(i * 8 + 3);
                const signed char* r4 = bottom_blob.row<const signed char>(i * 8 + 4);
                const signed char* r5 = bottom_blob.row<const signed char>(i * 8 + 5);
                const signed char* r6 = bottom_blob.row<const signed char>(i * 8 + 6);
                const signed char* r7 = bottom_blob.row<const signed char>(i * 8 + 7);

                signed char* outptr = top_blob.row<signed char>(i);

                int j = 0;
                for (; j < w; j++)
                {
                    outptr[0] = *r0++;
                    outptr[1] = *r1++;
                    outptr[2] = *r2++;
                    outptr[3] = *r3++;
                    outptr[4] = *r4++;
                    outptr[5] = *r5++;
                    outptr[6] = *r6++;
                    outptr[7] = *r7++;

                    outptr += 8;
                }
            }
        }
        if (pack8to1)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int i = 0; i < h; i++)
            {
                const signed char* r0 = bottom_blob.row<const signed char>(i);

                signed char* outptr0 = top_blob.row<signed char>(i * 8);
                signed char* outptr1 = top_blob.row<signed char>(i * 8 + 1);
                signed char* outptr2 = top_blob.row<signed char>(i * 8 + 2);
                signed char* outptr3 = top_blob.row<signed char>(i * 8 + 3);
                signed char* outptr4 = top_blob.row<signed char>(i * 8 + 4);
                signed char* outptr5 = top_blob.row<signed char>(i * 8 + 5);
                signed char* outptr6 = top_blob.row<signed char>(i * 8 + 6);
                signed char* outptr7 = top_blob.row<signed char>(i * 8 + 7);

                int j = 0;
                for (; j < w; j++)
                {
                    *outptr0++ = r0[0];
                    *outptr1++ = r0[1];
                    *outptr2++ = r0[2];
                    *outptr3++ = r0[3];
                    *outptr4++ = r0[4];
                    *outptr5++ = r0[5];
                    *outptr6++ = r0[6];
                    *outptr7++ = r0[7];

                    r0 += 8;
                }
            }
        }

        return 0;
    }

    if (dims == 3 || dims == 4)
    {
        int size = w * h * d;
        int outc = channels * elempack / out_elempack;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        if (dims == 3)
            top_blob.create(w, h, outc, out_elemsize, out_elempack, opt.blob_allocator);
        else // if (dims == 4)
            top_blob.create(w, h, d, outc, out_elemsize, out_elempack, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (pack1to8)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q = 0; q < outc; q++)
            {
                const signed char* r0 = bottom_blob.channel(q * 8);
                const signed char* r1 = bottom_blob.channel(q * 8 + 1);
                const signed char* r2 = bottom_blob.channel(q * 8 + 2);
                const signed char* r3 = bottom_blob.channel(q * 8 + 3);
                const signed char* r4 = bottom_blob.channel(q * 8 + 4);
                const signed char* r5 = bottom_blob.channel(q * 8 + 5);
                const signed char* r6 = bottom_blob.channel(q * 8 + 6);
                const signed char* r7 = bottom_blob.channel(q * 8 + 7);

                signed char* outptr = top_blob.channel(q);

                int i = 0;
                for (; i < size; i++)
                {
                    outptr[0] = *r0++;
                    outptr[1] = *r1++;
                    outptr[2] = *r2++;
                    outptr[3] = *r3++;
                    outptr[4] = *r4++;
                    outptr[5] = *r5++;
                    outptr[6] = *r6++;
                    outptr[7] = *r7++;

                    outptr += 8;
                }
            }
        }
        if (pack8to1)
        {
            #pragma omp parallel for num_threads(opt.num_threads)
            for (int q = 0; q < channels; q++)
            {
                const signed char* r0 = bottom_blob.channel(q);

                signed char* outptr0 = top_blob.channel(q * 8);
                signed char* outptr1 = top_blob.channel(q * 8 + 1);
                signed char* outptr2 = top_blob.channel(q * 8 + 2);
                signed char* outptr3 = top_blob.channel(q * 8 + 3);
                signed char* outptr4 = top_blob.channel(q * 8 + 4);
                signed char* outptr5 = top_blob.channel(q * 8 + 5);
                signed char* outptr6 = top_blob.channel(q * 8 + 6);
                signed char* outptr7 = top_blob.channel(q * 8 + 7);

                int i = 0;
                for (; i < size; i++)
                {
                    *outptr0++ = r0[0];
                    *outptr1++ = r0[1];
                    *outptr2++ = r0[2];
                    *outptr3++ = r0[3];
                    *outptr4++ = r0[4];
                    *outptr5++ = r0[5];
                    *outptr6++ = r0[6];
                    *outptr7++ = r0[7];

                    r0 += 8;
                }
            }
        }

        return 0;
    }

    return 0;
}

} // namespace ncnn
