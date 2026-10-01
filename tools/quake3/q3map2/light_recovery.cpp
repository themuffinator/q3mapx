// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "light_recovery.h"
#include "rapidjson/document.h"
#include <glib.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace q3mapx {
namespace {
using JSON=rapidjson::Value;
constexpr uint64_t reportLimit=64*1024*1024,bspLimit=0x7fffffff;
struct Identity { uint64_t bytes; std::string sha; };
std::filesystem::path pathOf(const char* value) {
    return std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(value))).lexically_normal();
}
bool sameFile(const std::filesystem::path& a,const std::filesystem::path& b) {
    std::error_code ec;
    return std::filesystem::weakly_canonical(a)==std::filesystem::weakly_canonical(b) || std::filesystem::equivalent(a,b,ec);
}
Identity identify(const std::filesystem::path& path,uint64_t limit,bool bsp=false) {
    if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Light recovery input must be a regular file");
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<0 || uint64_t(input.tellg())>limit) throw std::runtime_error("Light recovery input is unavailable or exceeds its byte limit");
    const auto bytes=uint64_t(input.tellg()); input.seekg(0);
    std::unique_ptr<GChecksum,decltype(&g_checksum_free)> hash(g_checksum_new(G_CHECKSUM_SHA256),g_checksum_free);
    std::array<unsigned char,65536> block{}; uint64_t total=0;
    while(input) {
        input.read(reinterpret_cast<char*>(block.data()),block.size()); const size_t count=size_t(input.gcount());
        if(bsp && total==0 && (count<8 || std::memcmp(block.data(),g_game->bspIdent,4)
            || (uint32_t(block[4])|uint32_t(block[5])<<8|uint32_t(block[6])<<16|uint32_t(block[7])<<24)!=uint32_t(g_game->bspVersion)))
            throw std::runtime_error("Light recovery source does not match the selected native BSP profile");
        if(count) g_checksum_update(hash.get(),block.data(),count);
        total+=count; if(total>bytes) throw std::runtime_error("Light recovery input changed while reading");
    }
    if(!input.eof() || total!=bytes) throw std::runtime_error("Incomplete light recovery input");
    return {bytes,g_checksum_get_string(hash.get())};
}
const JSON& required(const JSON& value,const char* key) {
    if(!value.IsObject() || !value.HasMember(key)) throw std::runtime_error(std::string("Missing light recovery field: ")+key);
    return value[key];
}
std::string_view string(const JSON& value) {
    if(!value.IsString()) throw std::runtime_error("Light recovery field must be a string");
    return {value.GetString(),value.GetStringLength()};
}
void expect(const JSON& value,std::string_view wanted) {
    if(string(value)!=wanted) throw std::runtime_error("Light recovery report has an unsupported or inconsistent status/family");
}
bool boolean(const JSON& value) {
    if(!value.IsBool()) throw std::runtime_error("Light recovery field must be boolean");
    return value.GetBool();
}
double number(const JSON& value,double lo,double hi) {
    if(!value.IsNumber() || !std::isfinite(value.GetDouble()) || value.GetDouble()<lo || value.GetDouble()>hi)
        throw std::runtime_error("Light recovery numeric field is outside its supported range");
    return value.GetDouble();
}
float nativeNumber(const JSON& value,double lo,double hi) {
    const double n=number(value,lo,hi); const float result=float(n);
    if(!std::isfinite(result) || (n!=0 && result==0)) throw std::runtime_error("Light recovery value cannot be represented as a native float");
    return result;
}
size_t integer(const JSON& value,size_t lo,size_t hi) {
    if(!value.IsUint64() || value.GetUint64()<lo || value.GetUint64()>hi)
        throw std::runtime_error("Light recovery integer field is outside its supported range");
    return size_t(value.GetUint64());
}
const JSON& array(const JSON& value,size_t lo,size_t hi) {
    if(!value.IsArray() || value.Size()<lo || value.Size()>hi) throw std::runtime_error("Light recovery array has an unsupported size");
    return value;
}
Vector3 vector(const JSON& value,double lo=-1e6,double hi=1e6) {
    array(value,3,3); Vector3 result;
    for(int a=0;a<3;++a) {
        const double v=number(value[a],lo,hi); result[a]=float(v);
        if(!std::isfinite(result[a]) || (v!=0 && result[a]==0)) throw std::runtime_error("Light recovery value cannot be represented as a native float");
    }
    return result;
}
Vector3b rgb(const JSON& value) {
    array(value,3,3); return Vector3b(byte(integer(value[0],0,255)),byte(integer(value[1],0,255)),byte(integer(value[2],0,255)));
}
void fields(const JSON& value,std::initializer_list<std::string_view> allowed) {
    if(!value.IsObject()) throw std::runtime_error("Light recovery record must be an object");
    for(auto it=value.MemberBegin();it!=value.MemberEnd();++it)
        if(std::find(allowed.begin(),allowed.end(),string(it->name))==allowed.end())
            throw std::runtime_error("Unknown light proposal field: "+std::string(string(it->name)));
}
// Validate the whole document before selecting fields. In particular duplicate
// accepted/status/target keys must not acquire parser-dependent meanings.
void audit(const JSON& root) {
    std::vector<std::pair<const JSON*,size_t>> pending{{&root,0}}; size_t nodes=0;
    while(!pending.empty()) {
        const auto [value,depth]=pending.back(); pending.pop_back();
        if(++nodes>2'000'000 || depth>64) throw std::runtime_error("Light recovery report exceeds its structure limit");
        if(value->IsObject()) {
            std::set<std::string_view> names;
            for(auto it=value->MemberBegin();it!=value->MemberEnd();++it) {
                const auto key=string(it->name);
                if(key.size()>256 || key.find('\0')!=std::string_view::npos || !names.insert(key).second)
                    throw std::runtime_error("Duplicate or invalid light recovery field");
                pending.push_back({&it->value,depth+1});
            }
        }
        else if(value->IsArray()) for(const auto& item:value->GetArray()) pending.push_back({&item,depth+1});
        else if(value->IsString() && (value->GetStringLength()>16384 || string(*value).find('\0')!=std::string_view::npos))
            throw std::runtime_error("Invalid light recovery string");
        if(pending.size()>2'000'000-nodes) throw std::runtime_error("Light recovery report exceeds its structure limit");
    }
}
std::string printableName(const JSON& value) {
    const auto name=string(value);
    if(name.empty() || name.size()>1023 || std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c==127 || c=='"' || c=='\\';}))
        throw std::runtime_error("Light target name is not safely representable in MAP syntax");
    return std::string(name);
}
std::string decimal(float value) {
    std::ostringstream out; out.imbue(std::locale::classic()); out.precision(std::numeric_limits<float>::max_digits10); out<<value; return out.str();
}
std::string coordinates(const Vector3& value) { return decimal(value[0])+" "+decimal(value[1])+" "+decimal(value[2]); }
struct Generated {
    const char* role;
    size_t proposal;
    std::vector<std::pair<std::string,std::string>> keys;
    void put(const char* key,const std::string& value) { keys.emplace_back(key,value); }
};
struct Metric {
    size_t samples=0; double absolute=0,squared=0,maximum=0;
    void add(const Vector3b& predicted,const Vector3b& observed) {
        ++samples;
        for(int a=0;a<3;++a) { const double e=double(predicted[a])-observed[a]; absolute+=std::abs(e); squared+=e*e; maximum=std::max(maximum,std::abs(e)); }
    }
    double rmse() const { return samples?std::sqrt(squared/(3*samples)):0; }
    void check(const JSON& record) const {
        if(integer(required(record,"samples"),0,10000)!=samples) throw std::runtime_error("Light recovery metric sample count is inconsistent");
        for(const auto& [key,expected]:std::array<std::pair<const char*,double>,3>{{{"mae_bytes",samples?absolute/(3*samples):0},{"rmse_bytes",rmse()},{"maximum_error_bytes",maximum}}}) {
            const double actual=number(required(record,key),0,255);
            if(std::abs(actual-expected)>1e-9*std::max(1.,expected)) throw std::runtime_error("Light recovery score is inconsistent with its observations");
        }
    }
};
}

struct LightRecovery::Data {
    std::filesystem::path source,report;
    Identity sourceIdentity{},reportIdentity{};
    rapidjson::Document document;
    bool spot=false;
    size_t sourceEntities=0,fixedLights=0,fittedLights=0,retainedLinks=0;
    std::vector<Generated> generated;
    std::set<std::string> names;
    const JSON& fit() const { return document[spot?"spot_fit":"point_fit"]; }
    void verify() const {
        const auto a=identify(source,bspLimit,true),b=identify(report,reportLimit);
        if(a.bytes!=sourceIdentity.bytes || a.sha!=sourceIdentity.sha || b.bytes!=reportIdentity.bytes || b.sha!=reportIdentity.sha)
            throw std::runtime_error("Light recovery input changed; outputs were not published");
    }
    void checkScores() const {
        const auto& fit=this->fit();
        const auto& samples=array(required(document,"samples"),1,10000);
        const auto& observations=array(required(fit,"validation_observations"),1,10000);
        const size_t block=integer(required(fit,"atlas_block_size"),4,512),style=integer(required(fit,"style"),0,253);
        const double improvement=number(required(fit,"min_improvement_rmse"),.01,64),ceiling=number(required(fit,"max_rmse"),.01,64);
        const size_t pageSize=g_game->lightmapSize,pageBytes=pageSize*pageSize*3;
        if(!pageBytes || bspLightBytes.empty() || bspLightBytes.size()%pageBytes) throw std::runtime_error("Light recovery needs complete internal lightmaps");
        std::array<Metric,2> baseline,trial,litBaseline,litTrial;
        std::set<size_t> seen;
        for(const auto& observation:observations.GetArray()) {
            const size_t index=integer(required(observation,"sample"),0,samples.Size()-1);
            if(!seen.insert(index).second) throw std::runtime_error("Duplicate light recovery validation sample");
            const auto& sample=samples[rapidjson::SizeType(index)];
            if(integer(required(sample,"index"),0,10000)!=index) throw std::runtime_error("Light recovery sample index is inconsistent");
            expect(required(sample,"status"),"sampled");
            const auto& baked=required(sample,"baked_lightmap"); expect(required(baked,"status"),"compared");
            const size_t surface=integer(required(sample,"surface"),0,bspDrawSurfaces.size()-1);
            const auto& ds=bspDrawSurfaces[surface];
            const size_t slot=integer(required(baked,"slot"),0,MAX_LIGHTMAPS-1);
            const size_t page=integer(required(baked,"page"),0,bspLightBytes.size()/pageBytes-1);
            const auto& texel=array(required(baked,"texel"),2,2);
            const size_t x=integer(texel[0],0,pageSize-1),y=integer(texel[1],0,pageSize-1);
            if(integer(required(baked,"style"),0,253)!=style || ds.lightmapStyles[slot]!=style || ds.lightmapNum[slot]!=int(page))
                throw std::runtime_error("Light recovery style/page does not match its BSP surface");
            const auto observed=rgb(required(baked,"observed_rgb")),base=rgb(required(baked,"predicted_rgb")),predicted=rgb(required(observation,"trial_rgb"));
            for(int a=0;a<3;++a) if(observed[a]==255 || observed[a]!=bspLightBytes[page*pageBytes+(y*pageSize+x)*3+a])
                throw std::runtime_error("Light recovery observation does not match usable source BSP texels");
            uint64_t hash=14695981039346656037ull;
            for(uint64_t value:{uint64_t(page),uint64_t(x/block),uint64_t(y/block)}) { hash^=value; hash*=1099511628211ull; }
            const bool withheld=boolean(required(observation,"withheld"));
            if(withheld!=(hash%5==0)) throw std::runtime_error("Light recovery validation split is inconsistent");
            baseline[withheld].add(base,observed); trial[withheld].add(predicted,observed);
            if(spot) {
                double positive=0;
                for(int a=0;a<3;++a) { const double e=std::max(0,int(observed[a])-int(base[a])); positive+=e*e; }
                const bool lit=positive>3*improvement*improvement;
                if(boolean(required(observation,"illuminated_support"))!=lit) throw std::runtime_error("Light recovery illuminated support is inconsistent");
                if(lit) { litBaseline[withheld].add(base,observed); litTrial[withheld].add(predicted,observed); }
            }
        }
        for(size_t i=0;i<2;++i) {
            const auto& split=required(fit,i?"withheld":"training");
            baseline[i].check(required(split,"baseline")); trial[i].check(required(split,"trial"));
            if(trial[i].samples<(i?12u:24u) || trial[i].rmse()>ceiling || baseline[i].rmse()-trial[i].rmse()<improvement)
                throw std::runtime_error("Light recovery observations fail the qualification gates");
            if(spot) {
                litBaseline[i].check(required(split,"illuminated_baseline")); litTrial[i].check(required(split,"illuminated_trial"));
                if(litTrial[i].samples<(i?6u:12u) || litTrial[i].rmse()>ceiling || litBaseline[i].rmse()-litTrial[i].rmse()<improvement)
                    throw std::runtime_error("Light recovery illuminated observations fail the qualification gates");
            }
        }
    }
    void reserveNames() {
        for(const auto& entity:entities) {
            if(const char* name=entity.valueForKey("targetname"); *name) names.insert(name);
            for(const auto& pair:entity.epairs) {
                const std::string_view value=pair.value.c_str(); size_t offset=0;
                while((offset=value.find("_q3mapx_",offset))!=std::string_view::npos) {
                    size_t end=offset+8;
                    while(end<value.size() && ((value[end]>='0' && value[end]<='9') || (value[end]>='a' && value[end]<='z')
                        || (value[end]>='A' && value[end]<='Z') || value[end]=='_')) ++end;
                    names.emplace(value.substr(offset,end-offset)); offset=end;
                }
            }
        }
    }
    void marker(const std::string& name,const Vector3& origin,const char* role,size_t index) {
        generated.push_back({role,index,{{"classname","info_null"},{"origin",coordinates(origin)},{"targetname",name}}});
    }
    void fitted(const JSON& value,size_t index) {
        Generated light{"fitted_light",index,{{"classname","light"}}};
        const auto origin=vector(required(value,"origin")),color=vector(required(value,"color"),0,1),energy=vector(required(value,"linear_intensity_rgb"),0,1e6);
        const float intensity=float(number(required(value,"intensity"),0,1e6));
        if(intensity<=0 || intensity!=vector3_max_component(energy)) throw std::runtime_error("Inconsistent fitted light intensity");
        const auto& settings=required(document,"settings");
        for(int a=0;a<3;++a) {
            const float linear=boolean(required(settings,"entity_colors_srgb"))?Image_LinearFloatFromsRGBFloat(color[a]):color[a];
            if(std::abs(linear*intensity-energy[a])>1e-5*std::max(1.f,intensity)) throw std::runtime_error("Inconsistent fitted light color/energy");
        }
        const size_t style=integer(required(value,"style"),0,253),flags=integer(required(value,"spawnflags"),0,1);
        if(style!=integer(required(fit(),"style"),0,253) || flags!=size_t(boolean(required(settings,"wolf"))))
            throw std::runtime_error("Fitted light style/flags do not match the recorded hypothesis");
        light.put("origin",coordinates(origin)); light.put("_color",coordinates(color)); light.put("_light",decimal(intensity));
        light.put("style",std::to_string(style)); light.put("spawnflags",std::to_string(flags));
        light.put("_extradist",decimal(float(number(required(value,"extra_distance"),0,1e6))));
        if(spot) {
            const auto target=vector(required(value,"target")),direction=vector(required(value,"direction"),-1,1);
            const float radius=float(number(required(value,"radius"),0,1e6));
            const double slope=number(required(value,"radius_by_distance"),0,100),angle=number(required(value,"half_angle_degrees"),1,85);
            Vector3 actual=target-origin; const float distance=VectorNormalize(actual);
            if(radius<=0 || distance<1 || vector3_length(actual-direction)>2e-5
                || std::abs((radius+16)/distance-slope)>1e-5*std::max(1.,slope)
                || std::abs(radians_to_degrees(std::atan(slope))-angle)>1e-4)
                throw std::runtime_error("Fitted spotlight target/cone is inconsistent or not representable");
            const auto& link=required(value,"target_link"); const std::string name=printableName(required(link,"targetname"));
            if(boolean(required(link,"input_entity_modified")) || vector(required(link,"origin"))!=target)
                throw std::runtime_error("Fitted spotlight target metadata is inconsistent");
            const auto& entity=required(link,"bsp_entity");
            if(entity.IsNull()) {
                expect(required(link,"status"),"new_inferred_marker_proposal"); expect(required(link,"classname"),"info_null");
                constexpr std::string_view prefix="_q3mapx_inferred_target_";
                if(!name.starts_with(prefix) || name.size()==prefix.size()
                    || !std::all_of(name.begin()+prefix.size(),name.end(),[](char c){return c>='0' && c<='9';}) || !names.insert(name).second)
                    throw std::runtime_error("Inferred spotlight target collides with a surviving name/reference or another proposal");
                marker(name,target,"inferred_target",index);
            }
            else {
                expect(required(link,"status"),"retained_static_marker_proposal");
                const auto& retained=entities[integer(entity,1,entities.size()-1)];
                if(!(retained.classname_is("info_null") || retained.classname_is("target_position"))
                    || name!=retained.valueForKey("targetname") || !*retained.valueForKey("origin") || retained.vectorForKey("origin")!=target
                    || *retained.valueForKey("model") || *retained.valueForKey("target") || *retained.valueForKey("target2")
                    || std::count_if(entities.begin(),entities.end(),[&](const auto& e){return name==e.valueForKey("targetname");})!=1)
                    throw std::runtime_error("Retained spotlight target is missing, ambiguous or incompatible");
                ++retainedLinks;
            }
            light.put("target",name); light.put("radius",decimal(radius));
        }
        else if(value.HasMember("target") || value.HasMember("target_link")) throw std::runtime_error("Point proposal unexpectedly contains a spotlight target");
        generated.push_back(std::move(light)); ++fittedLights;
    }
    void fixed(const JSON& value,size_t index,float extraDistance) {
        fields(value,{"origin","intensity","color","spawnflags","fade","angle_scale","extra_distance","style","target","radius","sun"});
        Generated light{"fixed_hypothesis_light",index,{{"classname","light"},{"origin",coordinates(vector(required(value,"origin")))}}};
        if(value.HasMember("color")) light.put("_color",coordinates(vector(value["color"],0,1)));
        for(const auto& [input,key]:std::array<std::pair<const char*,const char*>,5>{{{"intensity","_light"},{"fade","fade"},{"angle_scale","_anglescale"},{"extra_distance","_extradist"},{"radius","radius"}}})
            if(value.HasMember(input)) light.put(key,decimal(nativeNumber(value[input],std::string_view(input)=="intensity"?-1e6:0,1e6)));
        if(!value.HasMember("extra_distance")) light.put("_extradist",decimal(extraDistance));
        if(value.HasMember("style")) light.put("style",std::to_string(integer(value["style"],0,253)));
        if(value.HasMember("spawnflags")) light.put("spawnflags",std::to_string(integer(value["spawnflags"],0,127)));
        if(value.HasMember("sun")) light.put("_sun",boolean(value["sun"])?"1":"0");
        if(value.HasMember("target")) {
            size_t suffix=0; std::string name;
            do { name="_q3mapx_fixed_target_"+std::to_string(suffix++); } while(!names.insert(name).second);
            marker(name,vector(value["target"]),"fixed_hypothesis_target",index); light.put("target",name);
        }
        else if(value.HasMember("sun") && boolean(value["sun"])) throw std::runtime_error("Fixed sun proposal requires a target");
        generated.push_back(std::move(light)); ++fixedLights;
    }
};

LightRecovery::LightRecovery(std::unique_ptr<Data> data):data_(std::move(data)){}
LightRecovery::~LightRecovery()=default;
std::shared_ptr<LightRecovery> LightRecovery::load(const char* bsp,const char* report) {
    auto data=std::make_unique<Data>(); data->source=pathOf(bsp); data->report=pathOf(report);
    data->sourceIdentity=identify(data->source,bspLimit,true); data->reportIdentity=identify(data->report,reportLimit);
    std::ifstream input(data->report,std::ios::binary); std::string content(size_t(data->reportIdentity.bytes),'\0');
    if(!input.read(content.data(),std::streamsize(content.size())) || content.find('\0')!=std::string::npos)
        throw std::runtime_error("Incomplete or invalid light proposal report");
    auto& doc=data->document;
    doc.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(content.data(),content.size());
    if(doc.HasParseError()) throw std::runtime_error("Invalid light proposal JSON at byte "+std::to_string(doc.GetErrorOffset()));
    audit(doc); integer(required(doc,"schema_version"),1,1);
    if(string(required(doc,"source_sha256"))!=data->sourceIdentity.sha) throw std::runtime_error("Light report does not match the source BSP SHA-256");
    if(string(required(doc,"game"))!=g_game->arg) throw std::runtime_error("Light report does not match the selected game profile");
    if(!boolean(required(doc,"light_inference_performed")) || doc.HasMember("point_fit")==doc.HasMember("spot_fit"))
        throw std::runtime_error("A single qualified fitting result is required for light export");
    data->spot=doc.HasMember("spot_fit");
    expect(required(doc,"status"),data->spot?"spot_fitting_diagnostic":"point_fitting_diagnostic");
    const auto& fit=data->fit();
    if(!boolean(required(fit,"accepted"))) throw std::runtime_error("Unqualified light trials cannot be exported");
    expect(required(fit,"status"),data->spot?"conditional_spot_proposal":"conditional_point_proposal");
    expect(required(fit,"family"),data->spot?"nonnegative_native_spot_with_inverse_square_angle_and_extra_distance":"nonnegative_inverse_square_point_with_native_angle_and_extra_distance");
    expect(required(fit,"qualification"),"conditional_current_assets_and_encoding_not_original_author_metadata");
    expect(required(fit,"validation_split"),"one_in_five_hashed_atlas_blocks_never_used_for_search_or_stopping");
    array(required(fit,"best_trial"),1,integer(required(fit,"max_lights"),1,16));
    if(!required(doc,"settings").IsObject()) throw std::runtime_error("Missing light recovery settings");
    data->verify();
    return std::shared_ptr<LightRecovery>(new LightRecovery(std::move(data)));
}
void LightRecovery::prepare() {
    auto& d=*data_; d.verify();
    if(entities.empty() || bspDrawSurfaces.empty()) throw std::runtime_error("Light recovery needs world entities and lightmapped surfaces");
    d.sourceEntities=entities.size(); d.checkScores(); d.reserveNames();
    const auto& lights=required(d.fit(),"best_trial");
    const float extra=float(number(required(lights[0],"extra_distance"),0,1e6));
    for(size_t i=0;i<lights.Size();++i) {
        if(number(required(lights[rapidjson::SizeType(i)],"extra_distance"),0,1e6)!=extra)
            throw std::runtime_error("Fitted lights disagree on the global extra-distance hypothesis");
        d.fitted(lights[rapidjson::SizeType(i)],i);
    }
    auto fitted=std::move(d.generated); d.generated.clear();
    const auto& proposed=array(required(d.document,"proposed_entity_indices"),0,256);
    if(d.document.HasMember("fixed_proposals")) {
        const auto& fixed=array(d.document["fixed_proposals"],proposed.Size(),proposed.Size());
        size_t expected=entities.size();
        for(size_t i=0;i<fixed.Size();++i) {
            const auto& item=fixed[rapidjson::SizeType(i)];
            if(!item.IsObject()) throw std::runtime_error("Fixed light proposal must be an object");
            if(item.HasMember("target")) ++expected;
            integer(proposed[rapidjson::SizeType(i)],expected,expected); ++expected;
            d.fixed(item,i,extra);
        }
    }
    else if(!proposed.Empty()) throw std::runtime_error("Report omits fixed proposal dependencies; regenerate it with this compiler");
    for(const auto& source:array(required(d.document,"sources"),0,16384).GetArray()) {
        const auto& index=required(source,"proposed_light");
        if(!index.IsNull()) {
            if(proposed.Empty()) throw std::runtime_error("Report omits an active fixed proposal dependency");
            integer(index,0,proposed.Size()-1);
            if(!required(source,"bsp_entity").IsNull()) throw std::runtime_error("A fixed proposal cannot also be a retained BSP entity");
        }
    }
    d.generated.insert(d.generated.end(),std::make_move_iterator(fitted.begin()),std::make_move_iterator(fitted.end()));
}
void LightRecovery::protectOutputs(const char* map,const char* report) const {
    for(const char* output:{map,report}) for(const auto& input:{data_->source,data_->report})
        if(sameFile(pathOf(output),input)) throw std::runtime_error("Light recovery output aliases a source BSP or proposal report");
}
void LightRecovery::verifyInputs() const { data_->verify(); }
size_t LightRecovery::entityCount() const { return data_->generated.size(); }
void LightRecovery::writeEntities(FILE* file,size_t first) const {
    for(const auto& entity:data_->generated) {
        fprintf(file,"// entity %zu\n// q3mapx conditional light recovery: %s, proposal %zu; original authorship unproven.\n{\n",first++,entity.role,entity.proposal);
        for(const auto& [key,value]:entity.keys) fprintf(file,"\t\"%s\" \"%s\"\n",key.c_str(),value.c_str());
        fprintf(file,"}\n\n");
    }
}
void LightRecovery::writeReport(rapidjson::PrettyWriter<rapidjson::StringBuffer>& w,size_t first) const {
    const auto& d=*data_;
    w.Key("light_recovery"); w.StartObject(); w.Key("schema_version"); w.Int(1);
    w.Key("policy"); w.String("qualified_report_with_fixed_dependencies");
    w.Key("proposal_report"); const auto path=d.report.u8string(); w.String(reinterpret_cast<const char*>(path.c_str()));
    w.Key("proposal_sha256"); w.String(d.reportIdentity.sha.c_str());
    w.Key("source_sha256"); w.String(d.sourceIdentity.sha.c_str());
    w.Key("family"); required(d.fit(),"family").Accept(w);
    for(const char* key:{"original_author_lights_proven","target_identity_proven","forward_transport_revalidated","rebuilt_lighting_validated"}) { w.Key(key); w.Bool(false); }
    for(const char* key:{"source_identity_verified","stored_texels_and_scores_verified","existing_entities_retargeted"}) { w.Key(key); w.Bool(std::string_view(key)!="existing_entities_retargeted"); }
    w.Key("source_entities"); w.Uint64(d.sourceEntities);
    w.Key("fitted_lights"); w.Uint64(d.fittedLights); w.Key("fixed_hypothesis_lights"); w.Uint64(d.fixedLights);
    w.Key("retained_target_links"); w.Uint64(d.retainedLinks); w.Key("generated_entities"); w.Uint64(d.generated.size());
    w.Key("required_lighting_settings"); required(d.document,"settings").Accept(w);
    w.Key("extra_distance"); required(required(d.fit(),"best_trial")[0],"extra_distance").Accept(w);
    for(const char* key:{"training","withheld"}) { w.Key(key); required(d.fit(),key).Accept(w); }
    w.Key("emitted_entities"); w.StartArray();
    for(const auto& entity:d.generated) {
        w.StartObject(); w.Key("map_entity"); w.Uint64(first++); w.Key("role"); w.String(entity.role); w.Key("proposal_index"); w.Uint64(entity.proposal);
        w.Key("keys"); w.StartObject(); for(const auto& [key,value]:entity.keys) { w.Key(key.c_str()); w.String(value.c_str()); } w.EndObject(); w.EndObject();
    }
    w.EndArray(); w.EndObject();
}
}
