// SPDX-License-Identifier: GPL-3.0-or-later
#include "bsp_inspector.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace workbench {
namespace {
constexpr qint64 maxReply=1024*1024;
[[noreturn]] void invalid(){ throw std::runtime_error("Compiler returned an invalid BSP inspection report"); }
qint64 integer(const QJsonValue& value,qint64 minimum,qint64 maximum){
    const auto number=value.toInteger(std::numeric_limits<qint64>::min());
    if(!value.isDouble() || number<minimum || number>maximum || double(number)!=value.toDouble()) invalid();
    return number;
}
QString string(const QJsonValue& value,int maximum=4096){
    if(!value.isString() || value.toString().size()>maximum || value.toString().contains(QChar('\0'))) invalid();
    return value.toString();
}
bool boolean(const QJsonValue& value){ if(!value.isBool()) invalid(); return value.toBool(); }
QJsonArray array(const QJsonValue& value,int maximum){
    if(!value.isArray() || value.toArray().size()>maximum) invalid();
    return value.toArray();
}
QJsonArray messages(const QJsonValue& value){
    const auto result=array(value,512);
    for(const auto& entry:result) string(entry);
    return result;
}
}

QJsonObject parseBspInspection(const QByteArray& bytes){
    if(bytes.size()>maxReply) invalid();
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject()) invalid();
    const auto report=document.object();
    integer(report["schema_version"],1,1);
    if(string(report["inspection_scope"])!="signature_and_lump_directory" || boolean(report["geometry_validated"])) invalid();
    const bool valid=boolean(report["valid"]);
    string(report["file"],32768);
    integer(report["file_bytes"],0,std::numeric_limits<qint64>::max());
    const auto ident=string(report["ident"],4), hex=string(report["ident_hex"],8);
    if((ident.size()!=4 && !ident.isEmpty()) || hex.size()!=ident.size()*2
       || (!hex.isEmpty() && !QRegularExpression("^[0-9a-f]{8}$").match(hex).hasMatch())) invalid();
    integer(report["version"],INT32_MIN,INT32_MAX);
    const QRegularExpression identifier("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$");
    if(!report["selected_profile"].isNull() && !identifier.match(string(report["selected_profile"],64)).hasMatch()) invalid();
    QSet<QString> candidates;
    for(const auto& value:array(report["profile_candidates"],256)) {
        const auto profile=string(value,64);
        if(!identifier.match(profile).hasMatch() || candidates.contains(profile)) invalid();
        candidates.insert(profile);
    }
    if(boolean(report["ambiguous_game"])!=(candidates.size()>1)) invalid();
    const auto errors=messages(report["errors"]); messages(report["notes"]);
    if(valid!=errors.isEmpty()) invalid();
    bool validLayout=false;
    QSet<QString> layoutNames;
    for(const auto& value:array(report["layouts"],16)) {
        if(!value.isObject()) invalid();
        const auto layout=value.toObject();
        const auto id=string(layout["id"],64);
        if(!identifier.match(id).hasMatch() || layoutNames.contains(id)) invalid();
        layoutNames.insert(id); string(layout["title"],512);
        const bool layoutValid=boolean(layout["valid"]);
        if(layoutValid!=messages(layout["errors"]).isEmpty()) invalid();
        validLayout|=layoutValid;
        integer(layout["header_bytes"],8,65536);
        integer(layout["payload_bytes"],0,std::numeric_limits<qint64>::max());
        if(layout.contains("stored_checksum")) integer(layout["stored_checksum"],0,UINT32_MAX);
        int index=0;
        for(const auto& entry:array(layout["lumps"],64)) {
            if(!entry.isObject()) invalid();
            const auto lump=entry.toObject();
            integer(lump["index"],index,index); ++index;
            string(lump["name"],512); integer(lump["offset"],INT32_MIN,INT32_MAX);
            const auto length=integer(lump["bytes"],INT32_MIN,INT32_MAX);
            const auto stride=integer(lump["record_bytes"],0,UINT32_MAX);
            if(stride && length>=0 && length%stride==0) integer(lump["records"],length/stride,length/stride);
            else if(!lump["records"].isNull()) invalid();
        }
    }
    if(valid && !validLayout) invalid();
    return report;
}

void BspInspector::reset(const QString& message){
    ++generation_;
    if(pending_) pending_->kill();
    pending_=nullptr; loading_=false; error_=message; report_={}; emit changed();
}

void BspInspector::inspect(const QString& compiler,const QString& file,const QString& profile,int timeoutMs){
    reset();
    if(compiler.trimmed().isEmpty() || file.trimmed().isEmpty()) {
        error_="Choose a compiler and a BSP file to inspect"; emit changed(); return;
    }
    const auto generation=++generation_;
    const QString source=QFileInfo(file).absoluteFilePath();
    loading_=true; emit changed();
    struct Reply { QByteArray bytes, diagnostics; qint64 total=0; QString failure; };
    auto reply=std::make_shared<Reply>();
    auto* process=new QProcess(this); pending_=process;
    const auto collect=[process,reply]{
        if(!reply->failure.isEmpty()) return;
        for(const auto channel:{QProcess::StandardOutput,QProcess::StandardError}) {
            process->setReadChannel(channel);
            const auto part=process->read(maxReply-reply->total+1);
            reply->total+=part.size();
            if(channel==QProcess::StandardOutput) reply->bytes+=part;
            else reply->diagnostics+=part.left(qMax(qint64(0),8192-qint64(reply->diagnostics.size())));
            if(reply->total>maxReply) {
                reply->failure="Compiler inspection output exceeds 1 MiB";
                process->kill(); return;
            }
        }
    };
    connect(process,&QProcess::readyReadStandardOutput,this,collect);
    connect(process,&QProcess::readyReadStandardError,this,collect);
    connect(process,&QProcess::finished,this,[this,process,generation,source,reply,collect](int code,QProcess::ExitStatus status){
        collect();
        if(generation==generation_) {
            loading_=false; pending_=nullptr;
            if(!reply->failure.isEmpty()) error_=reply->failure;
            else if(status!=QProcess::NormalExit || (code!=0 && code!=1))
                error_=QString("Compiler inspection failed (exit %1)\n%2").arg(code).arg(QString::fromUtf8(reply->diagnostics).trimmed());
            else {
                try {
                    const auto result=parseBspInspection(reply->bytes);
                    if(result["file"].toString()!=source || result["valid"].toBool()!=(code==0)) invalid();
                    report_=result;
                } catch(const std::exception& error) { error_=QString::fromUtf8(error.what()); }
            }
            emit changed();
        }
        process->deleteLater();
    });
    connect(process,&QProcess::errorOccurred,this,[this,process,generation](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart) return;
        if(generation==generation_) {
            loading_=false; error_=process->errorString(); pending_=nullptr; emit changed();
        }
        process->deleteLater();
    });
    QTimer::singleShot(qBound(1,timeoutMs,60000),process,[process,reply]{
        if(process->state()!=QProcess::NotRunning) { reply->failure="Compiler inspection timed out"; process->kill(); }
    });
    QStringList arguments{"-inspect","-json"};
    if(!profile.isEmpty()) arguments << "-game" << profile;
    arguments << source;
    process->start(compiler,arguments);
}
}
