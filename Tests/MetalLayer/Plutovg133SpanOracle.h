/*
The packed integer span blend helpers in MetalSpanCompositeShaders.h derive
from https://github.com/sammycage/plutovg/blob/v1.3.3/source/plutovg-blend.c
(BYTE_MUL and source/source-over composition functions). They retain packed
integer carry and wrapping behavior, including the separate array fast paths.

MIT License

Copyright (c) 2020-2026 Samuel Ugochukwu <sammycageagle@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#pragma once
#include <cstdint>
namespace plutovg133_oracle {
static uint32_t plutovg_alpha(uint32_t c) {return c >> 24;}
static void plutovg_memfill32(uint32_t* d,int n,uint32_t c) {while(n--) *d++=c;}
static inline uint32_t BYTE_MUL(uint32_t x, uint32_t a)
{
    uint32_t t = (x & 0xff00ff) * a;
    t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff00ff) * a;
    x = (x + ((x >> 8) & 0xff00ff) + 0x800080);
    x &= 0xff00ff00;
    x |= t;
    return x;
}
static void composition_solid_source(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if(const_alpha == 255) {
        plutovg_memfill32(dest, length, color);
    } else {
        uint32_t ialpha = 255 - const_alpha;
        color = BYTE_MUL(color, const_alpha);
        for(int i = 0; i < length; i++) {
            dest[i] = color + BYTE_MUL(dest[i], ialpha);
        }
    }
}
static void composition_solid_source_over(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if(const_alpha != 255)
        color = BYTE_MUL(color, const_alpha);
    uint32_t ialpha = 255 - plutovg_alpha(color);
    for(int i = 0; i < length; i++) {
        dest[i] = color + BYTE_MUL(dest[i], ialpha);
    }
}
static void composition_source_over(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if(const_alpha == 255) {
        for(int i = 0; i < length; i++) {
            uint32_t s = src[i];
            if(s >= 0xff000000) {
                dest[i] = s;
            } else if(s != 0) {
                dest[i] = s + BYTE_MUL(dest[i], plutovg_alpha(~s));
            }
        }
    } else {
        for(int i = 0; i < length; i++) {
            uint32_t s = BYTE_MUL(src[i], const_alpha);
            dest[i] = s + BYTE_MUL(dest[i], plutovg_alpha(~s));
        }
    }
}
}
