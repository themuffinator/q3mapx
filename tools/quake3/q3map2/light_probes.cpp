// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "light_probes.h"
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
struct Sample { int surface; Vector3 position,normal; float offset=0; };
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
    size_t originalEntities=0;
    uint64_t pairLimit=5'000'000;
    std::atomic<size_t> next{0},responses{0};
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
    fields(doc,{"schema_version","samples","lights"}); integer(required(doc,"schema_version"),1,1);
    const auto& samples=required(doc,"samples");
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
    if(doc.HasMember("lights") && (!doc["lights"].IsArray() || doc["lights"].Size()>256)) throw std::runtime_error("At most 256 proposed probe lights are supported");
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
    unchanged(d.source,d.sourceIdentity,true); unchanged(d.request,d.requestIdentity,false);
    OutputFiles output; Stream stream(output.open(d.destination)); Writer w(stream);
    w.StartObject(); number(w,"schema_version",1); text(w,"status","direct_forward_observations_only");
    text(w,"game",g_game->arg); flag(w,"light_inference_performed",false);
    text(w,"source_sha256",d.sourceIdentity.sha.c_str()); text(w,"request_sha256",d.requestIdentity.sha.c_str());
    number(w,"generated_sources",generated); number(w,"active_sources",lights.size()); number(w,"culled_sources",generated-lights.size());
    number(w,"pair_tests_upper_bound",lights.size()*d.samples.size()); number(w,"pair_limit",d.pairLimit);
    number(w,"response_limit",maxResponses); number(w,"nonzero_responses",d.responses); number(w,"max_active_workers",32);
    number(w,"trace_node_capacity",MAX_TRACE_TEST_NODES); value(w,"cluster_tolerance",0.125);
    text(w,"coordinates","explicit_world_space_with_requested_normal_offset");
    vec(w,"world_ambient",ambient); vec(w,"world_minlight",minLight);
    w.Key("settings"); w.StartObject();
    for(const auto& [key,n]:std::array<std::pair<const char*,double>,12>{{{"point_scale",pointScale},{"spot_scale",spotScale},{"area_scale",areaScale},{"sky_scale",skyScale},
        {"gamma",lightmapGamma},{"compensation",lightmapCompensate},{"exposure",lightmapExposure},{"brightness",lightmapBrightness},{"contrast_factor",lightmapContrast},
        {"saturation",g_lightmapSaturation},{"maximum_light",maxLight},{"falloff_tolerance",falloffTolerance}}}) value(w,key,n);
    flag(w,"half_lambert",lightAngleHL); flag(w,"wolf",wolfLight); flag(w,"trace_occlusion",!noTrace); flag(w,"fast",fast); flag(w,"faster",faster);
    flag(w,"lightmaps_srgb",lightmapsRGB); flag(w,"textures_srgb",texturesRGB); flag(w,"entity_colors_srgb",colorsRGB); w.EndObject();
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
        w.EndObject();
    } w.EndArray();
    w.Key("limitations"); w.StartArray();
    for(const char* s:{
        "This is the current compiler's direct CPU forward model with current assets and explicit points/normals. No original lights, bake settings or source entities are inferred or exported.",
        "BSP entities are retained and proposals exist only in memory. MAP/SRF files are not read; shader scripts, images and referenced model assets use the selected filesystem settings. Missing shader text is unresolved material provenance, not proof that a shader was originally implicit.",
        "Surface metadata uses compiler defaults without the original SRF; patch lengths are recomputed from stored controls using the compiler's curve metric. Inline geometry uses surviving entity origins. Probe coordinates/normals are explicitly world-space; runtime poses, bake nudges, phong/bump normals and original tessellation settings are not reconstructed.",
        "Responses are pre-encoding direct contributions, separated by source/style. Ambient and minimum light are separate metadata. Bounce, dirt, floodlight, filtering, luxel reconstruction, supersampling, clamping and output encoding are not applied. Never subtract these values directly from stored RGB bytes.",
        "Surface emitters include their compiler-generated point/backsplash lights. Sun/sky attribution follows the generating surface; repeated shader skies follow ordinary compiler creation rules. Current assets may differ from those used by the original author.",
        "Zero responses are omitted only for sampled points. An unusable cluster or trace reaching its fixed node capacity has unknown illumination, not a measured zero; partial responses are discarded. Alpha/filter traces can request subsampling; the diagnostic does not supersample them.",
        "Pair/source/response limits and a 64 MiB report ceiling bound this diagnostic's retained data. Scene preparation and each trace use existing compiler algorithms; pair work is not a ray-step count or elapsed-time guarantee.",
        "No BSP, MAP, SRF, lightmap or generated shader output is written. Source/request hashes are rechecked before publishing the staged report. Concurrent changes to source, assets or destinations are unsupported."
    }) w.String(s);
    w.EndArray(); w.EndObject(); stream.Put('\n'); stream.Flush(); output.commit();
    Sys_Printf("Light probes: %zu samples, %zu active sources, %zu nonzero responses\n",d.samples.size(),d.activeLights.size(),size_t(d.responses));
}
}
