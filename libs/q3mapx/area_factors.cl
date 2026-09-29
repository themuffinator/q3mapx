// SPDX-License-Identifier: GPL-3.0-or-later
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#pragma OPENCL FP_CONTRACT OFF
typedef struct { float4 origin, normal; } Sample;
typedef struct { float4 origin, plane; uint4 winding; } Light;

// Preserve NRC's mixed precision and explicit operation order. OpenCL dot/cross
// builtins may contract or reassociate operations, so spell these out.
float dot_f(float3 a, float3 b) { return (a.x*b.x + a.y*b.y) + a.z*b.z; }
float3 cross_f(float3 a, float3 b) {
    return (float3)(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
float normalize_nrc(float3* v) {
    double3 d = convert_double3(*v);
    double len = sqrt((d.x*d.x + d.y*d.y) + d.z*d.z);
    *v = len == 0 ? (float3)(0) : convert_float3(d / len);
    return (float)len;
}
__kernel void area_factors(__global const Sample* samples, __global const Light* lights,
                          __global const float4* vertices, __global float* output,
                          uint sampleCount, uint first, uint count) {
    uint lane = get_global_id(0);
    if (lane >= count) return;
    uint index = first + lane;
    Sample s = samples[index % sampleCount];
    Light l = lights[index / sampleCount];
    output[index] = 0;
    if (s.origin.w == 0 || l.winding.y < 3) return;
    float3 direction = l.origin.xyz - s.origin.xyz;
    if (normalize_nrc(&direction) >= l.origin.w) return;
    float d = (float)((double)dot_f(s.origin.xyz,l.plane.xyz) - (double)l.plane.w);
    float3 point = s.origin.xyz;
    if (d > -8.0f && d < 8.0f) point += l.plane.xyz * (8.0f-d);
    float3 firstDir = vertices[l.winding.x].xyz - point;
    normalize_nrc(&firstDir);
    float3 previous = firstDir;
    double total = 0;
    for (uint i=0; i<l.winding.y; ++i) {
        float3 next = firstDir;
        if (i+1 < l.winding.y) {
            next = vertices[l.winding.x+i+1].xyz - point;
            normalize_nrc(&next);
        }
        float3 tri = cross_f(previous,next);
        if (normalize_nrc(&tri) >= 0.0001f) {
            double angle = acos(clamp((double)dot_f(previous,next),-1.0,1.0));
            total += (double)dot_f(s.normal.xyz,tri) * angle;
            if (total > 6.3 || total < -6.3) return;
        }
        previous = next;
    }
    output[index] = (float)(total * 0.15915494309189533576888376337251);
}
