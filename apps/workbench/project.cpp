// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QCryptographicHash>
#include <stdexcept>

namespace workbench {
static void fail(const QString& message){ throw std::runtime_error(message.toStdString()); }
void saveJson(const QString& path,const QJsonObject& object){
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) fail(file.errorString());
    const auto bytes=QJsonDocument(object).toJson();
    if (file.write(bytes)!=bytes.size() || !file.commit()) fail(file.errorString());
}
QJsonObject Project::toJson() const {
    return {{"schema_version",1},{"name",name},{"source",source},{"game_root",gameRoot},{"output_root",outputRoot},
        {"compiler",compiler},{"game",game},{"mod",mod},{"quality",quality},{"backend",backend},{"map_format",mapFormat},
        {"workers",workers},{"gpu_device",gpuDevice},{"minimap_size",minimapSize},{"minimap_samples",minimapSamples},
        {"reproducible_vis",reproducibleVis},
        {"mesh_patch_steps",meshPatchSteps},{"brush_order",brushOrder},
        {"detail_policy",detailPolicy},{"group_policy",groupPolicy},
        {"detail_work_limit",detailWorkLimit},{"group_work_limit",groupWorkLimit},
        {"patch_recovery",patchRecovery},{"patch_colors",patchColors},
        {"patch_color_subdivisions",patchColorSubdivisions},{"patch_fit_work_limit",patchFitWorkLimit},
        {"light_fit",lightFit.toJson()},{"apply_light_report",applyLightReport},
        {"light_report",lightReport},{"light_report_sha256",lightReportHash},
        {"bsp_options",QJsonArray::fromStringList(bspOptions)},{"vis_options",QJsonArray::fromStringList(visOptions)},
        {"light_options",QJsonArray::fromStringList(lightOptions)}};
}
Project Project::fromJson(const QJsonObject& o){
    if (o.value("schema_version").toInt()!=1) fail("Unsupported project schema; expected version 1");
    Project p;
    if (o.contains("reproducible_vis") && !o["reproducible_vis"].isBool()) fail("Invalid project field: reproducible_vis");
    p.reproducibleVis=o.value("reproducible_vis").toBool(false); // Older projects retain their original behavior.
    const auto string=[&](const char* key,QString& value){
        if (o.contains(key)) { if (!o[key].isString()) fail(QString("Invalid project field: ")+key); value=o[key].toString(); }
    };
    string("name",p.name); string("source",p.source); string("game_root",p.gameRoot); string("output_root",p.outputRoot);
    string("compiler",p.compiler); string("game",p.game); string("mod",p.mod); string("quality",p.quality);
    string("backend",p.backend); string("map_format",p.mapFormat);
    string("brush_order",p.brushOrder);
    string("detail_policy",p.detailPolicy); string("group_policy",p.groupPolicy);
    string("patch_recovery",p.patchRecovery); string("patch_colors",p.patchColors);
    string("light_report",p.lightReport); string("light_report_sha256",p.lightReportHash);
    if(o.contains("light_fit")) {
        if(!o["light_fit"].isObject()) fail("Invalid light fitting settings");
        p.lightFit=LightFitSettings::fromJson(o["light_fit"].toObject());
    }
    if(o.contains("apply_light_report")) {
        if(!o["apply_light_report"].isBool()) fail("Invalid light report selection");
        p.applyLightReport=o["apply_light_report"].toBool();
    }
    const auto integer=[&](const char* key,int& value,int low,int high){
        if (!o.contains(key)) return;
        if (!o[key].isDouble() || o[key].toDouble()!=o[key].toInt() || o[key].toInt()<low || o[key].toInt()>high)
            fail(QString("Invalid project number: ")+key);
        value=o[key].toInt();
    };
    integer("workers",p.workers,0,1024); integer("gpu_device",p.gpuDevice,-1,1023);
    integer("minimap_size",p.minimapSize,1,8192); integer("minimap_samples",p.minimapSamples,1,256);
    integer("mesh_patch_steps",p.meshPatchSteps,1,32);
    integer("detail_work_limit",p.detailWorkLimit,1,100'000'000);
    integer("group_work_limit",p.groupWorkLimit,1,100'000'000);
    integer("patch_color_subdivisions",p.patchColorSubdivisions,1,32);
    integer("patch_fit_work_limit",p.patchFitWorkLimit,1,1'000'000'000);
    const auto list=[&](const char* key,QStringList& values){
        if (!o.contains(key)) return;
        if (!o[key].isArray()) fail(QString("Invalid option list: ")+key);
        for (const auto& value:o[key].toArray()) {
            if (!value.isString() || value.toString().contains(QChar('\0'))) fail("Invalid argument in project");
            values.append(value.toString());
        }
    };
    list("bsp_options",p.bspOptions); list("vis_options",p.visOptions); list("light_options",p.lightOptions);
    if (!QStringList{"draft","balanced","production"}.contains(p.quality)) fail("Unknown quality preset");
    if (!QStringList{"auto","cpu","gpu","reference"}.contains(p.backend)) fail("Unknown compute backend");
    if (!QStringList{"map","map_bp","map_220"}.contains(p.mapFormat)) fail("Unknown map format");
    if (!QStringList{"bsp","rebuild"}.contains(p.brushOrder)) fail("Unknown recovery brush order");
    if (!QStringList{"legacy","cells"}.contains(p.detailPolicy)) fail("Unknown recovery detail policy");
    if (!QStringList{"none","surfaces"}.contains(p.groupPolicy)) fail("Unknown recovery group policy");
    if (p.groupPolicy=="surfaces" && p.brushOrder!="rebuild") fail("Surface grouping requires rebuild brush order");
    if (!QStringList{"none","source","fit","auto"}.contains(p.patchRecovery)) fail("Unknown patch recovery policy");
    if (!QStringList{"none","alpha","rgba"}.contains(p.patchColors)) fail("Unknown patch color policy");
    if (p.patchColorSubdivisions & (p.patchColorSubdivisions-1)) fail("Patch color subdivisions must be a power of two");
    return p;
}
Project Project::load(const QString& path){
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    if (file.size()>1024*1024) fail("Project file exceeds 1 MiB");
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) fail("Invalid project JSON: "+error.errorString());
    auto project=fromJson(document.object());
    const QDir directory(QFileInfo(path).absolutePath());
    for (auto* value:{&project.source,&project.gameRoot,&project.outputRoot,&project.compiler,&project.lightReport})
        if (!value->isEmpty() && QDir::isRelativePath(*value)) *value=directory.absoluteFilePath(*value);
    return project;
}
void Project::save(const QString& path) const { saveJson(path,toJson()); }
QStringList Project::validate(const QString& workflow) const {
    QStringList errors;
    if (!QFileInfo(source).isFile()) errors << "Choose an existing source MAP or BSP file.";
    if (!QFileInfo(compiler).isExecutable() || !QFileInfo(compiler).isFile()) errors << "Choose a runnable q3mapx compiler.";
    if (!QFileInfo(gameRoot).isDir()) errors << "Choose the game root containing baseq3 or the game's asset directory.";
    if (outputRoot.trimmed().isEmpty()) errors << "Choose an output folder.";
    if (game.trimmed().isEmpty()) errors << "Select a game profile.";
    const auto extension=QFileInfo(source).suffix().toLower();
    if ((workflow=="build" || workflow=="bsp") && extension!="map") errors << "BSP construction needs a .map source.";
    if (workflow!="build" && workflow!="bsp" && extension!="bsp") errors << "This workflow needs a .bsp source.";
    if (!QStringList{"build","bsp","vis","light","light-fit","minimap","decompile","obj","ase","geometry-analyze","geometry-optimize"}.contains(workflow)) errors << "Unknown workflow.";
    if(workflow=="light-fit") errors+=lightFit.validate();
    if(workflow=="decompile" && applyLightReport) {
        if(!QFileInfo(lightReport).isFile()) errors << "Choose an existing light fitting report.";
        if(lightReportHash.size()!=64 || QByteArray::fromHex(lightReportHash.toLatin1()).toHex()!=lightReportHash.toLatin1())
            errors << "Review the light fitting report and select Use for MAP recovery.";
    }
    if (meshPatchSteps<1 || meshPatchSteps>32) errors << "Mesh curve detail must be between 1 and 32.";
    if (!QStringList{"bsp","rebuild"}.contains(brushOrder)) errors << "Unknown recovery brush order.";
    if (!QStringList{"legacy","cells"}.contains(detailPolicy)) errors << "Unknown recovery detail policy.";
    if (!QStringList{"none","surfaces"}.contains(groupPolicy)) errors << "Unknown recovery group policy.";
    if (groupPolicy=="surfaces" && brushOrder!="rebuild") errors << "Surface grouping requires rebuild brush order.";
    if (!QStringList{"none","source","fit","auto"}.contains(patchRecovery)) errors << "Unknown patch recovery policy.";
    if (!QStringList{"none","alpha","rgba"}.contains(patchColors)) errors << "Unknown patch color policy.";
    if (patchColorSubdivisions<1 || patchColorSubdivisions>32 || (patchColorSubdivisions & (patchColorSubdivisions-1)))
        errors << "Patch color subdivisions must be 1, 2, 4, 8, 16 or 32.";
    if (patchFitWorkLimit<1 || patchFitWorkLimit>1'000'000'000) errors << "Patch fitting work limit must be between 1 and 1000000000.";
    if (detailWorkLimit<1 || detailWorkLimit>100'000'000 || groupWorkLimit<1 || groupWorkLimit>100'000'000)
        errors << "Recovery analysis work limits must be between 1 and 100000000.";
    if (workers<0 || workers>1024 || minimapSize<1 || minimapSize>8192 || minimapSamples<1 || minimapSamples>256)
        errors << "Invalid worker count or minimap dimensions.";
    return errors;
}
QJsonObject Job::toJson() const {
    return {{"group",group},{"label",label},{"program",program},{"arguments",QJsonArray::fromStringList(arguments)},
        {"directory",directory},{"log_path",logPath},{"output_path",outputPath},{"state",state},{"error",error},
        {"elapsed_ms",double(elapsedMs)},{"exit_code",exitCode}};
}
QVector<Job> buildPlan(const Project& p,const QString& workflow,const QString& directory){
    const QDir output(directory);
    const QString staged=output.filePath(QFileInfo(p.source).fileName());
    const QString bsp=output.filePath(QFileInfo(p.source).completeBaseName()+".bsp");
    QStringList base{"-game",p.game,"-fs_basepath",p.gameRoot,"-threads",p.workers ? QString::number(p.workers) : "auto"};
    if (!p.mod.isEmpty()) base << "-fs_game" << p.mod;
    QVector<Job> jobs;
    const auto add=[&](const QString& label,QStringList options,const QString& input,const QString& result){
        Job job; job.group=directory; job.label=label; job.program=p.compiler; job.directory=directory;
        job.logPath=output.filePath(label.toLower()+".log"); job.outputPath=result;
        job.arguments=base; job.arguments << "-profile" << output.filePath(label.toLower()+".cpu.json");
        job.arguments << options << input; jobs.append(job);
    };
    if (workflow=="build" || workflow=="bsp") add("BSP",QStringList{"-meta","-leaktest"}+p.bspOptions,staged,bsp);
    if (workflow=="build" || workflow=="vis") {
        QStringList options{"-vis","-saveprt"}; if (p.quality=="draft") options << "-fast";
        if (p.reproducibleVis) options << "-reproducible";
        add("VIS",options+p.visOptions,bsp,bsp);
    }
    if (workflow=="build" || workflow=="light") {
        QStringList options{"-light","-fast","-samples",p.quality=="draft" ? "1" : p.quality=="production" ? "4" : "2"};
        if (p.quality=="production") options << "-bounce" << "2";
        add("LIGHT",options+p.lightOptions,bsp,bsp);
    }
    if (workflow=="decompile") {
        QStringList options{"-decompile","-format",p.mapFormat,"-o",output.filePath("recovered.map")};
        // Preserve the previous command for default and older saved projects.
        if (p.brushOrder=="rebuild") options << "-brush-order" << "rebuild";
        if (p.detailPolicy=="cells") options << "-detail-policy" << "cells" << "-detail-max-work" << QString::number(p.detailWorkLimit);
        if (p.groupPolicy=="surfaces") options << "-group-policy" << "surfaces" << "-group-max-work" << QString::number(p.groupWorkLimit);
        if (p.applyLightReport) options << "-light-proposals" << output.filePath("selected-light-report.json");
        if (p.patchRecovery!="none") options << "-patch-recovery" << p.patchRecovery;
        if (p.patchColors!="none") options << "-patch-colors" << p.patchColors << "-patch-color-subdivisions" << QString::number(p.patchColorSubdivisions);
        if (p.patchRecovery=="fit" || p.patchRecovery=="auto") options << "-patch-fit-work" << QString::number(p.patchFitWorkLimit);
        add("DECOMPILE",options,staged,output.filePath("recovered.map"));
    }
    if(workflow=="light-fit") {
        add("LIGHT-FIT",QStringList{"-light","-probes",output.filePath("light-request.json"),"-probe-report",output.filePath("light-fit.json")}
            +p.lightFit.arguments(),staged,output.filePath("light-fit.json"));
    }
    if (workflow=="obj" || workflow=="ase")
        add(workflow.toUpper(),{"-convert","-format",workflow,"-patchsteps",QString::number(p.meshPatchSteps)},staged,
            output.filePath(QFileInfo(p.source).completeBaseName()+"."+workflow));
    if (workflow=="geometry-analyze" || workflow=="geometry-optimize") {
        const auto report=output.filePath("geometry.json");
        const auto optimized=output.filePath(QFileInfo(p.source).completeBaseName()+".optimized.bsp");
        QStringList options{"-optimize-geometry","-renderer","quake3e-gl","-report",report};
        if (workflow=="geometry-optimize") options << "-o" << optimized;
        add("GEOMETRY",options,staged,workflow=="geometry-optimize"?optimized:report);
    }
    if (workflow=="minimap") {
        QStringList options{"-minimap","-backend",p.backend,"-size",QString::number(p.minimapSize),"-samples",QString::number(p.minimapSamples),
                            "-compute-report",output.filePath("compute.json"),"-o",output.filePath("minimap.tga")};
        if (p.gpuDevice>=0) options << "-gpu-device" << QString::number(p.gpuDevice);
        add("MINIMAP",options,staged,output.filePath("minimap.tga"));
    }
    return jobs;
}
QString prepareRun(const Project& project,const QString& workflow){
    const auto errors=project.validate(workflow);
    if (!errors.isEmpty()) fail(errors.join('\n'));
    QByteArray lightReport;
    if(workflow=="decompile" && project.applyLightReport) {
        QFile report(project.lightReport);
        if(!report.open(QIODevice::ReadOnly) || report.size()>64*1024*1024) fail("Light report is unavailable or exceeds 64 MiB");
        lightReport=report.read(64*1024*1024+1);
        if(report.error()!=QFile::NoError || lightReport.size()!=report.size() || lightReport.size()>64*1024*1024
            || QCryptographicHash::hash(lightReport,QCryptographicHash::Sha256).toHex()!=project.lightReportHash.toLatin1())
            fail("The selected light report changed. Review it again before recovery.");
    }
    const QString name=QFileInfo(project.source).completeBaseName()+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    const QString directory=QDir(project.outputRoot).absoluteFilePath(name);
    if (!QDir().mkpath(directory)) fail("Cannot create output folder: "+directory);
    if (!QFile::copy(project.source,QDir(directory).filePath(QFileInfo(project.source).fileName()))) fail("Cannot stage source into output folder");
    // VIS requires the matching portal file; source input is never modified.
    if (workflow=="vis") {
        const QString portal=QFileInfo(project.source).absolutePath()+"/"+QFileInfo(project.source).completeBaseName()+".prt";
        if (!QFile::copy(portal,QDir(directory).filePath(QFileInfo(portal).fileName()))) fail("Visibility needs a matching .prt file beside the source BSP");
    }
    Project snapshot=project;
    snapshot.source=QDir(directory).filePath(QFileInfo(project.source).fileName());
    if(workflow=="light-fit") saveJson(QDir(directory).filePath("light-request.json"),project.lightFit.request());
    if(!lightReport.isEmpty()) {
        snapshot.lightReport=QDir(directory).filePath("selected-light-report.json");
        QSaveFile report(snapshot.lightReport);
        if(!report.open(QIODevice::WriteOnly) || report.write(lightReport)!=lightReport.size() || !report.commit()) fail("Cannot stage selected light report");
    }
    snapshot.save(QDir(directory).filePath("project.q3mapx.json"));
    return directory;
}
QString displayCommand(const Job& job){
    QStringList values{job.program}; values+=job.arguments;
    for (auto& value:values) { value.replace('"',"\\\""); if (value.contains(' ') || value.contains('\t')) value='"'+value+'"'; }
    return values.join(' ');
}
}
