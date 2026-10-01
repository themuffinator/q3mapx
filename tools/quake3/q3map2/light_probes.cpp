// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "light_probes.h"
#include "point_fitting.h"
#include "bsp_lighting_evidence.h"
#include "bspfile_ibsp.h"
#include "bspfile_rbsp.h"
#include "q3mapx/atomic_file.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include <glib.h>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace q3mapx {
namespace {
using JSON=rapidjson::Value;
constexpr uint64_t maxRequestBytes=4*1024*1024,maxReportBytes=64*1024*1024;
constexpr size_t maxSamples=10'000,maxSources=16'384,maxResponses=100'000;
bool probesActive=false;
struct Identity { uint64_t bytes; std::string sha; };
Identity identify(const std::filesystem::path& path,uint64_t limit,bool bsp=false) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in || in.tellg()<0 || uint64_t(in.tellg())>limit) throw std::runtime_error("Probe input is unavailable or exceeds its byte limit");
    const auto bytes=uint64_t(in.tellg()); in.seekg(0);
    std::unique_ptr<GChecksum,decltype(&g_checksum_free)> hash(g_checksum_new(G_CHECKSUM_SHA256),g_checksum_free);
    std::array<unsigned char,65536> block{}; uint64_t total=0;
    while(in) {
        in.read(reinterpret_cast<char*>(block.data()),block.size()); const size_t count=size_t(in.gcount());
        if(bsp && total==0 && (count<8 || std::memcmp(block.data(),g_game->bspIdent,4)
            || (uint32_t(block[4])|uint32_t(block[5])<<8|uint32_t(block[6])<<16|uint32_t(block[7])<<24)!=uint32_t(g_game->bspVersion)))
            throw std::runtime_error("Probe source does not match selected native BSP profile");
        if(count) g_checksum_update(hash.get(),block.data(),count);
        total+=count; if(total>bytes) throw std::runtime_error("Probe input changed while reading");
    }
    if(!in.eof() || total!=bytes) throw std::runtime_error("Incomplete probe input");
    return {bytes,g_checksum_get_string(hash.get())};
}
void unchanged(const std::filesystem::path& path,const Identity& before,bool bsp) {
    const auto after=identify(path,bsp?0x7fffffff:maxRequestBytes,bsp);
    if(before.bytes!=after.bytes || before.sha!=after.sha) throw std::runtime_error("Probe input changed; report not published");
}
std::filesystem::path pathOf(const char* s) { return std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(s))).lexically_normal(); }
bool sameFile(const std::filesystem::path& a,const std::filesystem::path& b) {
    std::error_code ec; return std::filesystem::weakly_canonical(a)==std::filesystem::weakly_canonical(b) || std::filesystem::equivalent(a,b,ec);
}
void fields(const JSON& object,std::initializer_list<std::string_view> allowed) {
    if(!object.IsObject()) throw std::runtime_error("Probe record must be an object");
    std::set<std::string> seen;
    for(auto i=object.MemberBegin();i!=object.MemberEnd();++i) {
        std::string key(i->name.GetString(),i->name.GetStringLength());
        if(std::find(allowed.begin(),allowed.end(),key)==allowed.end() || !seen.insert(key).second)
            throw std::runtime_error("Unknown or duplicate probe field: "+key);
    }
}
const JSON& required(const JSON& v,const char* key) {
    if(!v.HasMember(key)) throw std::runtime_error(std::string("Missing probe field: ")+key);
    return v[key];
}
double scalar(const JSON& v,double lo,double hi) {
    if(!v.IsNumber() || !std::isfinite(v.GetDouble()) || v.GetDouble()<lo || v.GetDouble()>hi)
        throw std::runtime_error("Probe numeric field outside supported range");
    return v.GetDouble();
}
int integer(const JSON& v,int lo,int hi) {
    if(!v.IsInt() || v.GetInt()<lo || v.GetInt()>hi) throw std::runtime_error("Probe integer field outside supported range");
    return v.GetInt();
}
float floatValue(const JSON& v,double lo,double hi) {
    const double n=scalar(v,lo,hi); const float f=float(n);
    if(!std::isfinite(f) || (n!=0 && f==0)) throw std::runtime_error("Probe numeric field is not representable as a nonzero native float");
    return f;
}
Vector3 vector(const JSON& v,double lo=-1e6,double hi=1e6) {
    if(!v.IsArray() || v.Size()!=3) throw std::runtime_error("Probe vector must have three components");
    return Vector3(floatValue(v[0],lo,hi),floatValue(v[1],lo,hi),floatValue(v[2],lo,hi));
}
bool finite(const Vector3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
void entityNumber(entity_t& entity,const char* key,float number) {
    std::ostringstream text; text.imbue(std::locale::classic()); text.precision(std::numeric_limits<float>::max_digits10); text<<number;
    entity.setKeyValue(key,text.str().c_str());
}
void entityVector(entity_t& entity,const char* key,const Vector3& number) {
    std::ostringstream text; text.imbue(std::locale::classic()); text.precision(std::numeric_limits<float>::max_digits10);
    text<<number[0]<<' '<<number[1]<<' '<<number[2]; entity.setKeyValue(key,text.str().c_str());
}
void numericEntityValue(const char* value,unsigned count,double lo,double hi,bool integral=false) {
    if(!*value) return;
    std::istringstream stream(value); stream.imbue(std::locale::classic());
    for(unsigned i=0;i<count;++i) {
        double n=0;
        // Match integer entity syntax before applying bounds: an exponent or
        // decimal accepted as a double is not the integer the compiler reads.
        if(integral) { int parsed=0; stream>>parsed; n=parsed; }
        else stream>>n;
        if(!stream || !std::isfinite(n) || n<lo || n>hi || (n!=0 && float(n)==0))
            throw std::runtime_error("Invalid numeric BSP entity property for light probes");
    }
    std::string extra; if(stream>>extra) throw std::runtime_error("Trailing BSP entity numeric data for light probes");
}
void validateEntity(const entity_t& e) {
    numericEntityValue(e.valueForKey("origin"),3,-1e6,1e6);
    numericEntityValue(e.valueForKey("_color"),3,0,1e6);
    if(e.classname_prefixed("light")) {
        for(const char* key:{"_light","light","scale","fade","_anglescale","_extradist","radius"})
            numericEntityValue(e.valueForKey(key),1,-1e6,1e6);
        for(const char* key:{"_deviance","_deviation","_jitter"}) numericEntityValue(e.valueForKey(key),1,0,1e6);
        numericEntityValue(e.valueForKey("spawnflags"),1,0,127,true);
        numericEntityValue(e.valueForKey("_samples"),1,0,256,true);
        for(const char* key:{"_style","style"}) numericEntityValue(e.valueForKey(key),1,0,253,true);
    }
    if(e.classname_is("worldspawn")) for(const char* key:{"_ambient","ambient","_minlight","_minvertexlight","_mingridlight","_maxlight"})
        numericEntityValue(e.valueForKey(key),1,0,1e6);
    const char* model=e.valueForKey("model");
    if(*model=='*') {
        int index; const char* end=model+std::strlen(model);
        const auto parsed=std::from_chars(model+1,end,index);
        if(parsed.ec!=std::errc{} || parsed.ptr!=end || index<1 || size_t(index)>=bspModels.size())
            throw std::runtime_error("Invalid inline model reference for light probes");
    }
}
void allowedOptions(Args& args) {
    const std::set<std::string> values={"-point","-pointscale","-spherical","-sphericalscale","-spot","-spotscale","-area","-areascale",
        "-sky","-skyscale","-gamma","-exposure","-compensate","-brightness","-contrast","-saturation","-extradist","-thresh","-lightanglehl","-backsplash"};
    const std::set<std::string> flags={"-wolf","-q3","-nofastpoint","-fast","-faster","-notrace","-patchshadows","-onesky",
        "-srgb","-nosrgb","-srgblight","-nosrgblight","-srgbtex","-nosrgbtex","-srgbcolor","-nosrgbcolor","-styles","-style","-nostyles","-nostyle"};
    const auto options=args.getVector();
    for(size_t i=0;i<options.size();++i) {
        std::string option=options[i]; for(char& c:option) c=char(std::tolower(static_cast<unsigned char>(c)));
        if(values.contains(option)) {
            const size_t count=option=="-backsplash"?2:1;
            if(options.size()-i-1<count) throw std::runtime_error("Missing light probe option value");
            i+=count;
        }
        else if(!flags.contains(option)) throw std::runtime_error("Light probe mode does not support option: "+option);
    }
}
struct Sample {
    int surface; Vector3 position,normal; float offset=0;
    int slot=-1,page=-1,x=0,y=0,style=0; bool patch=false;
    Vector3b observed{0};
};
struct BakedOptions {
    bool enabled=false;
    unsigned stride=4;
    uint64_t maxWork=50'000'000,maxObservations=200'000;
    size_t sampleLimit=maxSamples;
    float offset=0;
    std::set<int> surfaces;
};
struct ErrorTotals {
    uint64_t samples=0,components=0;
    double absolute=0,squared=0,maximum=0;
    void add(const Vector3b& prediction,const Vector3b& observed) {
        ++samples;
        for(int a=0;a<3;++a) {
            const double error=double(prediction[a])-observed[a];
            ++components; absolute+=std::abs(error); squared+=error*error;
            maximum=std::max(maximum,std::abs(error));
        }
    }
};
struct Response { size_t light; Vector3 color; float subsampling; };
struct Result { int cluster=-1; bool traceLimit=false; Vector3 origin; std::vector<Response> responses; };
class Stream {
public:
    using Ch=char;
    explicit Stream(FILE* file):file_(file){}
    void Put(char c) { if(bytes_++==maxReportBytes) throw std::runtime_error("Light probe report exceeds 64 MiB"); if(used_==buffer_.size()) Flush(); buffer_[used_++]=c; }
    void Flush() { writeOutput(file_,buffer_.data(),used_); used_=0; }
private:
    FILE* file_; uint64_t bytes_=0; size_t used_=0; std::array<char,65536> buffer_{};
};
using Writer=rapidjson::PrettyWriter<Stream>;
void number(Writer& w,const char* key,uint64_t n) { w.Key(key); w.Uint64(n); }
void value(Writer& w,const char* key,double n) { if(!std::isfinite(n)) throw std::runtime_error("Non-finite probe metadata"); w.Key(key); w.Double(n); }
void vec(Writer& w,const char* key,const Vector3& v) { if(!finite(v)) throw std::runtime_error("Non-finite probe vector"); w.Key(key); w.StartArray(); for(int a=0;a<3;++a) w.Double(v[a]); w.EndArray(); }
void text(Writer& w,const char* key,const char* v) { w.Key(key); w.String(v); }
void flag(Writer& w,const char* key,bool v) { w.Key(key); w.Bool(v); }
void rgb(Writer& w,const char* key,const Vector3b& v) { w.Key(key); w.StartArray(); for(int a=0;a<3;++a) w.Uint(v[a]); w.EndArray(); }
void errors(Writer& w,const char* key,const ErrorTotals& e) {
    w.Key(key); w.StartObject(); number(w,"samples",e.samples); number(w,"components",e.components);
    if(e.components) { value(w,"mae_bytes",e.absolute/e.components); value(w,"rmse_bytes",std::sqrt(e.squared/e.components)); value(w,"maximum_error_bytes",e.maximum); }
    else for(const char* field:{"mae_bytes","rmse_bytes","maximum_error_bytes"}) { w.Key(field); w.Null(); }
    w.EndObject();
}
const char* type(const light_t& light) {
    switch(light.type) { case ELightType::Point:return "point"; case ELightType::Spot:return "spot"; case ELightType::Area:return "area"; case ELightType::Sun:return "sun_sky"; }
    return "unknown";
}
}

struct LightProbes::Data {
    std::filesystem::path source,request,destination;
    Identity sourceIdentity{},requestIdentity{};
    rapidjson::Document input;
    std::vector<Sample> samples;
    std::vector<Result> results;
    std::vector<int> candidateEntities;
    std::set<std::string> targets;
    std::vector<const light_t*> activeLights;
    BakedOptions baked;
    PointFitOptions fit;
    std::map<std::string,uint64_t> exclusions;
    uint64_t evidenceWork=0,evidenceObservations=0;
    size_t originalEntities=0;
    uint64_t pairLimit=5'000'000;
    std::atomic<size_t> next{0},responses{0};
    PointFitResult fitPoints(const Vector3& ambient) {
        PointFitResult unavailable;
        std::set<int> shaders;
        for(const auto& surface:bspDrawSurfaces) shaders.insert(surface.shaderNum);
        for(int index:shaders) {
            const auto& si=ShaderInfoForShader(bspShaders[index].shader);
            if(!si.shaderText && !fit.allowImplicitMaterials) ++unavailable.exclusions["unresolved_shader_text"];
            if(!si.shaderImage || strEqual(si.shaderImage->name.c_str(),DEFAULT_IMAGE)) ++unavailable.exclusions["unresolved_shader_image"];
            if(!si.lightImagePath.empty() && !ImageLoad(si.lightImagePath)) ++unavailable.exclusions["missing_requested_light_image"];
            if(!si.normalImagePath.empty() && !si.normalImage) ++unavailable.exclusions["missing_requested_normal_image"];
        }
        if(!unavailable.exclusions.empty()) { unavailable.status="unresolved_materials"; return unavailable; }
        std::vector<PointFitReceiver> receivers;
        std::map<std::string,uint64_t> excluded;
        for(size_t i=0;i<samples.size();++i) {
            const auto& s=samples[i]; const auto& r=results[i];
            if(unsigned(s.style)!=fit.style) { ++excluded["different_style"]; continue; }
            if(r.cluster<0 || r.traceLimit) { ++excluded["unknown_baseline_trace"]; continue; }
            if(std::any_of(s.observed.data(),s.observed.data()+3,[](byte b){ return b==255; })) {
                ++excluded["observed_255_channel"]; continue;
            }
            Vector3 baseline(0);
            for(const auto& response:r.responses) if(activeLights[response.light]->style==s.style) baseline+=response.color;
            if(s.slot==0) baseline+=ambient;
            Vector3 color=baseline;
            if(s.slot==0) for(int a=0;a<3;++a) color[a]=std::max(color[a],minLight[a]);
            const float brightness=surfaceInfos[s.surface].si->lmBrightness;
            const Vector3 encoded=ColorToFloat(color,1,brightness);
            if(!finite(encoded) || std::any_of(encoded.data(),encoded.data()+3,[](float v){ return v<0 || v>=256; })) {
                ++excluded["unrepresentable_baseline_encoding"]; continue;
            }
            receivers.push_back({i,s.surface,r.cluster,s.slot,s.page,s.x,s.y,r.origin,s.normal,baseline,s.observed,brightness});
        }
        auto result=fitPointLights(fit,std::move(receivers));
        result.exclusions=std::move(excluded);
        return result;
    }
    void selectBakedSamples() {
        BSPEvidence budget;
        const auto evidence=analyzeBSPLighting(budget,baked.maxWork,{baked.stride,baked.maxObservations});
        evidenceWork=budget.workUsed; evidenceObservations=evidence.observations;
        std::vector<Vector3> offsets(bspModels.size(),Vector3(0));
        for(const auto& e:entities) if(const char* m=e.valueForKey("model"); *m=='*') offsets[std::atoi(m+1)]=e.vectorForKey("origin");
        for(int index:baked.surfaces) if(size_t(index)>=bspDrawSurfaces.size()) throw std::runtime_error("Baked probe surface index is outside the BSP");
        for(size_t i=0;i<evidence.surfaces.size();++i) {
            if(!baked.surfaces.empty() && !baked.surfaces.contains(int(i))) { ++exclusions["surface_not_selected"]; continue; }
            const auto& surface=evidence.surfaces[i]; const auto& ds=bspDrawSurfaces[i];
            for(int slot=0;slot<MAX_LIGHTMAPS;++slot) {
                const auto& item=surface.slots[slot];
                if(std::string_view(item.status)!="analyzed" && std::string_view(item.status)!="bezier_analyzed") {
                    ++exclusions[std::string("slot_")+item.status]; continue;
                }
                exclusions["constant_regions"]+=item.constantRegions;
                exclusions["degenerate_geometry_triangles"]+=item.degenerateGeometryTriangles;
                exclusions["degenerate_uv_triangles"]+=item.degenerateUVTriangles;
                const auto add=[&](const auto& point,bool patch,bool unresolved) {
                    if(unresolved) { ++exclusions["unresolved_mapping"]; return; }
                    if(point.ambiguous) { ++exclusions["ambiguous_mapping"]; return; }
                    if(point.boundary) { ++exclusions["mapping_boundary"]; return; }
                    if(point.normal==std::array<double,3>{}) { ++exclusions["zero_normal"]; return; }
                    if(samples.size()>=baked.sampleLimit) throw std::runtime_error("Baked probe sample limit exceeded; increase stride or restrict surfaces");
                    Sample sample{}; sample.surface=int(i); sample.slot=slot; sample.page=ds.lightmapNum[slot];
                    sample.style=ds.lightmapStyles[slot]; sample.x=point.x; sample.y=point.y; sample.patch=patch; sample.offset=baked.offset;
                    for(int a=0;a<3;++a) { sample.position[a]=float(point.position[a])+offsets[surface.model][a]; sample.normal[a]=float(point.normal[a]); }
                    vector3_normalise(sample.normal);
                    if(!finite(sample.position) || !finite(sample.normal)) throw std::runtime_error("Invalid baked probe geometry");
                    const size_t offset=((size_t(sample.page)*evidence.pageSize+sample.y)*evidence.pageSize+sample.x)*3;
                    for(int a=0;a<3;++a) sample.observed[a]=bspLightBytes[offset+a];
                    samples.push_back(sample);
                };
                for(const auto& p:item.observations) add(p,false,false);
                for(const auto& p:item.patchObservations) add(p,true,p.unresolved || !p.rootHits);
            }
        }
    }
    void sample(size_t index) {
        const auto& s=samples[index]; auto& result=results[index]; const auto& info=surfaceInfos[s.surface];
        result.origin=s.position+s.normal*s.offset;
        result.cluster=ClusterForPointExt(result.origin,0.125f);
        if(result.cluster<0) return;
        trace_t trace{};
        trace.testOcclusion=!noTrace; trace.forceSunlight=info.si->forceSunlight;
        trace.recvShadows=info.recvShadows; trace.numSurfaces=1; int surface=s.surface; trace.surfaces=&surface;
        trace.inhibitRadius=DEFAULT_INHIBIT_RADIUS; trace.twoSided=info.si->twoSided;
        trace.cluster=result.cluster; trace.origin=result.origin; trace.normal=s.normal;
        for(size_t i=0;i<activeLights.size();++i) {
            trace.numTestNodes=0;
            trace.light=activeLights[i]; LightContributionToSample(&trace);
            if(trace.numTestNodes>=MAX_TRACE_TEST_NODES) {
                result.traceLimit=true; responses.fetch_sub(result.responses.size()); result.responses.clear(); return;
            }
            if(trace.light->flags & LightFlags::Negative) vector3_negate(trace.color);
            if(!finite(trace.color) || !std::isfinite(trace.forceSubsampling)) throw std::runtime_error("Non-finite direct light response");
            if(trace.color==g_vector3_identity) continue;
            if(responses.fetch_add(1)>=maxResponses) throw std::runtime_error("Light probe response limit exceeded");
            result.responses.push_back({i,trace.color,trace.forceSubsampling});
        }
    }
    static Data* active;
    static void worker(int) { for(size_t i=active->next.fetch_add(1);i<active->samples.size();i=active->next.fetch_add(1)) active->sample(i); }
};
LightProbes::Data* LightProbes::Data::active=nullptr;
void checkProbeSourceBudget() {
    if(probesActive && lights.size()>=maxSources) throw std::runtime_error("Light probe source limit exceeded during generation");
}
LightProbes::LightProbes(std::unique_ptr<Data> data):data_(std::move(data)){ probesActive=true; }
LightProbes::~LightProbes(){ probesActive=false; }

std::unique_ptr<LightProbes> LightProbes::parse(Args& args,const char* source) {
    const bool requested=args.takeArg("-probes");
    const char* request=requested?args.takeNext():nullptr;
    const bool reportSet=args.takeArg("-probe-report"); const char* report=reportSet?args.takeNext():nullptr;
    const bool limitSet=args.takeArg("-probe-max-pairs"); const uint64_t limit=limitSet?args.takeInt(1,20'000'000):5'000'000;
    if(!requested) { if(reportSet || limitSet) throw std::runtime_error("Probe report/limits require -probes"); return {}; }
    if(g_game->load!=LoadIBSPFile && g_game->load!=LoadRBSPFile) throw std::runtime_error("Light probes require a native IBSP/RBSP reader");
    allowedOptions(args);
    auto data=std::make_unique<Data>(); data->pairLimit=limit;
    data->source=pathOf(source); data->source.replace_extension(".bsp"); data->request=pathOf(request);
    data->destination=report?pathOf(report):data->source; if(!report) data->destination.replace_extension(".light-probes.json");
    if(data->destination.extension()!=".json" || sameFile(data->source,data->destination) || sameFile(data->request,data->destination))
        throw std::runtime_error("Light probe report must be a separate .json output");
    data->sourceIdentity=identify(data->source,0x7fffffff,true); data->requestIdentity=identify(data->request,maxRequestBytes);
    std::ifstream input(data->request,std::ios::binary); std::string content(size_t(data->requestIdentity.bytes),'\0');
    if(!input.read(content.data(),std::streamsize(content.size()))) throw std::runtime_error("Cannot read probe request");
    if(content.find('\0')!=std::string::npos) throw std::runtime_error("Embedded NUL in probe JSON");
    data->input.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(content.data(),content.size());
    if(data->input.HasParseError()) throw std::runtime_error("Invalid probe JSON at byte "+std::to_string(data->input.GetErrorOffset()));
    const auto& doc=data->input;
    fields(doc,{"schema_version","samples","baked_lightmaps","lights","fit_point_lights"}); integer(required(doc,"schema_version"),1,1);
    if(doc.HasMember("samples")==doc.HasMember("baked_lightmaps")) throw std::runtime_error("Choose exactly one of samples or baked_lightmaps");
    if(doc.HasMember("samples")) {
        const auto& samples=doc["samples"];
        if(!samples.IsArray() || samples.Empty() || samples.Size()>maxSamples) throw std::runtime_error("Light probes require 1..10000 samples");
        for(const auto& s:samples.GetArray()) {
            fields(s,{"surface","position","normal","offset"});
            Sample sample; sample.surface=integer(required(s,"surface"),0,200'000);
            sample.position=vector(required(s,"position")); sample.normal=vector(required(s,"normal"));
            const double n=std::hypot(double(sample.normal[0]),double(sample.normal[1]),double(sample.normal[2]));
            if(n<1e-12) throw std::runtime_error("Light probe normals must be nonzero");
            sample.normal/=n;
            if(s.HasMember("offset")) sample.offset=floatValue(s["offset"],-16,16);
            data->samples.push_back(sample);
        }
    }
    else {
        const auto& settings=doc["baked_lightmaps"]; auto& baked=data->baked; baked.enabled=true;
        fields(settings,{"stride","normal_offset","max_samples","max_observations","max_work","surfaces"});
        baked.offset=floatValue(required(settings,"normal_offset"),-16,16);
        if(settings.HasMember("stride")) baked.stride=integer(settings["stride"],1,1024);
        if(settings.HasMember("max_samples")) baked.sampleLimit=integer(settings["max_samples"],1,int(maxSamples));
        if(settings.HasMember("max_observations")) baked.maxObservations=integer(settings["max_observations"],1,200'000);
        if(settings.HasMember("max_work")) baked.maxWork=integer(settings["max_work"],1,1'000'000'000);
        if(settings.HasMember("surfaces")) {
            const auto& surfaces=settings["surfaces"];
            if(!surfaces.IsArray() || surfaces.Empty() || surfaces.Size()>200'000) throw std::runtime_error("Invalid baked probe surface selection");
            for(const auto& surface:surfaces.GetArray()) if(!baked.surfaces.insert(integer(surface,0,199'999)).second)
                throw std::runtime_error("Duplicate baked probe surface selection");
        }
    }
    if(doc.HasMember("lights") && (!doc["lights"].IsArray() || doc["lights"].Size()>256)) throw std::runtime_error("At most 256 proposed probe lights are supported");
    if(doc.HasMember("fit_point_lights")) {
        if(!data->baked.enabled) throw std::runtime_error("Point fitting requires baked_lightmaps observations");
        const auto& settings=doc["fit_point_lights"]; auto& fit=data->fit; fit.enabled=true;
        fields(settings,{"grid_spacing","mins","maxs","max_candidates","max_lights","max_intensity","refinement_steps","style",
                         "min_improvement_rmse","max_rmse","max_work","allow_implicit_materials"});
        if(settings.HasMember("grid_spacing")) fit.spacing=floatValue(settings["grid_spacing"],1,1e6);
        if(settings.HasMember("max_candidates")) fit.maxCandidates=integer(settings["max_candidates"],1,4096);
        if(settings.HasMember("max_lights")) fit.maxLights=integer(settings["max_lights"],1,16);
        if(settings.HasMember("max_intensity")) fit.maxIntensity=floatValue(settings["max_intensity"],0.01,1e6);
        if(settings.HasMember("refinement_steps")) fit.refinementSteps=integer(settings["refinement_steps"],0,10);
        if(settings.HasMember("style")) fit.style=integer(settings["style"],0,253);
        if(settings.HasMember("min_improvement_rmse")) fit.minImprovement=floatValue(settings["min_improvement_rmse"],0.01,64);
        if(settings.HasMember("max_rmse")) fit.maxRMSE=floatValue(settings["max_rmse"],0.01,64);
        if(settings.HasMember("max_work")) fit.maxWork=integer(settings["max_work"],1,1'000'000'000);
        if(settings.HasMember("allow_implicit_materials")) {
            if(!settings["allow_implicit_materials"].IsBool()) throw std::runtime_error("allow_implicit_materials must be boolean");
            fit.allowImplicitMaterials=settings["allow_implicit_materials"].GetBool();
        }
        if(settings.HasMember("mins")!=settings.HasMember("maxs")) throw std::runtime_error("Point fitting needs both mins and maxs");
        if(settings.HasMember("mins")) {
            fit.explicitBounds=true; fit.mins=vector(settings["mins"]); fit.maxs=vector(settings["maxs"]);
            for(int a=0;a<3;++a) if(fit.mins[a]>=fit.maxs[a]) throw std::runtime_error("Invalid point fitting bounds");
        }
        fit.blockSize=std::max(4u,data->baked.stride*2);
    }
    return std::unique_ptr<LightProbes>(new LightProbes(std::move(data)));
}

void LightProbes::prepare() {
    auto& d=*data_;
    if(entities.empty() || !entities[0].classname_is("worldspawn") || bspModels.empty() || bspNodes.empty())
        throw std::runtime_error("Light probes need a world model, worldspawn and BSP tree");
    if(entities.size()>100'000 || bspDrawVerts.size()>2'000'000 || bspDrawSurfaces.size()>200'000
        || uint64_t(bspDrawSurfaces.size())*(bspLeafs.size()+bspLeafSurfaces.size())>50'000'000)
        throw std::runtime_error("Light probe scene preparation limit exceeded");
    // Lighting's surface initialization requires unique, complete ownership.
    std::vector<unsigned char> owners(bspDrawSurfaces.size());
    for(const auto& model:bspModels) for(int i=0;i<model.numBSPSurfaces;++i)
        if(++owners[model.firstBSPSurface+i]!=1) throw std::runtime_error("Overlapping probe surface ownership");
    if(std::find(owners.begin(),owners.end(),0)!=owners.end()) throw std::runtime_error("Unowned probe surface");
    for(const auto& sample:d.samples) if(size_t(sample.surface)>=bspDrawSurfaces.size()) throw std::runtime_error("Probe surface index is outside the BSP");
    for(const auto& vertex:bspDrawVerts) for(int a=0;a<3;++a)
        if(std::abs(vertex.xyz[a])>1e6) throw std::runtime_error("Probe geometry exceeds supported coordinate range");
    std::vector<unsigned> poses(bspModels.size());
    for(const auto& e:entities) {
        validateEntity(e);
        if(const char* model=e.valueForKey("model"); *model=='*') ++poses[std::atoi(model+1)];
        if(const char* target=e.valueForKey("targetname"); *target) d.targets.insert(target);
    }
    for(size_t i=1;i<poses.size();++i) if(poses[i]!=1) throw std::runtime_error("Inline probe model needs exactly one surviving entity pose");
    if(d.baked.enabled) d.selectBakedSamples();
    if(d.fit.enabled && !d.fit.explicitBounds) {
        d.fit.mins=bspModels[0].minmax.mins; d.fit.maxs=bspModels[0].minmax.maxs;
        if(!finite(d.fit.mins) || !finite(d.fit.maxs)) throw std::runtime_error("Invalid world bounds for point fitting");
    }
    d.originalEntities=entities.size();
    if(d.input.HasMember("lights")) for(const auto& light:d.input["lights"].GetArray()) {
        fields(light,{"origin","intensity","color","spawnflags","fade","angle_scale","extra_distance","style","target","radius","sun"});
        entity_t e{}; e.setKeyValue("classname","light"); entityVector(e,"origin",vector(required(light,"origin")));
        if(light.HasMember("color")) entityVector(e,"_color",vector(light["color"],0,1));
        for(const auto& [input,key]:std::array<std::pair<const char*,const char*>,5>{{{"intensity","_light"},{"fade","fade"},{"angle_scale","_anglescale"},{"extra_distance","_extradist"},{"radius","radius"}}})
            if(light.HasMember(input)) entityNumber(e,key,floatValue(light[input],std::string_view(input)=="intensity"?-1e6:0,1e6));
        if(light.HasMember("style")) e.setKeyValue("style",integer(light["style"],0,253));
        if(light.HasMember("spawnflags")) e.setKeyValue("spawnflags",integer(light["spawnflags"],0,127));
        if(light.HasMember("sun")) { if(!light["sun"].IsBool()) throw std::runtime_error("Probe sun must be boolean"); e.setKeyValue("_sun",light["sun"].GetBool()?1:0); }
        if(light.HasMember("target")) {
            entity_t target{}; target.setKeyValue("classname","info_null"); entityVector(target,"origin",vector(light["target"]));
            const std::string prefix="_q3mapx_probe_target_"+std::to_string(d.candidateEntities.size())+"_";
            size_t suffix=0; std::string name=prefix+std::to_string(suffix);
            while(!d.targets.insert(name).second) name=prefix+std::to_string(++suffix);
            target.setKeyValue("targetname",name.c_str()); e.setKeyValue("target",name.c_str()); entities.push_back(std::move(target));
        }
        else if(light.HasMember("sun") && light["sun"].GetBool()) throw std::runtime_error("Proposed sun requires a target position");
        d.candidateEntities.push_back(int(entities.size())); entities.push_back(std::move(e));
    }
}
void LightProbes::prepareSurfaces() {
    uint64_t estimated=0;
    for(const auto& e:entities) if(e.classname_prefixed("light")) estimated+=uint64_t(std::max(1,e.intForKey("_samples")));
    std::set<const shaderInfo_t*> seen;
    for(size_t i=0;i<bspDrawSurfaces.size();++i) {
        const auto& ds=bspDrawSurfaces[i]; auto& info=surfaceInfos[i]; const auto& si=*info.si;
        if(ds.surfaceType==MST_PATCH) {
            info.longestCurve=PatchLongestCurve(mesh_view_t(ds.patchWidth,ds.patchHeight,&yDrawVerts[ds.firstVert]));
            info.patchIterations=IterationsForCurve(info.longestCurve,patchSubdivisions);
            if(info.patchIterations>8) throw std::runtime_error("Probe patch tessellation limit exceeded");
        }
        if(si.value>0 && si.lightSubdivide>0 && si.lightSubdivide<1)
            throw std::runtime_error("Probe emitter subdivision below one map unit is unsupported");
        if(!seen.insert(&si).second) continue;
        for(const auto& sun:si.suns) estimated+=uint64_t(std::max(1,sun.numSamples));
        for(const auto& sky:si.skylights) {
            if(sky.iterations<2 || sky.iterations>128) throw std::runtime_error("Probe sky subdivision limit exceeded");
            estimated+=8*uint64_t(sky.iterations)*sky.iterations+4*sky.iterations+2;
        }
        if(estimated>maxSources) throw std::runtime_error("Light probe source generation estimate exceeds limit");
    }
}
void LightProbes::checkLights() const {
    if(lights.size()>maxSources) throw std::runtime_error("Light probe source limit exceeded");
    for(const auto& light:lights) if(!finite(light.origin) || !finite(light.normal) || !finite(light.color)
        || !std::isfinite(light.photons) || !std::isfinite(light.add) || !std::isfinite(light.extraDist)
        || !std::isfinite(light.radiusByDist) || !std::isfinite(light.fade) || !std::isfinite(light.angleScale))
        throw std::runtime_error("Non-finite generated light parameters");
}

void LightProbes::run(const Vector3& ambient,size_t generated) {
    auto& d=*data_; checkLights();
    if(uint64_t(lights.size())*d.samples.size()>d.pairLimit) throw std::runtime_error("Light probe pair limit exceeded");
    for(const auto& light:lights) d.activeLights.push_back(&light);
    d.results.resize(d.samples.size()); Data::active=&d;
    struct Reset { ~Reset(){ Data::active=nullptr; } } reset;
    RunThreadsOnIndividual(int(std::min({d.samples.size(),size_t(std::max(numthreads,1)),size_t(32)})),true,Data::worker,"TraceLightProbes",1);
    PointFitResult fit;
    if(d.fit.enabled) fit=d.fitPoints(ambient);
    unchanged(d.source,d.sourceIdentity,true); unchanged(d.request,d.requestIdentity,false);
    OutputFiles output; Stream stream(output.open(d.destination)); Writer w(stream);
    w.StartObject(); number(w,"schema_version",1);
    text(w,"status",d.fit.enabled?"point_fitting_diagnostic":"direct_forward_observations_only");
    text(w,"game",g_game->arg); flag(w,"light_inference_performed",d.fit.enabled && fit.gridPoints>0);
    text(w,"source_sha256",d.sourceIdentity.sha.c_str()); text(w,"request_sha256",d.requestIdentity.sha.c_str());
    number(w,"generated_sources",generated); number(w,"active_sources",lights.size()); number(w,"culled_sources",generated-lights.size());
    number(w,"pair_tests_upper_bound",lights.size()*d.samples.size()); number(w,"pair_limit",d.pairLimit);
    number(w,"response_limit",maxResponses); number(w,"nonzero_responses",d.responses); number(w,"max_active_workers",32);
    number(w,"trace_node_capacity",MAX_TRACE_TEST_NODES); value(w,"cluster_tolerance",0.125);
    text(w,"coordinates",d.baked.enabled?"bsp_geometric_associations_with_model_origins_and_requested_normal_offset":"explicit_world_space_with_requested_normal_offset");
    vec(w,"world_ambient",ambient); vec(w,"world_minlight",minLight);
    w.Key("settings"); w.StartObject();
    for(const auto& [key,n]:std::array<std::pair<const char*,double>,12>{{{"point_scale",pointScale},{"spot_scale",spotScale},{"area_scale",areaScale},{"sky_scale",skyScale},
        {"gamma",lightmapGamma},{"compensation",lightmapCompensate},{"exposure",lightmapExposure},{"brightness",lightmapBrightness},{"contrast_factor",lightmapContrast},
        {"saturation",g_lightmapSaturation},{"maximum_light",maxLight},{"falloff_tolerance",falloffTolerance}}}) value(w,key,n);
    flag(w,"half_lambert",lightAngleHL); flag(w,"wolf",wolfLight); flag(w,"trace_occlusion",!noTrace); flag(w,"fast",fast); flag(w,"faster",faster);
    flag(w,"lightmaps_srgb",lightmapsRGB); flag(w,"textures_srgb",texturesRGB); flag(w,"entity_colors_srgb",colorsRGB); w.EndObject();
    if(d.baked.enabled) {
        w.Key("baked_comparison"); w.StartObject();
        text(w,"status",d.samples.empty()?"no_usable_observations":"direct_encoding_hypothesis");
        flag(w,"original_bake_settings_known",false); flag(w,"encoding_calibrated",false);
        text(w,"sampling","unambiguous_nonboundary_internal_texel_centers_with_explicit_normal_offset");
        text(w,"normal_source","interpolated_stored_vertex_or_patch_control_normals");
        text(w,"encoding","current_compiler_ColorToFloat_then_native_byte_conversion");
        text(w,"ambient_minlight","world_values_on_slot_zero_only_unknown_source_overrides");
        text(w,"weighting","equal_per_surface_slot_texel_observation_not_independent_texels");
        number(w,"stride",d.baked.stride); value(w,"normal_offset",d.baked.offset);
        number(w,"page_size",g_game->lightmapSize); number(w,"max_samples",d.baked.sampleLimit);
        number(w,"max_observations",d.baked.maxObservations); number(w,"max_work",d.baked.maxWork);
        number(w,"evidence_observations",d.evidenceObservations); number(w,"evidence_work",d.evidenceWork);
        number(w,"selected_samples",d.samples.size());
        w.Key("selected_surfaces"); w.StartArray(); for(int i:d.baked.surfaces) w.Int(i); w.EndArray();
        w.Key("exclusions"); w.StartObject(); for(const auto& [key,count]:d.exclusions) number(w,key.c_str(),count); w.EndObject();
        w.EndObject();
    }
    w.Key("proposed_entity_indices"); w.StartArray(); for(int e:d.candidateEntities) w.Int(e); w.EndArray();
    w.Key("sources"); w.StartArray();
    for(size_t i=0;i<d.activeLights.size();++i) {
        const auto& light=*d.activeLights[i]; w.StartObject(); number(w,"index",i); text(w,"type",type(light));
        w.Key("bsp_entity"); if(light.sourceEntity>=0 && size_t(light.sourceEntity)<d.originalEntities) w.Int(light.sourceEntity); else w.Null();
        const auto proposed=std::find(d.candidateEntities.begin(),d.candidateEntities.end(),light.sourceEntity);
        w.Key("proposed_light"); if(proposed!=d.candidateEntities.end()) w.Uint64(proposed-d.candidateEntities.begin()); else w.Null();
        w.Key("surface"); if(light.sourceSurface>=0) w.Int(light.sourceSurface); else w.Null();
        number(w,"style",light.style); vec(w,"origin_after_envelope_nudge",light.origin); vec(w,"normal",light.normal); vec(w,"color",light.color);
        value(w,"photons",light.photons); value(w,"area_add",light.add); value(w,"radius_by_distance",light.radiusByDist); value(w,"envelope",light.envelope);
        value(w,"fade",light.fade); value(w,"angle_scale",light.angleScale); value(w,"extra_distance",light.extraDist); value(w,"falloff_tolerance",light.falloffTolerance);
        flag(w,"angle_attenuation",bool(light.flags&LightFlags::AttenAngle)); flag(w,"linear_attenuation",bool(light.flags&LightFlags::AttenLinear));
        flag(w,"negative",bool(light.flags&LightFlags::Negative)); flag(w,"dark_flag",bool(light.flags&LightFlags::Dark)); w.EndObject();
    } w.EndArray();
    w.Key("materials"); w.StartArray();
    std::set<int> shaders;
    for(const auto& surface:bspDrawSurfaces) shaders.insert(surface.shaderNum);
    for(int index:shaders) {
        const auto& si=ShaderInfoForShader(bspShaders[index].shader); w.StartObject(); number(w,"bsp_shader",index); text(w,"name",si.shader.c_str());
        flag(w,"shader_text_available",si.shaderText!=nullptr); flag(w,"default_image",si.shaderImage && strEqual(si.shaderImage->name.c_str(),DEFAULT_IMAGE));
        text(w,"shader_image",si.shaderImage?si.shaderImage->filename.c_str():""); text(w,"light_image",si.lightImage?si.lightImage->filename.c_str():"");
        flag(w,"missing_requested_normal_image",!si.normalImagePath.empty() && !si.normalImage); flag(w,"normal_image_present",si.normalImage!=nullptr);
        flag(w,"two_sided",si.twoSided); flag(w,"alpha_shadow",bool(si.compileFlags&C_ALPHASHADOW)); flag(w,"light_filter",bool(si.compileFlags&C_LIGHTFILTER));
        flag(w,"sky",bool(si.compileFlags&C_SKY)); value(w,"surface_light",si.value); value(w,"floodlight_intensity",si.floodlightIntensity);
        value(w,"filter_radius",si.lmFilterRadius); value(w,"lightmap_brightness",si.lmBrightness); w.EndObject();
    } w.EndArray();
    ErrorTotals allErrors,non255Errors; std::map<int,ErrorTotals> styleErrors;
    uint64_t unknownComparisons=0,invalidEncoding=0,subsamplingComparisons=0;
    w.Key("samples"); w.StartArray();
    for(size_t i=0;i<d.samples.size();++i) {
        const auto& sample=d.samples[i]; const auto& r=d.results[i]; w.StartObject(); number(w,"index",i); number(w,"surface",sample.surface);
        vec(w,"position",sample.position); vec(w,"normal",sample.normal); value(w,"normal_offset",sample.offset); vec(w,"probe_origin",r.origin);
        w.Key("cluster"); w.Int(r.cluster); text(w,"status",r.cluster<0?"outside_usable_cluster":r.traceLimit?"trace_node_limit":"sampled");
        std::map<int,Vector3> total;
        w.Key("responses"); w.StartArray();
        for(const auto& response:r.responses) {
            w.StartObject(); number(w,"source",response.light); vec(w,"linear_rgb",response.color); value(w,"subsampling_signal",response.subsampling); w.EndObject();
            const int style=d.activeLights[response.light]->style;
            auto [entry,inserted]=total.try_emplace(style,Vector3(0)); entry->second+=response.color;
        } w.EndArray();
        w.Key("direct_by_style"); w.StartArray(); for(const auto& [style,color]:total) { w.StartObject(); number(w,"style",style); vec(w,"linear_rgb",color); w.EndObject(); } w.EndArray();
        if(sample.slot>=0) {
            w.Key("baked_lightmap"); w.StartObject(); number(w,"slot",sample.slot); number(w,"style",sample.style); number(w,"page",sample.page);
            w.Key("texel"); w.StartArray(); w.Int(sample.x); w.Int(sample.y); w.EndArray();
            text(w,"geometry",sample.patch?"stored_bezier":"indexed_triangles"); rgb(w,"observed_rgb",sample.observed);
            const bool has255=sample.observed[0]==255 || sample.observed[1]==255 || sample.observed[2]==255;
            flag(w,"observed_has_255_channel",has255);
            const float brightness=surfaceInfos[sample.surface].si->lmBrightness;
            value(w,"material_lightmap_brightness",brightness);
            const bool subsampling=std::any_of(r.responses.begin(),r.responses.end(),[&](const auto& response) {
                return d.activeLights[response.light]->style==sample.style && response.subsampling!=0;
            });
            flag(w,"subsampling_requested",subsampling);
            if(r.cluster<0 || r.traceLimit) {
                ++unknownComparisons; text(w,"status","unknown_trace");
            }
            else {
                Vector3 color(0);
                if(auto found=total.find(sample.style);found!=total.end()) color=found->second;
                if(sample.slot==0) { color+=ambient; for(int a=0;a<3;++a) color[a]=std::max(color[a],minLight[a]); }
                vec(w,"hypothesis_linear_rgb",color);
                const Vector3 encoded=ColorToFloat(color,1,brightness);
                if(!finite(encoded) || std::any_of(encoded.data(),encoded.data()+3,[](float v) { return v<0 || v>=256; })) {
                    ++invalidEncoding; text(w,"status","unrepresentable_encoding");
                }
                else {
                    const Vector3b predicted=encoded;
                    text(w,"status",subsampling?"compared_without_requested_subsampling":"compared");
                    vec(w,"encoded_before_byte_conversion",encoded); rgb(w,"predicted_rgb",predicted);
                    w.Key("residual_bytes"); w.StartArray(); for(int a=0;a<3;++a) w.Int(int(predicted[a])-sample.observed[a]); w.EndArray();
                    allErrors.add(predicted,sample.observed); styleErrors[sample.style].add(predicted,sample.observed);
                    if(!has255) non255Errors.add(predicted,sample.observed);
                    if(subsampling) ++subsamplingComparisons;
                }
            }
            w.EndObject();
        }
        w.EndObject();
    } w.EndArray();
    if(d.baked.enabled) {
        w.Key("comparison_summary"); w.StartObject(); errors(w,"all_compared",allErrors); errors(w,"without_observed_255_channel",non255Errors);
        number(w,"unknown_trace",unknownComparisons); number(w,"unrepresentable_encoding",invalidEncoding);
        number(w,"compared_without_requested_subsampling",subsamplingComparisons);
        w.Key("by_style"); w.StartArray(); for(const auto& [style,e]:styleErrors) { w.StartObject(); number(w,"style",style); errors(w,"errors",e); w.EndObject(); } w.EndArray();
        w.EndObject();
    }
    if(d.fit.enabled) {
        w.Key("point_fit"); w.StartObject(); text(w,"status",fit.status); flag(w,"accepted",fit.accepted);
        text(w,"qualification","conditional_current_assets_and_encoding_not_original_author_metadata");
        text(w,"family","nonnegative_inverse_square_point_with_native_angle_and_extra_distance");
        text(w,"validation_split","one_in_five_hashed_atlas_blocks_never_used_for_search_or_stopping");
        number(w,"atlas_block_size",d.fit.blockSize); number(w,"style",d.fit.style);
        flag(w,"original_bake_settings_known",false); flag(w,"encoding_calibrated",false);
        flag(w,"allow_implicit_materials",d.fit.allowImplicitMaterials);
        vec(w,"mins",d.fit.mins); vec(w,"maxs",d.fit.maxs); value(w,"grid_spacing",d.fit.spacing);
        value(w,"max_intensity",d.fit.maxIntensity); number(w,"max_lights",d.fit.maxLights);
        number(w,"max_candidates",d.fit.maxCandidates); number(w,"refinement_steps",d.fit.refinementSteps);
        value(w,"min_improvement_rmse",d.fit.minImprovement); value(w,"max_rmse",d.fit.maxRMSE);
        number(w,"max_work",d.fit.maxWork); number(w,"work_used",fit.work); number(w,"grid_points",fit.gridPoints);
        number(w,"usable_candidates",fit.usableCandidates); number(w,"positions_tested",fit.positionsTested);
        number(w,"unknown_trace_evaluations",fit.unknownCandidates); number(w,"unrepresentable_evaluations",fit.encodingFailures);
        w.Key("exclusions"); w.StartObject(); for(const auto& [key,count]:fit.exclusions) number(w,key.c_str(),count); w.EndObject();
        const auto metric=[&](const char* name,const PointFitMetrics& m) {
            w.Key(name); w.StartObject(); number(w,"samples",m.samples);
            if(m.samples) { value(w,"mae_bytes",m.mae); value(w,"rmse_bytes",m.rmse); value(w,"maximum_error_bytes",m.maximum); }
            else for(const char* key:{"mae_bytes","rmse_bytes","maximum_error_bytes"}) { w.Key(key); w.Null(); }
            w.EndObject();
        };
        for(int split=0;split<2;++split) {
            w.Key(split==0?"training":"withheld"); w.StartObject();
            metric("baseline",fit.baseline[split]); metric("trial",fit.trial[split]); w.EndObject();
        }
        w.Key("initial_grid_alternatives"); w.StartArray();
        for(const auto& alternative:fit.trainingAlternatives) {
            w.StartObject(); vec(w,"origin",alternative.light.origin); vec(w,"linear_intensity_rgb",alternative.light.energy);
            value(w,"training_quantization_center_rmse",alternative.trainingRMSE); w.EndObject();
        } w.EndArray();
        w.Key("best_trial"); w.StartArray();
        for(const auto& source:fit.lights) {
            w.StartObject(); vec(w,"origin",source.origin); vec(w,"linear_intensity_rgb",source.energy);
            const float intensity=vector3_max_component(source.energy); value(w,"intensity",intensity);
            Vector3 color=intensity>0?source.energy/intensity:Vector3(0);
            if(colorsRGB) for(int a=0;a<3;++a) color[a]=std::clamp(Image_sRGBFloatFromLinearFloat(color[a]),0.f,1.f);
            vec(w,"color",color); number(w,"style",d.fit.style); number(w,"spawnflags",wolfLight?1:0);
            value(w,"extra_distance",extraDist); w.EndObject();
        } w.EndArray();
        w.Key("validation_observations"); w.StartArray();
        for(size_t i=0;i<fit.receivers.size();++i) {
            w.StartObject(); number(w,"sample",fit.receivers[i].sample); flag(w,"withheld",fit.receivers[i].withheld);
            if(i<fit.subsampling.size()) flag(w,"subsampling_requested",fit.subsampling[i]!=0);
            if(i<fit.prediction.size()) rgb(w,"trial_rgb",fit.prediction[i]);
            w.EndObject();
        } w.EndArray(); w.EndObject();
    }
    w.Key("limitations"); w.StartArray();
    for(const char* s:{
        "This is the current compiler's direct CPU forward model with current assets and explicit points/normals. Optional point fitting proposes a conditional explanation; no original author metadata or bake settings are recovered and no entities are exported.",
        "BSP entities are retained and proposals exist only in memory. MAP/SRF files are not read; shader scripts, images and referenced model assets use the selected filesystem settings. Missing shader text is unresolved material provenance, not proof that a shader was originally implicit.",
        "Surface metadata uses compiler defaults without the original SRF; patch lengths are recomputed from stored controls using the compiler's curve metric. Inline geometry uses surviving entity origins. Probe coordinates/normals are explicitly world-space; runtime poses, bake nudges, phong/bump normals and original tessellation settings are not reconstructed.",
        "Responses are pre-encoding direct contributions, separated by source/style. Ambient and minimum light are separate metadata. Bounce, dirt, floodlight, filtering, luxel reconstruction, supersampling, clamping and output encoding are not applied. Never subtract these values directly from stored RGB bytes.",
        "Surface emitters include their compiler-generated point/backsplash lights. Sun/sky attribution follows the generating surface; repeated shader skies follow ordinary compiler creation rules. Current assets may differ from those used by the original author.",
        "Zero responses are omitted only for sampled points. An unusable cluster or trace reaching its fixed node capacity has unknown illumination, not a measured zero; partial responses are discarded. Alpha/filter traces can request subsampling; the diagnostic does not supersample them.",
        "Pair/source/response limits and a 64 MiB report ceiling bound this diagnostic's retained data. Scene preparation and each trace use existing compiler algorithms; pair work is not a ray-step count or elapsed-time guarantee.",
        "No BSP, MAP, SRF, lightmap or generated shader output is written. Source/request hashes are rechecked before publishing the staged report. Concurrent changes to source, assets or destinations are unsupported."
    }) w.String(s);
    if(d.fit.enabled) for(const char* s:{
        "Point fitting holds retained lights, supplied proposals, current shader emitters and sun/sky fixed. Its nonnegative inverse-square point family does not calibrate encoding or model bounce, dirt, floodlight, filter reconstruction, spotlights or target links. A low residual does not prove a missing entity light or a unique solution.",
        "Candidate positions use a bounded BSP-world grid and local refinement. Colors/intensities minimize a pre-byte quantization-centre training objective; acceptance uses byte RMSE improvement and absolute RMSE limits in both training and withheld atlas blocks. This split withholds texel blocks, not independent scenes or assets, and supplies no calibrated confidence probability.",
        "Observed 255 channels and unknown baseline illumination are excluded from fitting. Missing shader text is unresolved unless implicit materials were explicitly allowed; known missing/default images prevent fitting. Existing assets may still differ from the original bake. best_trial remains a rejected trial unless accepted is true; inference is read-only and requires author review."
    }) w.String(s);
    if(d.baked.enabled) for(const char* s:{
        "Automatic internal-lightmap comparisons apply current compiler encoding, world ambient/minlight on slot zero, and current material brightness to the direct hypothesis. These are declared assumptions, not recovered or calibrated bake settings. Residuals are predicted minus stored bytes, not decoded irradiance or evidence of missing entity lights.",
        "Ambiguous/unresolved/boundary/zero-normal mappings and constant-UV regions are excluded. Vertex/grid/external/deluxe channels are not compared. Stored normals, an explicit normal offset and surviving model origins do not reconstruct original luxel nudges, normals, tessellation or discarded source ambient/shadow overrides.",
        "Different surfaces can reference the same stored texel. Error summaries weight observations equally and are not statistical confidence. A channel equal to 255 flags possible information loss; values below 255 do not prove an invertible transfer. Unknown traces and unrepresentable encodings are excluded from numeric errors, never counted as a match."
    }) w.String(s);
    w.EndArray(); w.EndObject(); stream.Put('\n'); stream.Flush(); output.commit();
    Sys_Printf("Light probes: %zu samples, %zu active sources, %zu nonzero responses\n",d.samples.size(),d.activeLights.size(),size_t(d.responses));
}
}
