// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_inventory.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace workbench {
namespace {
constexpr qint64 maxReply=1024*1024;
constexpr qint64 maxDiagnostics=8192;
[[noreturn]] void invalid(){ throw std::runtime_error("Compiler returned an invalid device inventory"); }
qint64 integer(const QJsonValue& value,qint64 maximum){
    const auto number=value.toInteger(-1);
    if(!value.isDouble() || number<0 || number>maximum || double(number)!=value.toDouble()) invalid();
    return number;
}
QString string(const QJsonValue& value,int maximum=4096){
    if(!value.isString() || value.toString().size()>maximum || value.toString().contains(QChar('\0'))) invalid();
    return value.toString();
}
void stop(QProcess* process){
    // Stop retaining output even if the operating system has not reaped the child yet.
    process->closeReadChannel(QProcess::StandardOutput);
    process->closeReadChannel(QProcess::StandardError);
    process->kill();
}
}

QJsonObject parseDeviceInventory(const QByteArray& bytes){
    if(bytes.size()>maxReply) invalid();
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject()) invalid();
    const auto report=document.object();
    if(integer(report["schema_version"],1)!=1) invalid();
    string(report["reason"],16384);
    if(!report["devices"].isArray() || report["devices"].toArray().size()>1024) invalid();
    QSet<qint64> indices;
    for(const auto& entry:report["devices"].toArray()) {
        if(!entry.isObject()) invalid();
        const auto device=entry.toObject();
        const auto index=integer(device["index"],INT32_MAX);
        if(indices.contains(index)) invalid();
        indices.insert(index);
        string(device["name"]); string(device["vendor"]); string(device["version"]);
        integer(device["memory_bytes"],std::numeric_limits<qint64>::max());
        integer(device["compute_units"],UINT32_MAX);
        if(!device["unified_memory"].isBool()) invalid();
    }
    return report;
}

void DeviceInventory::reset(const QString& message){
    ++generation_;
    if(pending_) stop(pending_);
    pending_=nullptr; loading_=false; report_={}; error_=message; diagnostics_.clear();
    emit changed();
}

void DeviceInventory::refresh(const QString& compiler,int timeoutMs){
    reset();
    if(compiler.trimmed().isEmpty()) {
        error_="Choose a compiler in Project settings before querying devices"; emit changed(); return;
    }
    const auto generation=++generation_;
    loading_=true;
    struct Reply { QByteArray bytes, diagnostics; qint64 total=0; QString failure; bool diagnosticsTruncated=false; };
    auto reply=std::make_shared<Reply>();
    auto* process=new QProcess(this); pending_=process;
    const auto collect=[process,reply]{
        if(!reply->failure.isEmpty()) return;
        for(const auto channel:{QProcess::StandardOutput,QProcess::StandardError}) {
            process->setReadChannel(channel);
            const auto part=process->read(maxReply-reply->total+1);
            reply->total+=part.size();
            if(channel==QProcess::StandardOutput) reply->bytes+=part;
            else {
                const auto space=qMax(qint64(0),maxDiagnostics-qint64(reply->diagnostics.size()));
                reply->diagnostics+=part.left(space); reply->diagnosticsTruncated|=part.size()>space;
            }
            if(reply->total>maxReply) {
                reply->failure="Compiler device output exceeds 1 MiB (stdout and stderr combined)";
                stop(process); return;
            }
        }
    };
    connect(process,&QProcess::readyReadStandardOutput,this,collect);
    connect(process,&QProcess::readyReadStandardError,this,collect);
    connect(process,&QProcess::started,this,[this,process,generation]{
        if(generation!=generation_) stop(process);
    });
    connect(process,&QProcess::finished,this,[this,process,generation,reply,collect](int code,QProcess::ExitStatus status){
        collect();
        if(generation==generation_) {
            loading_=false; pending_=nullptr;
            diagnostics_=QString::fromUtf8(reply->diagnostics).trimmed();
            if(reply->diagnosticsTruncated)
                diagnostics_+="\n[Compiler messages limited to the first 8 KiB]";
            if(!reply->failure.isEmpty()) error_=reply->failure;
            else if(status!=QProcess::NormalExit || code!=0)
                error_=QString("Device query failed (%1)").arg(status==QProcess::NormalExit?QString("exit %1").arg(code):"compiler crashed");
            else {
                try { report_=parseDeviceInventory(reply->bytes); }
                catch(const std::exception& error) { error_=QString::fromUtf8(error.what()); }
            }
            emit changed();
        }
        process->deleteLater();
    });
    connect(process,&QProcess::errorOccurred,this,[this,process,generation,reply](QProcess::ProcessError error){
        if(error==QProcess::ReadError) { reply->failure="Cannot read compiler device output"; stop(process); }
        if(error!=QProcess::FailedToStart) return;
        if(generation==generation_) {
            loading_=false; pending_=nullptr; error_="Cannot start device query: "+process->errorString(); emit changed();
        }
        process->deleteLater();
    });
    QTimer::singleShot(qBound(1,timeoutMs,60000),process,[process,reply]{
        if(process->state()!=QProcess::NotRunning) {
            if(reply->failure.isEmpty()) reply->failure="Compiler device query timed out";
            stop(process);
        }
    });
    process->start(compiler,{"-devices"});
    emit changed();
}
}
