// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "light_gpu.h"
#include "q3mapx/area_factors.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <mutex>

namespace q3mapx {
namespace {
bool enabled = false;
int device = -1;
const char* reportPath = nullptr;
std::unique_ptr<AreaFactorComputer>& computer(){
    // Initialized after the OpenCL driver by beginLightingGpu, so even a legacy
    // fatal exit destroys device resources before unloading the driver.
    static std::unique_ptr<AreaFactorComputer> value;
    return value;
}
std::vector<int> candidates;
std::mutex computeMutex;
ComputeReport totals;
uint64_t batches = 0, factors = 0, fallbacks = 0;

std::array<float,4> packed(const Vector3& v, float w=0){ return { v[0],v[1],v[2],w }; }
}

void parseLightingGpuOptions(Args& args){
    while (args.takeArg("-light-backend")) {
        const char* value = args.takeNext();
        if (!strEqual(value,"cpu") && !strEqual(value,"gpu")) Error("Light backend must be cpu or gpu");
        enabled = strEqual(value,"gpu");
    }
    while (args.takeArg("-gpu-device")) device = ParseIntegerOption("-gpu-device",args.takeNext(),0,1023);
    while (args.takeArg("-compute-report")) {
        reportPath = args.takeNext();
        if (!path_extension_is(reportPath,"json")) Error("Compute report must have a .json extension");
    }
}
void beginLightingGpu(){
    if (!enabled) { totals.reason = "CPU lighting selected"; return; }
    auto initialized = createAreaFactorComputer(device,totals);
    computer() = std::move(initialized);
    if (!computer()) Error("GPU lighting initialization failed: %s",totals.reason.c_str());
    Sys_Printf("Experimental GPU area-light factors: %s\n",totals.device.c_str());
}
bool lightingGpuEnabled(){ return enabled; }
void prepareLightingGpuPass(){
    candidates.clear();
    if (!enabled || !computer() || faster) return;
    // A stable selection bounds aggregate host storage across workers (at most
    // four 64 MiB result caches). Device transfers/dispatches are serialized.
    for (int i=0; i<numRawLightmaps; ++i) {
        const size_t count = size_t(rawLightmaps[i].sw)*rawLightmaps[i].sh;
        if (count >= 64 && count <= 1048576) candidates.push_back(i);
    }
    std::sort(candidates.begin(),candidates.end(),[](int a,int b){
        const size_t x=size_t(rawLightmaps[a].sw)*rawLightmaps[a].sh, y=size_t(rawLightmaps[b].sw)*rawLightmaps[b].sh;
        return x == y ? a < b : x > y;
    });
    if (candidates.size() > 4) candidates.resize(4);
}
LightFactorCache cacheLightFactors(int rawLightmapNum, const rawLightmap_t& lm, const trace_t& trace){
    LightFactorCache cache;
    if (!enabled || !computer() || std::find(candidates.begin(),candidates.end(),rawLightmapNum) == candidates.end()) return cache;
    const size_t count = size_t(lm.sw)*lm.sh;
    try {
        cache.offsets.assign(trace.numLights,-1);
        cache.samples.resize(count);
        for (size_t i=0; i<count; ++i) {
            // Unmapped slots may contain uninitialized positions/normals. Do not
            // read them; the kernel explicitly rejects the zero mapped flag.
            if (lm.superClusters[i] >= CLUSTER_NORMAL) cache.samples[i] = { packed(lm.superOrigins[i],1), packed(lm.superNormals[i]) };
            else cache.samples[i] = {};
        }
        cache.trace = &trace;
    } catch (const std::bad_alloc&) {
        cache = {};
        std::lock_guard lock(computeMutex);
        ++fallbacks; totals.reason = "Host area-factor allocation failed; CPU used";
        return {};
    }
    return cache;
}
void LightFactorCache::prepare(int light){
    if (!trace || light < nextLight || trace->lights[light]->type != ELightType::Area) return;
    std::fill(offsets.begin(),offsets.end(),-1);
    const size_t count = samples.size();
    try {
        std::vector<AreaFactorLight> lights;
        std::vector<AreaFactorVertex> vertices;
        // Stream groups of lights through bounded result storage, including a
        // large lightmap whose complete light/sample matrix would not fit.
        for (nextLight=light; nextLight<trace->numLights; ++nextLight) {
            const auto& l = *trace->lights[nextLight];
            if (l.type != ELightType::Area || l.w.size() > 1024) continue;
            if (lights.size()+1 > (16u*1024*1024)/count || vertices.size()+l.w.size() > 1048576 || lights.size() >= 65536) break;
            offsets[nextLight] = int(lights.size()*count);
            lights.push_back({ packed(l.origin,l.envelope), packed(l.normal,l.dist), { uint32_t(vertices.size()),uint32_t(l.w.size()),0,0 } });
            for (const auto& p : l.w) vertices.push_back(packed(p));
        }
        if (lights.empty()) return;
        std::lock_guard lock(computeMutex);
        ComputeReport report;
        if (!computer()->compute(samples,lights,vertices,values,report)) {
            ++fallbacks; totals.reason = report.reason;
            std::fill(offsets.begin(),offsets.end(),-1); return;
        }
        totals.usedGPU = true;
        totals.transferSeconds += report.transferSeconds;
        totals.kernelSeconds += report.kernelSeconds;
        totals.totalSeconds += report.totalSeconds;
        ++batches; factors += values.size();
    } catch (const std::bad_alloc&) {
        std::vector<float>().swap(values);
        std::vector<int>().swap(offsets);
        std::vector<AreaFactorSample>().swap(samples);
        trace = nullptr;
        std::lock_guard lock(computeMutex);
        ++fallbacks; totals.reason = "Host area-factor allocation failed; CPU used";
    }
}
void finishLightingGpu(){
    if (enabled) {
        Sys_Printf("GPU area factors: %llu batches, %llu factors, %.3f s setup, %.3f s kernels, %.3f s transfer\n",
            static_cast<unsigned long long>(batches),static_cast<unsigned long long>(factors),
            totals.setupSeconds,totals.kernelSeconds,totals.transferSeconds);
        if (!batches && totals.reason.empty()) totals.reason = "No eligible area-light batches; CPU used";
        if (!totals.reason.empty()) Sys_Printf("GPU lighting note: %s\n",totals.reason.c_str());
    }
    if (reportPath) {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("schema_version"); w.Int(1);
        w.Key("requested_backend"); w.String(enabled ? "gpu" : "cpu");
        w.Key("backend"); w.String(totals.usedGPU ? "hybrid" : "cpu");
        w.Key("workload"); w.String("initial-area-light-polygon-factors");
        w.Key("material_tracing"); w.String("cpu");
        w.Key("device"); w.String(totals.device.c_str());
        w.Key("reason"); w.String(totals.reason.c_str());
        w.Key("batches"); w.Uint64(batches);
        w.Key("factors"); w.Uint64(factors);
        w.Key("fallback_batches"); w.Uint64(fallbacks);
        w.Key("setup_seconds"); w.Double(totals.setupSeconds);
        w.Key("transfer_seconds"); w.Double(totals.transferSeconds);
        w.Key("kernel_seconds"); w.Double(totals.kernelSeconds);
        w.Key("compute_seconds"); w.Double(totals.totalSeconds);
        w.EndObject();
        SaveFile(reportPath,buffer.GetString(),int(buffer.GetSize()));
    }
    if (enabled) computer().reset();
}
}
