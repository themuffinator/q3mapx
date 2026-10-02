// SPDX-License-Identifier: GPL-3.0-or-later
#include "patch_review.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace workbench {
namespace {
using JSON=rapidjson::Value;
void fail(const char* text) { throw std::runtime_error(text); }
void check(const std::atomic_bool* cancel) { if(cancel && cancel->load()) fail("Report reading cancelled."); }
const JSON& field(const JSON& value,const char* key) {
    if(!value.IsObject() || !value.HasMember(key)) fail("Missing patch recovery report field.");
    return value[key];
}
QString text(const JSON& value) {
    if(!value.IsString()) fail("Invalid patch recovery report text.");
    return QString::fromUtf8(value.GetString(),value.GetStringLength());
}
quint64 count(const JSON& value) {
    if(!value.IsUint64() || value.GetUint64()>0x7fffffff) fail("Invalid patch recovery count.");
    return value.GetUint64();
}
bool boolean(const JSON& value) {
    if(!value.IsBool()) fail("Invalid patch recovery flag.");
    return value.GetBool();
}
QJsonObject object(const JSON& value) {
    if(!value.IsObject()) fail("Invalid patch recovery record.");
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); value.Accept(writer);
    if(buffer.GetSize()>65536) fail("Patch recovery record exceeds 64 KiB.");
    return QJsonDocument::fromJson(QByteArray(buffer.GetString(),int(buffer.GetSize()))).object();
}
void audit(const JSON& root,const std::atomic_bool* cancel) {
    std::vector<std::pair<const JSON*,unsigned>> stack{{&root,0}}; size_t visited=0;
    while(!stack.empty()) {
        if(!(visited%4096)) check(cancel);
        const auto [v,depth]=stack.back(); stack.pop_back();
        if(++visited>2'000'000 || depth>64) fail("Recovery report exceeds its structure limit.");
        if(v->IsObject()) {
            std::set<std::string_view> names;
            for(auto it=v->MemberBegin();it!=v->MemberEnd();++it) {
                const std::string_view name(it->name.GetString(),it->name.GetStringLength());
                if(name.size()>256 || name.find('\0')!=name.npos || !names.insert(name).second) fail("Duplicate or invalid recovery property.");
                stack.push_back({&it->value,depth+1});
            }
        } else if(v->IsArray()) for(const auto& child:v->GetArray()) stack.push_back({&child,depth+1});
        else if(v->IsString() && (v->GetStringLength()>16384 || std::string_view(v->GetString(),v->GetStringLength()).find('\0')!=std::string_view::npos))
            fail("Invalid recovery report string.");
        if(stack.size()>2'000'000-visited) fail("Recovery report exceeds its structure limit.");
    }
}
quint64 statuses(const JSON& values) {
    if(!values.IsObject() || values.MemberCount()>128) fail("Invalid patch recovery status totals.");
    quint64 total=0;
    for(auto it=values.MemberBegin();it!=values.MemberEnd();++it) total+=count(it->value);
    return total;
}
}

PatchReview readPatchReview(const QString& path,const std::atomic_bool* cancelled) {
    check(cancelled); QFile file(path);
    if(!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size()>64*1024*1024)
        fail("Choose a recovery report no larger than 64 MiB.");
    const auto bytes=file.read(64*1024*1024+1);
    if(file.error()!=QFile::NoError || bytes.size()!=file.size() || bytes.size()>64*1024*1024 || bytes.contains('\0'))
        fail("Cannot read a complete recovery report.");
    check(cancelled);
    rapidjson::Document doc; doc.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(bytes.constData(),size_t(bytes.size()));
    if(doc.HasParseError()) fail("Invalid recovery JSON.");
    audit(doc,cancelled);
    if(count(field(doc,"schema_version"))!=1) fail("Unsupported recovery report version.");
    PatchReview result; result.path=QFileInfo(path).absoluteFilePath();
    result.input=text(field(doc,"input")); result.output=text(field(doc,"output")); result.game=text(field(doc,"game"));
    if(!QStringList{"map","map_bp","map_220"}.contains(text(field(doc,"format")))) fail("Choose a MAP recovery report.");
    if(doc.HasMember("patch_recovery")) {
        const auto& source=doc["patch_recovery"]; result.policy=text(field(source,"policy"));
        if(!QStringList{"source","fit","auto"}.contains(result.policy)) fail("Unsupported patch recovery policy in report.");
        result.archived=count(field(source,"restored_source_patches")); result.binding=boolean(field(source,"geometry_binding_verified"));
        if((result.archived || result.policy=="source") && !result.binding) fail("Restored patch sources lack a verified binding.");
        if(text(field(source,"basis"))!=(result.binding?"retained_pre_tessellation_source_archive":"compiled_triangle_samples"))
            fail("Unsupported patch source evidence basis.");
        if(boolean(field(source,"author_identity_authenticated")) || boolean(field(source,"original_compile_context_restored")))
            fail("Unsupported original-source claim in report.");
        if(result.archived) result.decisions.append({"Source archive","retained",QString::number(result.archived)+" patches","Authored controls and settings",
            {{"restored_source_patches",double(result.archived)},{"geometry_binding_verified",result.binding},
             {"basis","retained_pre_tessellation_source_archive"},{"original_compile_context_restored",false},{"author_identity_authenticated",false}}});
        if(source.HasMember("triangle_fitting")) {
            const auto& fit=source["triangle_fitting"];
            if(result.policy=="source" || boolean(field(fit,"original_source_proven")) || boolean(field(fit,"rebuild_equivalence_proven")))
                fail("Unsupported triangle fitting claim.");
            if(text(field(fit,"basis"))!="complete_affine_uv_grid_and_verified_quadratic_samples"
                || text(field(fit,"density_basis"))!="inherit_not_inferred"
                || text(field(fit,"subdivisions_basis"))!="observed_sample_grid_not_author_setting")
                fail("Unsupported triangle fitting evidence basis.");
            result.colors=text(field(fit,"color_policy")); result.fitted=count(field(fit,"fitted_patches"));
            const auto& decisions=field(fit,"decisions"); const auto omitted=count(field(fit,"omitted_records"));
            if(!decisions.IsArray() || decisions.Size()>10000 || statuses(field(fit,"counts"))!=decisions.Size()+omitted)
                fail("Inconsistent triangle fitting decisions.");
            const auto limit=count(field(fit,"work_limit"));
            if(!limit || limit>1'000'000'000 || count(field(fit,"work_used"))>limit) fail("Invalid fitting work accounting.");
            if(result.fitted>(fit["counts"].HasMember("fitted")?count(fit["counts"]["fitted"]):0))
                fail("More exported patches than accepted triangle fits.");
            result.omitted+=omitted;
            std::map<QString,quint64> observed;
            for(const auto& decision:decisions.GetArray()) {
                check(cancelled); const auto status=text(field(decision,"status"));
                if(status.isEmpty() || status.size()>128) fail("Invalid fitting status.");
                const auto key=status.toUtf8();
                if(!fit["counts"].HasMember(key.constData()) || ++observed[status]>count(fit["counts"][key.constData()])) fail("Fitting statuses disagree with totals.");
                const auto& surfaces=field(decision,"surfaces");
                if(!surfaces.IsArray() || surfaces.Size()>64) fail("Invalid fitting surface list.");
                QStringList ids; for(const auto& s:surfaces.GetArray()) ids<<QString::number(count(s));
                const auto omittedSurfaces=count(field(decision,"omitted_surfaces"));
                if(omittedSurfaces) ids<<QString("+%1 omitted").arg(omittedSurfaces);
                const auto width=count(field(decision,"width")),height=count(field(decision,"height")),sub=count(field(decision,"subdivisions"));
                if(width>31 || height>31 || sub>32 || (status=="fitted" && (width<3 || height<3 || !(width&1) || !(height&1) || sub<4 || (sub&(sub-1)))))
                    fail("Invalid fitted patch dimensions.");
                for(const auto* key:{"samples","triangles"}) count(field(decision,key));
                for(const auto* key:{"max_position_error","max_uv_error"}) {
                    const auto& v=field(decision,key);
                    if(!v.IsNumber() || !std::isfinite(v.GetDouble()) || v.GetDouble()<0) fail("Invalid fitting error.");
                }
                result.decisions.append({"Triangle fit",status,QString("Model %1 · surfaces %2").arg(count(field(decision,"model"))).arg(ids.join(", ")),
                    status=="fitted"?QString("%1 × %2 · %3 segments").arg(width).arg(height).arg(sub):QString(),object(decision)});
            }
            result.skipped+=statuses(field(fit,"counts"))-(field(fit,"counts").HasMember("fitted")?count(fit["counts"]["fitted"]):0);
        } else if(result.policy=="fit" || result.policy=="auto") fail("Missing triangle fitting decisions.");
    }
    if(doc.HasMember("patch_colors")) {
        const auto& colors=doc["patch_colors"]; const auto policy=text(field(colors,"policy"));
        if(!QStringList{"alpha","rgba"}.contains(policy)) fail("Unknown native patch color policy.");
        if(doc.HasMember("patch_recovery") && doc["patch_recovery"].HasMember("triangle_fitting") && result.colors!=policy)
            fail("Conflicting patch color policies.");
        result.colors=policy;
        if(boolean(field(colors,"original_paint_proven")) || boolean(field(colors,"rebuild_equivalence_proven"))) fail("Unsupported compiled-color source claim.");
        if(text(field(colors,"basis"))!="compiled_native_patch_control_channels"
            || text(field(colors,"settings_basis"))!="chosen_for_export_not_recovered_author_metadata")
            fail("Unsupported native patch evidence basis.");
        const auto& rows=field(colors,"patches"); const auto omitted=count(field(colors,"omitted_records"));
        const auto& counts=field(colors,"counts"); const auto total=statuses(counts);
        if(!rows.IsArray() || rows.Size()>10000 || total!=rows.Size()+omitted) fail("Inconsistent native patch decisions.");
        result.native=counts.HasMember("recovered")?count(counts["recovered"]):0; result.skipped+=total-result.native; result.omitted+=omitted;
        std::map<QString,quint64> observed;
        for(const auto& row:rows.GetArray()) {
            check(cancelled);
            const auto status=text(field(row,"status")); if(status.isEmpty() || status.size()>128) fail("Invalid native patch status.");
            const auto key=status.toUtf8();
            if(!counts.HasMember(key.constData()) || ++observed[status]>count(counts[key.constData()])) fail("Native patch statuses disagree with totals.");
            result.decisions.append({"Native controls",status,QString("Surface %1").arg(count(field(row,"surface"))),QString(),object(row)});
        }
    }
    if(!QStringList{"none","alpha","rgba"}.contains(result.colors)) fail("Unknown recovered color policy.");
    result.reportHash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    check(cancelled); return result;
}
PatchReviewReader::~PatchReviewReader() { if(cancelled_) cancelled_->store(true); }
void PatchReviewReader::refresh(const QString& path) {
    ++generation_; if(cancelled_) cancelled_->store(true);
    path_=path; result_={}; error_.clear(); loading_=!path.isEmpty(); emit changed();
    if(loading_ && !worker_) start();
}
void PatchReviewReader::cancel() {
    ++generation_; if(cancelled_) cancelled_->store(true);
    loading_=false; result_={}; error_="Report reading cancelled."; emit changed();
}
void PatchReviewReader::start() {
    struct Reply { PatchReview result; QString error; }; auto reply=std::make_shared<Reply>();
    cancelled_=std::make_shared<std::atomic_bool>(false); const auto flag=cancelled_; const auto generation=generation_;
    auto* worker=QThread::create([reply,flag,path=path_]{
        try { reply->result=readPatchReview(path,flag.get()); } catch(const std::exception& e) { reply->error=QString::fromUtf8(e.what()); }
    }); worker_=worker;
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    connect(worker,&QThread::finished,this,[this,reply,generation]{
        worker_=nullptr; if(generation!=generation_) { if(loading_) start(); return; }
        result_=reply->result; error_=reply->error; loading_=false; emit changed();
    }); worker->start();
}
}
