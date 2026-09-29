// SPDX-License-Identifier: GPL-3.0-or-later
#pragma OPENCL FP_CONTRACT OFF
typedef struct { float4 bounds; uint first, count, pad0, pad1; } Brush;
uint coordinate(float x, float minimum, float scale, uint grid) {
    return convert_uint(clamp((x - minimum) * scale, 0.0f, (float)(grid - 1)));
}
float random_value(uint* state) {
    *state += 0x9e3779b9u;
    uint value = *state;
    value = (value ^ (value >> 16)) * 0x85ebca6bu;
    value = (value ^ (value >> 13)) * 0xc2b2ae35u;
    value ^= value >> 16;
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}
float sample_column(float x, float y, __global const float4* planes, __global const Brush* brushes,
                    __global const uint2* cells, __global const uint* refs, float4 index, uint grid) {
    uint2 range = cells[coordinate(y,index.y,index.w,grid) * grid + coordinate(x,index.x,index.z,grid)];
    float value = 0;
    for (uint i = 0; i < range.y; ++i) {
        Brush b = brushes[refs[range.x + i]];
        if (x < b.bounds.x || y < b.bounds.y || x > b.bounds.z || y > b.bounds.w) continue;
        bool hasNear = false, hasFar = false, rejected = false;
        float near = 0, far = 0;
        for (uint j = 0; j < b.count; ++j) {
            float4 p = planes[b.first + j];
            float distance = x * p.x + y * p.y;
            if (p.z == 0) { if (distance > p.w) { rejected = true; break; } }
            else {
                float t = (p.w - distance) / p.z;
                if (p.z < 0) { if (!hasNear || t > near) near = t; hasNear = true; }
                else { if (!hasFar || t < far) far = t; hasFar = true; }
                if (hasNear && hasFar && near >= far) { rejected = true; break; }
            }
        }
        if (!rejected && hasNear && hasFar) value += far - near;
    }
    return value;
}
__kernel void columns(__global const float4* planes, __global const Brush* brushes,
                      __global const uint2* cells, __global const uint* refs,
                      __global const float2* offsets, __global float* output,
                      float4 index, uint grid, float4 area, float sizeZ,
                      uint width, uint height, uint samples, uint randomSamples, uint seed,
                      uint firstRow, __global const float* xbase, __global const float* ybase) {
    uint x = get_global_id(0), y = get_global_id(1) + firstRow;
    if (x >= width || y >= height) return;
    float dx = area.x, dy = area.y;
    float value = 0;
    if (samples <= 1) {
        float sx = xbase[x], sy = ybase[y];
        value = sample_column(sx,sy,planes,brushes,cells,refs,index,grid) / sizeZ;
    } else {
        float xmin = xbase[x], ymin = ybase[y];
        uint state = (y * width + x) ^ seed;
        for (uint i = 0; i < samples; ++i) {
            float2 uv;
            if (randomSamples) {
                do { uv.x = 2.0f * random_value(&state) - 1.0f; uv.y = 2.0f * random_value(&state) - 1.0f; }
                while (uv.x * uv.x + uv.y * uv.y > 1.0f);
                uv += (float2)(0.5f);
            } else uv = offsets[i];
            value += sample_column(xmin + uv.x * dx, ymin + uv.y * dy,planes,brushes,cells,refs,index,grid);
        }
        value /= (float)samples * sizeZ;
    }
    output[y * width + x] = value;
}
