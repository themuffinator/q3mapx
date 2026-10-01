// SPDX-License-Identifier: GPL-3.0-or-later
#include "light_fit.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace workbench {
namespace {
void fail(const QString& text) { throw std::runtime_error(text.toStdString()); }
QString decimal(double value) { return QString::number(value,'g',17); }
void checkCancelled(const std::atomic_bool* value) { if(value && value->load()) fail("Report reading cancelled."); }
QString hashFile(const QString& path,qint64 limit,const std::atomic_bool* cancelled) {
    QFile file(path);
    if(!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size()>limit) fail("Source BSP is unavailable or exceeds 2 GiB.");
    QCryptographicHash hash(QCryptographicHash::Sha256); qint64 size=0;
    while(!file.atEnd()) {
        checkCancelled(cancelled);
        const auto bytes=file.read(65536);
        if(bytes.isEmpty() && file.error()!=QFile::NoError) fail("Cannot read source BSP.");
        size+=bytes.size(); if(size>limit) fail("Source BSP exceeds 2 GiB.");
        hash.addData(bytes);
    }
    if(size!=file.size()) fail("Source BSP changed while reading.");
    return QString::fromLatin1(hash.result().toHex());
}
using JSON=rapidjson::Value;
const JSON& field(const JSON& object,const char* key) {
    if(!object.IsObject() || !object.HasMember(key)) fail(QString("Missing light report field: ")+key);
    return object[key];
}
QString text(const JSON& value) {
    if(!value.IsString()) fail("Invalid light report string.");
    return QString::fromUtf8(value.GetString(),value.GetStringLength());
}
double number(const JSON& value,double lo,double hi) {
    if(!value.IsNumber() || !std::isfinite(value.GetDouble()) || value.GetDouble()<lo || value.GetDouble()>hi) fail("Invalid light report number.");
    return value.GetDouble();
}
quint64 integer(const JSON& value,quint64 hi) {
    if(!value.IsUint64() || value.GetUint64()>hi) fail("Invalid light report integer.");
    return value.GetUint64();
}
QJsonObject object(const JSON& value) {
    if(!value.IsObject()) fail("Invalid light report object.");
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); value.Accept(writer);
    if(buffer.GetSize()>65536) fail("Light report summary is too large.");
    return QJsonDocument::fromJson(QByteArray(buffer.GetString(),int(buffer.GetSize()))).object();
}
void audit(const JSON& root,const std::atomic_bool* cancelled) {
    std::vector<std::pair<const JSON*,size_t>> pending{{&root,0}}; size_t count=0;
    while(!pending.empty()) {
        if(!(count%4096)) checkCancelled(cancelled);
        const auto [value,depth]=pending.back(); pending.pop_back();
        if(++count>2'000'000 || depth>64) fail("Light report exceeds its structure limit.");
        if(value->IsObject()) {
            std::set<std::string_view> keys;
            for(auto it=value->MemberBegin();it!=value->MemberEnd();++it) {
                const std::string_view key(it->name.GetString(),it->name.GetStringLength());
                if(key.size()>256 || key.find('\0')!=key.npos || !keys.insert(key).second) fail("Duplicate or invalid light report property.");
                pending.push_back({&it->value,depth+1});
            }
        } else if(value->IsArray()) {
            for(const auto& child:value->GetArray()) pending.push_back({&child,depth+1});
        } else if(value->IsString() && (value->GetStringLength()>16384
                   || std::string_view(value->GetString(),value->GetStringLength()).find('\0')!=std::string_view::npos)) fail("Invalid light report string.");
        if(pending.size()>2'000'000-count) fail("Light report exceeds its structure limit.");
    }
}
}

QJsonObject LightFitSettings::toJson() const {
    return {{"family",family},{"stride",stride},{"max_lights",maxLights},{"refinement_steps",refinements},
        {"style",style},{"max_work",maxWork},{"grid_spacing",spacing},{"gamma",gamma},{"compensate",compensate},
        {"extra_distance",extraDistance},{"max_rmse",maxRMSE},{"min_improvement_rmse",minImprovement},
        {"wolf",wolf},{"lightmaps_srgb",lightmapsSRGB},{"textures_srgb",texturesSRGB},
        {"entity_colors_srgb",colorsSRGB},{"fixed_lights",fixedLights}};
}
LightFitSettings LightFitSettings::fromJson(const QJsonObject& o) {
    LightFitSettings result;
    if(o.contains("family")) { if(!o["family"].isString()) fail("Invalid light fitting family."); result.family=o["family"].toString(); }
    const auto integer=[&](const char* key,int& value){
        if(!o.contains(key)) return;
        if(!o[key].isDouble() || o[key].toDouble()!=o[key].toInt()) fail(QString("Invalid light fitting number: ")+key);
        value=o[key].toInt();
    };
    const auto real=[&](const char* key,double& value){
        if(!o.contains(key)) return;
        if(!o[key].isDouble() || !std::isfinite(o[key].toDouble())) fail(QString("Invalid light fitting number: ")+key);
        value=o[key].toDouble();
    };
    const auto flag=[&](const char* key,bool& value){
        if(!o.contains(key)) return;
        if(!o[key].isBool()) fail(QString("Invalid light fitting flag: ")+key);
        value=o[key].toBool();
    };
    integer("stride",result.stride); integer("max_lights",result.maxLights); integer("refinement_steps",result.refinements);
    integer("style",result.style); integer("max_work",result.maxWork);
    real("grid_spacing",result.spacing); real("gamma",result.gamma); real("compensate",result.compensate);
    real("extra_distance",result.extraDistance); real("max_rmse",result.maxRMSE); real("min_improvement_rmse",result.minImprovement);
    flag("wolf",result.wolf); flag("lightmaps_srgb",result.lightmapsSRGB); flag("textures_srgb",result.texturesSRGB); flag("entity_colors_srgb",result.colorsSRGB);
    if(o.contains("fixed_lights")) { if(!o["fixed_lights"].isArray()) fail("Fixed lights must be a JSON array."); result.fixedLights=o["fixed_lights"].toArray(); }
    const auto errors=result.validate(); if(!errors.isEmpty()) fail(errors.join('\n'));
    return result;
}
QStringList LightFitSettings::validate() const {
    QStringList errors;
    if(family!="point" && family!="spot") errors << "Choose point or spot fitting.";
    if(stride<1 || stride>256 || maxLights<1 || maxLights>16 || refinements<0 || refinements>10 || style<0 || style>253 || maxWork<1 || maxWork>1'000'000'000)
        errors << "Light fitting sample/search limits are outside their supported ranges.";
    const auto within=[](double v,double lo,double hi){ return std::isfinite(v) && v>=lo && v<=hi; };
    if(!within(spacing,1,1e6) || !within(gamma,.01,16) || !within(compensate,.01,64) || !within(extraDistance,0,1e6)
        || !within(maxRMSE,.01,64) || !within(minImprovement,.01,64)) errors << "Light fitting numeric settings are outside their supported ranges.";
    if(fixedLights.size()>256 || QJsonDocument(fixedLights).toJson(QJsonDocument::Compact).size()>65536)
        errors << "Fixed lights exceed 256 entries or 64 KiB.";
    for(const auto& light:fixedLights) if(!light.isObject()) { errors << "Each fixed light must be a JSON object."; break; }
    return errors;
}
QJsonObject LightFitSettings::request() const {
    QJsonObject result{{"schema_version",1},{"baked_lightmaps",QJsonObject{{"stride",stride},{"normal_offset",1}}},
        {family=="spot"?"fit_spot_lights":"fit_point_lights",QJsonObject{{"grid_spacing",spacing},{"max_lights",maxLights},
            {"refinement_steps",refinements},{"style",style},{"max_work",maxWork},{"max_rmse",maxRMSE},{"min_improvement_rmse",minImprovement}}}};
    if(!fixedLights.isEmpty()) result["lights"]=fixedLights;
    return result;
}
QStringList LightFitSettings::arguments() const {
    return {wolf?"-wolf":"-q3","-gamma",decimal(gamma),"-compensate",decimal(compensate),"-extradist",decimal(extraDistance),
        "-lightanglehl","0","-nofastpoint",lightmapsSRGB?"-sRGBlight":"-nosRGBlight",texturesSRGB?"-sRGBtex":"-nosRGBtex",colorsSRGB?"-sRGBcolor":"-nosRGBcolor"};
}
LightFitReview readLightFitReport(const QString& path,const QString& source,const QString& game,const std::atomic_bool* cancelled) {
    checkCancelled(cancelled); QFile file(path);
    if(!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size()>64*1024*1024) fail("Choose a fitting report no larger than 64 MiB.");
    const auto bytes=file.read(64*1024*1024+1);
    if(file.error()!=QFile::NoError || bytes.size()!=file.size() || bytes.size()>64*1024*1024 || bytes.contains('\0')) fail("Cannot read a complete UTF-8 light report.");
    checkCancelled(cancelled);
    rapidjson::Document doc; doc.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(bytes.constData(),size_t(bytes.size()));
    if(doc.HasParseError()) fail("Invalid light fitting JSON."); audit(doc,cancelled);
    if(number(field(doc,"schema_version"),1,1)!=1 || doc.HasMember("point_fit")==doc.HasMember("spot_fit")) fail("Choose a point or spotlight fitting report.");
    const bool spot=doc.HasMember("spot_fit"); const auto& fit=doc[spot?"spot_fit":"point_fit"];
    LightFitReview result; result.path=QFileInfo(path).absoluteFilePath(); result.family=spot?"spot":"point";
    result.game=text(field(doc,"game")); result.status=text(field(fit,"status")); result.sourceHash=text(field(doc,"source_sha256"));
    if(result.sourceHash.size()!=64 || result.sourceHash.toLatin1().toLower()!=result.sourceHash.toLatin1()
        || QByteArray::fromHex(result.sourceHash.toLatin1()).toHex()!=result.sourceHash.toLatin1()) fail("Invalid light report source hash.");
    if(!field(fit,"accepted").IsBool()) fail("Invalid light report qualification.");
    result.accepted=fit["accepted"].GetBool();
    if(!field(doc,"light_inference_performed").IsBool() || !doc["light_inference_performed"].GetBool()
        || text(field(fit,"family"))!=(spot?"nonnegative_native_spot_with_inverse_square_angle_and_extra_distance":"nonnegative_inverse_square_point_with_native_angle_and_extra_distance")
        || text(field(fit,"qualification"))!="conditional_current_assets_and_encoding_not_original_author_metadata") fail("Unsupported light fitting hypothesis.");
    if(text(field(doc,"status"))!=(spot?"spot_fitting_diagnostic":"point_fitting_diagnostic")) fail("Unsupported fitting report status.");
    if(result.accepted && result.status!=(spot?"conditional_spot_proposal":"conditional_point_proposal")) fail("Inconsistent fitting report qualification.");
    const auto& lights=field(fit,"best_trial");
    if(!lights.IsArray() || lights.Size()>16 || (result.accepted && lights.Empty())) fail("Invalid fitted light list.");
    for(const auto& light:lights.GetArray()) {
        const auto& position=field(light,"origin"); const auto& color=field(light,"color");
        if(!position.IsArray() || position.Size()!=3 || !color.IsArray() || color.Size()!=3) fail("Invalid fitted light vector.");
        for(int a=0;a<3;++a) { number(position[a],-1e6,1e6); number(color[a],0,1); }
        number(field(light,"intensity"),0,1e6); integer(field(light,"style"),253);
        if(spot) { object(field(light,"target_link")); number(field(light,"radius"),0,1e6); }
        result.lights.append(object(light));
    }
    for(const char* partition:{"training","withheld"}) {
        const auto& metrics=field(fit,partition);
        if(!metrics.IsObject() || metrics.HasMember("illuminated_baseline")!=metrics.HasMember("illuminated_trial")) fail("Invalid light report score pair.");
        for(const char* stage:{"baseline","trial","illuminated_baseline","illuminated_trial"}) {
            if(std::string_view(stage).starts_with("illuminated") && !metrics.HasMember(stage)) continue;
            const auto& score=field(metrics,stage); const auto count=integer(field(score,"samples"),10000);
            const auto& rmse=field(score,"rmse_bytes");
            if(count) number(rmse,0,255);
            else if(!rmse.IsNull()) fail("An unobserved light score must be unavailable.");
        }
        result.scores[partition]=object(metrics);
    }
    result.settings=object(field(doc,"settings"));
    const auto& dependencies=field(doc,"proposed_entity_indices");
    if(!dependencies.IsArray() || dependencies.Size()>256) fail("Invalid fixed light dependencies.");
    result.fixedLights=int(dependencies.Size());
    if(result.fixedLights && (!doc.HasMember("fixed_proposals") || !doc["fixed_proposals"].IsArray()
                             || doc["fixed_proposals"].Size()!=dependencies.Size())) fail("Regenerate this report to retain its fixed light dependencies.");
    result.reportHash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    result.sourceMatches=hashFile(source,0x7fffffff,cancelled)==result.sourceHash;
    result.gameMatches=game.isEmpty() || game==result.game;
    checkCancelled(cancelled); return result;
}

LightFitReader::~LightFitReader() { if(cancelled_) cancelled_->store(true); }
void LightFitReader::refresh(const QString& report,const QString& source,const QString& game) {
    ++generation_; if(cancelled_) cancelled_->store(true);
    report_=report; source_=source; game_=game; result_={}; error_.clear();
    loading_=!report.isEmpty() && !source.isEmpty(); emit changed();
    if(loading_ && !worker_) start();
}
void LightFitReader::cancel() {
    ++generation_; if(cancelled_) cancelled_->store(true); loading_=false; result_={}; error_="Report reading cancelled."; emit changed();
}
void LightFitReader::start() {
    struct Reply { LightFitReview value; QString error; };
    auto reply=std::make_shared<Reply>(); cancelled_=std::make_shared<std::atomic_bool>(false);
    const auto generation=generation_; const auto flag=cancelled_;
    auto* worker=QThread::create([reply,flag,report=report_,source=source_,game=game_]{
        try { reply->value=readLightFitReport(report,source,game,flag.get()); }
        catch(const std::exception& e) { reply->error=QString::fromUtf8(e.what()); }
    });
    worker_=worker;
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    connect(worker,&QThread::finished,this,[this,reply,generation]{
        worker_=nullptr;
        if(generation!=generation_) { if(loading_) start(); return; }
        result_=reply->value; error_=reply->error; loading_=false; emit changed();
    });
    worker->start();
}
}
