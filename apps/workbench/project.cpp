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
    const auto integer=[&](const char* key,int& value,int low,int high){
        if (!o.contains(key)) return;
        if (!o[key].isDouble() || o[key].toDouble()!=o[key].toInt() || o[key].toInt()<low || o[key].toInt()>high)
            fail(QString("Invalid project number: ")+key);
        value=o[key].toInt();
    };
    integer("workers",p.workers,0,1024); integer("gpu_device",p.gpuDevice,-1,1023);
    integer("minimap_size",p.minimapSize,1,8192); integer("minimap_samples",p.minimapSamples,1,256);
    integer("mesh_patch_steps",p.meshPatchSteps,1,32);
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
    for (auto* value:{&project.source,&project.gameRoot,&project.outputRoot,&project.compiler})
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
    if (!QStringList{"build","bsp","vis","light","minimap","decompile","obj","ase"}.contains(workflow)) errors << "Unknown workflow.";
    if (meshPatchSteps<1 || meshPatchSteps>32) errors << "Mesh curve detail must be between 1 and 32.";
    if (!QStringList{"bsp","rebuild"}.contains(brushOrder)) errors << "Unknown recovery brush order.";
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
        add("DECOMPILE",options,staged,output.filePath("recovered.map"));
    }
    if (workflow=="obj" || workflow=="ase")
        add(workflow.toUpper(),{"-convert","-format",workflow,"-patchsteps",QString::number(p.meshPatchSteps)},staged,
            output.filePath(QFileInfo(p.source).completeBaseName()+"."+workflow));
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
    snapshot.save(QDir(directory).filePath("project.q3mapx.json"));
    return directory;
}
QString displayCommand(const Job& job){
    QStringList values{job.program}; values+=job.arguments;
    for (auto& value:values) { value.replace('"',"\\\""); if (value.contains(' ') || value.contains('\t')) value='"'+value+'"'; }
    return values.join(' ');
}
}
