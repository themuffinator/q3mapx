// SPDX-License-Identifier: GPL-3.0-or-later
#include "compute.h"
#include "area_factors.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <stdexcept>

#if Q3MAPX_ENABLE_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include "columns_kernel.h"
#include "area_factors_kernel.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace q3mapx {
namespace {
using Clock = std::chrono::steady_clock;
double seconds( Clock::time_point start ) { return std::chrono::duration<double>(Clock::now()-start).count(); }
void check( cl_int result, const char* action ) {
    if ( result != CL_SUCCESS ) throw std::runtime_error(std::string(action) + " (OpenCL " + std::to_string(result) + ")");
}
#define CL_FUNCTIONS(X) \
 X(clGetPlatformIDs) X(clGetPlatformInfo) X(clGetDeviceIDs) X(clGetDeviceInfo) X(clCreateContext) X(clReleaseContext) \
 X(clCreateCommandQueue) X(clReleaseCommandQueue) X(clCreateProgramWithSource) X(clBuildProgram) \
 X(clGetProgramBuildInfo) X(clReleaseProgram) X(clCreateKernel) X(clReleaseKernel) X(clSetKernelArg) \
 X(clCreateBuffer) X(clReleaseMemObject) X(clEnqueueNDRangeKernel) X(clEnqueueReadBuffer) X(clFinish) \
 X(clGetEventProfilingInfo) X(clReleaseEvent)
struct Driver {
#ifdef _WIN32
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
#define DECLARE(name) decltype(&::name) name = nullptr;
    CL_FUNCTIONS(DECLARE)
#undef DECLARE
    Driver(){
#ifdef _WIN32
        library = LoadLibraryExW(L"OpenCL.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        library = dlopen("libOpenCL.so.1",RTLD_NOW | RTLD_LOCAL);
        if ( !library ) library = dlopen("libOpenCL.so",RTLD_NOW | RTLD_LOCAL);
#endif
        if ( !library ) throw std::runtime_error("OpenCL runtime unavailable");
        try {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(symbol(#name));
            CL_FUNCTIONS(LOAD)
#undef LOAD
        } catch (...) { unload(); throw; }
    }
    void* symbol(const char* name){
#ifdef _WIN32
        auto value = reinterpret_cast<void*>(GetProcAddress(library,name));
#else
        auto value = dlsym(library,name);
#endif
        if (!value) throw std::runtime_error(std::string("Missing OpenCL entry point: ") + name);
        return value;
    }
    void unload(){
#ifdef _WIN32
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
        library = nullptr;
    }
    ~Driver(){ unload(); }
};
Driver& driver(){
    if (const char* disabled = std::getenv("Q3MAPX_DISABLE_GPU"); disabled && std::string(disabled) == "1")
        throw std::runtime_error("GPU disabled by Q3MAPX_DISABLE_GPU=1");
    static Driver instance;
    return instance;
}
template<class T> T info(Driver& d,cl_device_id device,cl_device_info field){
    T result{}; check(d.clGetDeviceInfo(device,field,sizeof(result),&result,nullptr),"Reading device information"); return result;
}
std::string stringInfo(Driver& d,cl_device_id device,cl_device_info field){
    size_t size = 0; check(d.clGetDeviceInfo(device,field,0,nullptr,&size),"Reading device string size");
    if (!size || size > 65536) throw std::runtime_error("Invalid OpenCL device string length");
    std::string text(size,'\0'); check(d.clGetDeviceInfo(device,field,size,text.data(),nullptr),"Reading device string");
    if (text.back() == '\0') text.pop_back(); return text;
}
struct Device { cl_device_id handle; ComputeDevice details; };
std::vector<Device> enumerate(Driver& d, bool automatic = false){
    cl_uint count = 0;
    check(d.clGetPlatformIDs(0,nullptr,&count),"Finding OpenCL platforms");
    if (!count || count > 256) throw std::runtime_error("No usable OpenCL platforms");
    std::vector<cl_platform_id> platforms(count);
    check(d.clGetPlatformIDs(count,platforms.data(),nullptr),"Reading OpenCL platforms");
    std::vector<Device> devices;
    for (auto platform : platforms) {
        // Avoid initializing translation layers that duplicate already usable native GPUs.
        // Full enumeration (and explicit indices) retains every device for diagnostics.
        char vendor[256]{};
        if (automatic && !devices.empty() && d.clGetPlatformInfo(platform,CL_PLATFORM_VENDOR,sizeof(vendor),vendor,nullptr) == CL_SUCCESS
            && std::string(vendor).find("Microsoft") != std::string::npos) continue;
        cl_uint n = 0;
        const auto result = d.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,0,nullptr,&n);
        if (result == CL_DEVICE_NOT_FOUND) continue;
        check(result,"Finding GPU devices");
        if (n > 1024) throw std::runtime_error("Invalid GPU device count");
        std::vector<cl_device_id> handles(n);
        if (!n) continue;
        check(d.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,n,handles.data(),nullptr),"Reading GPU devices");
        for (auto handle : handles) {
            if (!info<cl_bool>(d,handle,CL_DEVICE_AVAILABLE) || !info<cl_bool>(d,handle,CL_DEVICE_COMPILER_AVAILABLE)) continue;
            ComputeDevice device{int(devices.size()),stringInfo(d,handle,CL_DEVICE_NAME),stringInfo(d,handle,CL_DEVICE_VENDOR),
                stringInfo(d,handle,CL_DEVICE_VERSION),info<cl_ulong>(d,handle,CL_DEVICE_GLOBAL_MEM_SIZE),
                info<cl_uint>(d,handle,CL_DEVICE_MAX_COMPUTE_UNITS),info<cl_bool>(d,handle,CL_DEVICE_HOST_UNIFIED_MEMORY) != 0};
            devices.push_back({handle,std::move(device)});
        }
    }
    return devices;
}
struct Session {
    Driver& d;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    std::vector<cl_mem> buffers;
    std::vector<cl_event> events;
    explicit Session(Driver& driver) : d(driver) {}
    void clearWork(){
        for (auto event : events) d.clReleaseEvent(event);
        for (auto buffer : buffers) d.clReleaseMemObject(buffer);
        events.clear(); buffers.clear();
    }
    ~Session(){
        clearWork();
        if (kernel) d.clReleaseKernel(kernel);
        if (program) d.clReleaseProgram(program);
        if (queue) d.clReleaseCommandQueue(queue);
        if (context) d.clReleaseContext(context);
    }
    cl_mem buffer(size_t bytes,const void* data){
        const uint32_t empty = 0;
        if (!bytes) { bytes = sizeof(empty); data = &empty; }
        cl_int error;
        auto result = d.clCreateBuffer(context,data ? CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR : CL_MEM_WRITE_ONLY,
                                       bytes,const_cast<void*>(data),&error);
        check(error,"Allocating/transferring GPU buffer");
        try { buffers.push_back(result); }
        catch (...) { d.clReleaseMemObject(result); throw; }
        return result;
    }
    void trackEvent(cl_event event){
        try { events.push_back(event); }
        catch (...) { d.clReleaseEvent(event); throw; }
    }
    template<class T> void argument(cl_uint index,const T& value){ check(d.clSetKernelArg(kernel,index,sizeof(value),&value),"Setting GPU argument"); }
};
}

std::vector<ComputeDevice> computeDevices( std::string& reason ){
    reason.clear();
    try {
        auto& d = driver();
        std::vector<ComputeDevice> result;
        for (auto& device : enumerate(d)) result.push_back(std::move(device.details));
        if (result.empty()) reason = "No usable OpenCL GPU found";
        return result;
    } catch (const std::exception& error) { reason = error.what(); return {}; }
}

struct AreaFactorComputer::Impl {
    Session session;
    Device device;
    Impl(Driver& driver, Device selected) : session(driver), device(std::move(selected)) {}
};
AreaFactorComputer::AreaFactorComputer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AreaFactorComputer::~AreaFactorComputer() = default;

std::unique_ptr<AreaFactorComputer> createAreaFactorComputer(int deviceIndex, ComputeReport& report){
    report = {};
    const auto start = Clock::now();
    try {
        auto& d = driver();
        auto devices = enumerate(d,deviceIndex == -1);
        if (devices.empty()) throw std::runtime_error("No usable OpenCL GPU found");
        if (deviceIndex >= int(devices.size()) || deviceIndex < -1) throw std::runtime_error("GPU device index out of range");
        if (deviceIndex == -1) {
            for (size_t i=0; i<devices.size(); ++i) {
                if (!info<cl_device_fp_config>(d,devices[i].handle,CL_DEVICE_DOUBLE_FP_CONFIG)) continue;
                if (deviceIndex == -1 || (!devices[i].details.unifiedMemory && devices[deviceIndex].details.unifiedMemory)
                    || (devices[i].details.unifiedMemory == devices[deviceIndex].details.unifiedMemory
                        && devices[i].details.computeUnits > devices[deviceIndex].details.computeUnits)) deviceIndex = int(i);
            }
            if (deviceIndex == -1) throw std::runtime_error("GPU area factors require double precision");
        }
        auto impl = std::make_unique<AreaFactorComputer::Impl>(d,devices[deviceIndex]);
        const auto& selected = impl->device;
        if (!info<cl_device_fp_config>(d,selected.handle,CL_DEVICE_DOUBLE_FP_CONFIG))
            throw std::runtime_error("Selected GPU does not support double precision");
        report.device = selected.details.name;
        auto& session = impl->session;
        cl_int error;
        session.context = d.clCreateContext(nullptr,1,&selected.handle,nullptr,nullptr,&error); check(error,"Creating area-factor context");
        session.queue = d.clCreateCommandQueue(session.context,selected.handle,CL_QUEUE_PROFILING_ENABLE,&error); check(error,"Creating area-factor queue");
        const char* source = areaFactorKernelSource;
        session.program = d.clCreateProgramWithSource(session.context,1,&source,nullptr,&error); check(error,"Creating area-factor program");
        error = d.clBuildProgram(session.program,1,&selected.handle,"-cl-std=CL1.2",nullptr,nullptr);
        if (error != CL_SUCCESS) {
            size_t length = 0;
            d.clGetProgramBuildInfo(session.program,selected.handle,CL_PROGRAM_BUILD_LOG,0,nullptr,&length);
            std::string log(std::min(length,size_t(65536)),'\0');
            if (!log.empty()) d.clGetProgramBuildInfo(session.program,selected.handle,CL_PROGRAM_BUILD_LOG,log.size(),log.data(),nullptr);
            throw std::runtime_error("Area-factor kernel build failed: " + log);
        }
        session.kernel = d.clCreateKernel(session.program,"area_factors",&error); check(error,"Creating area-factor kernel");
        report.setupSeconds = report.totalSeconds = seconds(start);
        return std::unique_ptr<AreaFactorComputer>(new AreaFactorComputer(std::move(impl)));
    } catch (const std::exception& error) { report.reason = error.what(); report.totalSeconds = seconds(start); return {}; }
}

bool AreaFactorComputer::compute(std::span<const AreaFactorSample> samples, std::span<const AreaFactorLight> lights,
                                 std::span<const AreaFactorVertex> vertices, std::vector<float>& output, ComputeReport& report){
    static_assert(sizeof(AreaFactorSample) == 32 && sizeof(AreaFactorLight) == 48 && sizeof(AreaFactorVertex) == 16);
    report = {}; output.clear();
    const auto start = Clock::now();
    auto& s = impl_->session;
    auto& d = s.d;
    try {
        if (samples.empty() || lights.empty() || samples.size() > 1048576 || lights.size() > 65536
            || vertices.size() > 1048576 || lights.size() > (16u * 1024 * 1024) / samples.size())
            throw std::invalid_argument("Area-factor batch exceeds bounded dimensions");
        uint32_t maxPoints = 3;
        for (const auto& l : lights) {
            if (l.winding[0] > vertices.size() || l.winding[1] > vertices.size()-l.winding[0] || l.winding[1] > 1024)
                throw std::invalid_argument("Invalid area-factor winding range");
            maxPoints = std::max(maxPoints,l.winding[1]);
            for (float v : l.origin) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite area light");
            for (float v : l.plane) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite area plane");
        }
        for (const auto& p : samples) {
            for (float v : p.origin) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite area sample");
            for (float v : p.normal) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite area normal");
        }
        for (const auto& p : vertices) for (float v : p) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite area vertex");
        const size_t bytes = samples.size() * lights.size() * sizeof(float);
        const auto limit = info<cl_ulong>(d,impl_->device.handle,CL_DEVICE_MAX_MEM_ALLOC_SIZE);
        for (size_t size : { bytes, samples.size_bytes(), lights.size_bytes(), vertices.size_bytes() })
            if (size > limit) throw std::runtime_error("Area-factor buffer exceeds device allocation limit");
        if (bytes + samples.size_bytes() + lights.size_bytes() + vertices.size_bytes() > impl_->device.details.memoryBytes / 4)
            throw std::runtime_error("Area-factor batch exceeds device memory budget");
        report.device = impl_->device.details.name;
        output.resize(samples.size()*lights.size());
        const auto upload = Clock::now();
        auto points = s.buffer(samples.size_bytes(),samples.data());
        auto emitters = s.buffer(lights.size_bytes(),lights.data());
        auto winding = s.buffer(vertices.size_bytes(),vertices.data());
        auto result = s.buffer(bytes,nullptr);
        s.argument(0,points); s.argument(1,emitters); s.argument(2,winding); s.argument(3,result);
        s.argument(4,uint32_t(samples.size()));
        report.transferSeconds = seconds(upload);
        const uint32_t batch = std::max(1u,256u*1024/maxPoints);
        for (uint32_t first=0; first<output.size(); first+=batch) {
            const uint32_t count = std::min(batch,uint32_t(output.size())-first);
            s.argument(5,first); s.argument(6,count);
            const size_t global = count;
            cl_event event = nullptr;
            check(d.clEnqueueNDRangeKernel(s.queue,s.kernel,1,nullptr,&global,nullptr,0,nullptr,&event),"Dispatching area factors");
            s.trackEvent(event);
        }
        check(d.clFinish(s.queue),"Waiting for area factors");
        for (auto event : s.events) {
            cl_ulong begin = 0,end = 0;
            check(d.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(begin),&begin,nullptr),"Profiling area-factor start");
            check(d.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(end),&end,nullptr),"Profiling area-factor end");
            report.kernelSeconds += double(end-begin)*1e-9;
        }
        const auto download = Clock::now();
        check(d.clEnqueueReadBuffer(s.queue,result,CL_TRUE,0,bytes,output.data(),0,nullptr,nullptr),"Reading area factors");
        report.transferSeconds += seconds(download);
        for (float value : output) if (!std::isfinite(value)) throw std::runtime_error("GPU returned non-finite area factors");
        report.usedGPU = true; report.totalSeconds = seconds(start);
        s.clearWork();
        return true;
    } catch (const std::exception& error) {
        d.clFinish(s.queue); s.clearWork(); output.clear();
        report.reason = error.what(); report.totalSeconds = seconds(start); return false;
    }
}

bool computeColumns( const ColumnScene& scene, const ColumnImage& image, int deviceIndex,
                     float* output, ComputeReport& report ){
    report = {};
    const auto start = Clock::now();
    try {
        if (!output || !image.width || !image.height || !image.samples || image.width > 8192 || image.height > 8192 || image.samples > 4096
            || !std::isfinite(image.sizeZ) || image.sizeZ <= 0 || scene.cells.empty())
            throw std::invalid_argument("Invalid GPU image request");
        auto& d = driver();
        auto devices = enumerate(d,deviceIndex == -1);
        if (devices.empty()) throw std::runtime_error("No usable OpenCL GPU found");
        if (deviceIndex >= int(devices.size()) || deviceIndex < -1) throw std::runtime_error("GPU device index out of range");
        if (deviceIndex == -1) {
            deviceIndex = 0;
            for (size_t i = 1; i < devices.size(); ++i) {
                const auto& a = devices[i].details; const auto& b = devices[deviceIndex].details;
                if ((!a.unifiedMemory && b.unifiedMemory) || (a.unifiedMemory == b.unifiedMemory && a.computeUnits > b.computeUnits)) deviceIndex = int(i);
            }
        }
        const auto& selected = devices[deviceIndex];
        report.device = selected.details.name;
        const size_t bytes = size_t(image.width) * image.height * sizeof(float);
        if (bytes > info<cl_ulong>(d,selected.handle,CL_DEVICE_MAX_MEM_ALLOC_SIZE)) throw std::runtime_error("Image exceeds GPU allocation limit");
        Session session(d);
        cl_int error;
        session.context = d.clCreateContext(nullptr,1,&selected.handle,nullptr,nullptr,&error); check(error,"Creating GPU context");
        session.queue = d.clCreateCommandQueue(session.context,selected.handle,CL_QUEUE_PROFILING_ENABLE,&error); check(error,"Creating GPU queue");
        const char* source = columnKernelSource;
        session.program = d.clCreateProgramWithSource(session.context,1,&source,nullptr,&error); check(error,"Creating GPU program");
        error = d.clBuildProgram(session.program,1,&selected.handle,"-cl-std=CL1.2",nullptr,nullptr);
        if (error != CL_SUCCESS) {
            size_t length = 0;
            d.clGetProgramBuildInfo(session.program,selected.handle,CL_PROGRAM_BUILD_LOG,0,nullptr,&length);
            std::string log(std::min(length,size_t(65536)),'\0');
            if (!log.empty()) d.clGetProgramBuildInfo(session.program,selected.handle,CL_PROGRAM_BUILD_LOG,log.size(),log.data(),nullptr);
            throw std::runtime_error("OpenCL kernel build failed: " + log);
        }
        session.kernel = d.clCreateKernel(session.program,"columns",&error); check(error,"Creating GPU kernel");
        report.setupSeconds = seconds(start);
        const auto upload = Clock::now();
        auto planes = session.buffer(scene.planes.size()*sizeof(ColumnPlane),scene.planes.data());
        auto brushes = session.buffer(scene.brushes.size()*sizeof(ColumnBrush),scene.brushes.data());
        auto cells = session.buffer(scene.cells.size()*sizeof(scene.cells[0]),scene.cells.data());
        auto references = session.buffer(scene.references.size()*sizeof(uint32_t),scene.references.data());
        auto offsets = session.buffer(image.offsets ? image.samples*2*sizeof(float) : 0,image.offsets);
        auto result = session.buffer(bytes,nullptr);
        const std::array<float,4> index{scene.minX,scene.minY,scene.scaleX,scene.scaleY};
        const std::array<float,4> area{image.sizeX / image.width,image.sizeY / image.height,0,0};
        // Host coordinates preserve the legacy half-pixel precision even without GPU doubles.
        std::vector<float> xs(image.width), ys(image.height);
        for (unsigned x = 0; x < image.width; ++x) xs[x] = image.samples <= 1
            ? float(image.minX + image.sizeX * ((x + 0.5) / image.width))
            : image.minX + image.sizeX * (float(x) / image.width);
        for (unsigned y = 0; y < image.height; ++y) ys[y] = image.samples <= 1
            ? float(image.minY + image.sizeY * ((y + 0.5) / image.height))
            : image.minY + image.sizeY * (float(y) / image.height);
        auto xbase = session.buffer(xs.size()*sizeof(float),xs.data());
        auto ybase = session.buffer(ys.size()*sizeof(float),ys.data());
        session.argument(0,planes); session.argument(1,brushes); session.argument(2,cells); session.argument(3,references);
        session.argument(4,offsets); session.argument(5,result); session.argument(6,index); session.argument(7,scene.grid);
        session.argument(8,area); session.argument(9,image.sizeZ); session.argument(10,image.width); session.argument(11,image.height);
        session.argument(12,image.samples); session.argument(13,unsigned(image.offsets == nullptr)); session.argument(14,image.seed);
        session.argument(16,xbase); session.argument(17,ybase);
        report.transferSeconds = seconds(upload);
        // Bounded row batches limit single-dispatch latency and driver watchdog exposure.
        const unsigned rows = std::clamp(2u*1024*1024 / image.width / image.samples,1u,64u);
        for (unsigned row = 0; row < image.height; row += rows) {
            session.argument(15,row);
            const size_t global[2]{image.width,std::min(rows,image.height-row)};
            cl_event event = nullptr;
            check(d.clEnqueueNDRangeKernel(session.queue,session.kernel,2,nullptr,global,nullptr,0,nullptr,&event),"Dispatching minimap columns");
            session.trackEvent(event);
        }
        check(d.clFinish(session.queue),"Waiting for GPU columns");
        for (auto event : session.events) {
            cl_ulong begin = 0,end = 0;
            check(d.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(begin),&begin,nullptr),"Profiling GPU start");
            check(d.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(end),&end,nullptr),"Profiling GPU end");
            report.kernelSeconds += double(end-begin)*1e-9;
        }
        const auto download = Clock::now();
        check(d.clEnqueueReadBuffer(session.queue,result,CL_TRUE,0,bytes,output,0,nullptr,nullptr),"Reading GPU columns");
        report.transferSeconds += seconds(download);
        for (size_t i = 0; i < size_t(image.width) * image.height; ++i)
            if (!std::isfinite(output[i])) throw std::runtime_error("GPU returned non-finite samples");
        report.totalSeconds = seconds(start);
        report.usedGPU = true;
        return true;
    } catch (const std::exception& error) { report.reason = error.what(); report.totalSeconds = seconds(start); return false; }
}
}
#else
namespace q3mapx {
struct AreaFactorComputer::Impl {};
AreaFactorComputer::AreaFactorComputer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AreaFactorComputer::~AreaFactorComputer() = default;
std::unique_ptr<AreaFactorComputer> createAreaFactorComputer(int, ComputeReport& report){
    report = {}; report.reason = "OpenCL disabled at build time"; return {};
}
bool AreaFactorComputer::compute(std::span<const AreaFactorSample>, std::span<const AreaFactorLight>,
                                 std::span<const AreaFactorVertex>, std::vector<float>& output, ComputeReport& report){
    output.clear(); report = {}; report.reason = "OpenCL disabled at build time"; return false;
}
std::vector<ComputeDevice> computeDevices(std::string& reason){ reason = "OpenCL disabled at build time"; return {}; }
bool computeColumns(const ColumnScene&,const ColumnImage&,int,float*,ComputeReport& report){
    report = {}; report.reason = "OpenCL disabled at build time"; return false;
}
}
#endif
