// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"
#include "job_queue.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace workbench;
static void require(bool condition,const char* message){ if(!condition) throw std::runtime_error(message); }
static void finishQueue(JobQueue& queue){
    QEventLoop loop; bool timeout=false;
    QObject::connect(&queue,&JobQueue::idle,&loop,&QEventLoop::quit);
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timeout=true; queue.cancel(); loop.quit();});
    timer.start(60000); queue.start(); if(queue.running()) loop.exec();
    require(!timeout,"Queue timed out");
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--slow-child")) { QTimer::singleShot(30000,&app,&QCoreApplication::quit); return app.exec(); }
    try {
        require(argc==4,"Expected compiler, fixture MAP and work directory");
        const QString root=QDir(argv[3]).absolutePath(); QDir().mkpath(root);
        Project p; p.name="A project with spaces"; p.compiler=QFileInfo(argv[1]).absoluteFilePath(); p.source=QFileInfo(argv[2]).absoluteFilePath();
        QDir assets=QFileInfo(p.source).absoluteDir(); assets.cdUp(); assets.cdUp(); p.gameRoot=assets.absolutePath();
        p.outputRoot=root+"/outputs with spaces"; p.workers=4;
        p.bspOptions={"-keeplights"};
        require(p.brushOrder=="bsp","New projects changed the default recovery order");
        require(p.detailPolicy=="legacy" && p.groupPolicy=="none","New projects enabled inference");
        p.brushOrder="rebuild";
        p.detailPolicy="cells"; p.groupPolicy="surfaces"; p.detailWorkLimit=1'234'567; p.groupWorkLimit=2'345'678;
        const auto path=root+"/project.q3mapx.json"; p.save(path); auto loaded=Project::load(path);
        require(loaded.toJson()==p.toJson(),"Project did not round-trip");
        auto invalid=p.toJson(); invalid["workers"]=-1; bool rejected=false;
        try { Project::fromJson(invalid); } catch(...) { rejected=true; }
        require(rejected,"Invalid project worker count accepted");
        require(p.validate("build").isEmpty(),"Valid project rejected");
        const auto directory=prepareRun(p,"build");
        auto plan=buildPlan(p,"build",directory);
        require(plan.size()==3 && plan[0].arguments.contains(p.gameRoot),"Command construction lost argument boundaries");
        require(plan[1].arguments.contains("-reproducible"),"Reproducible visibility option missing");
        auto older=p.toJson(); older.remove("reproducible_vis");
        require(!Project::fromJson(older).reproducibleVis,"Older project behavior changed");
        older.remove("mesh_patch_steps");
        require(Project::fromJson(older).meshPatchSteps==8,"Older project mesh detail default changed");
        for(const auto* key:{"brush_order","detail_policy","group_policy","detail_work_limit","group_work_limit"}) older.remove(key);
        const auto migrated=Project::fromJson(older);
        require(migrated.brushOrder=="bsp" && migrated.detailPolicy=="legacy" && migrated.groupPolicy=="none"
            && migrated.detailWorkLimit==50'000'000 && migrated.groupWorkLimit==50'000'000,"Older project recovery defaults changed");
        for(const auto* key:{"brush_order","detail_policy","group_policy"}) {
            for(const auto& value:QJsonArray{"unknown",17,false,QJsonValue(QJsonValue::Null),""}) {
                invalid=p.toJson(); invalid[key]=value; rejected=false;
                try { Project::fromJson(invalid); } catch(...) { rejected=true; }
                require(rejected,"Malformed recovery policy accepted");
            }
        }
        for(const auto* key:{"detail_work_limit","group_work_limit"}) {
            for(const auto& value:QJsonArray{0,-1,100'000'001,1.5,false,QJsonValue(QJsonValue::Null),"50000000"}) {
                invalid=p.toJson(); invalid[key]=value; rejected=false;
                try { Project::fromJson(invalid); } catch(...) { rejected=true; }
                require(rejected,"Malformed recovery work limit accepted");
            }
            for(int value:{1,100'000'000}) { invalid=p.toJson(); invalid[key]=value; (void)Project::fromJson(invalid); }
        }
        invalid=p.toJson(); invalid["brush_order"]="bsp"; rejected=false;
        try { Project::fromJson(invalid); } catch(...) { rejected=true; }
        require(rejected,"Conflicting group/order project accepted");
        invalid=p.toJson(); invalid["mesh_patch_steps"]=33; rejected=false;
        try { Project::fromJson(invalid); } catch(...) { rejected=true; }
        require(rejected,"Invalid mesh curve detail accepted");
        const auto original=QFileInfo(p.source).lastModified();
        JobQueue queue; queue.enqueue(plan); finishQueue(queue);
        for(const auto& job:queue.jobs()) { require(job.state=="Succeeded",qPrintable(job.error)); require(QFileInfo(job.logPath).size()>0,"Missing persistent log"); }
        require(QFileInfo(plan.back().outputPath).size()>100,"Real compile did not produce BSP");
        require(QFileInfo(p.source).lastModified()==original,"Source was modified");
        const auto snapshot=Project::load(directory+"/project.q3mapx.json"); require(snapshot.source.startsWith(directory),"Run snapshot did not retain staged source");
        p.source=plan.back().outputPath;
        p.meshPatchSteps=4;
        auto legacy=p; legacy.brushOrder="bsp"; legacy.detailPolicy="legacy"; legacy.groupPolicy="none";
        const auto oldArgs=buildPlan(legacy,"decompile",root).front().arguments;
        for(const auto* flag:{"-brush-order","-detail-policy","-detail-max-work","-group-policy","-group-max-work"})
            require(!oldArgs.contains(flag),"Default recovery command changed");
        for(const auto& workflow:QStringList{"decompile","minimap","obj","ase"}) {
            const auto output=prepareRun(p,workflow); auto jobs=buildPlan(p,workflow,output);
            require(jobs.front().arguments.contains("-brush-order")== (workflow=="decompile"),"Recovery setting escaped its workflow");
            for(const auto* flag:{"-detail-policy","-detail-max-work","-group-policy","-group-max-work"})
                require(jobs.front().arguments.contains(flag)==(workflow=="decompile"),"Inference setting escaped its workflow");
            queue.enqueue(jobs); finishQueue(queue); require(queue.jobs().back().state=="Succeeded","Recovery/minimap/mesh export failed");
            require(QFileInfo(jobs.back().outputPath).size()>0,"Missing workflow output");
            if(workflow=="decompile") {
                QFile report(jobs.back().outputPath+".recovery.json"); require(report.open(QIODevice::ReadOnly),"Missing recovery report");
                const auto result=QJsonDocument::fromJson(report.readAll()).object();
                require(result["brush_order"].toObject()["policy"]=="rebuild","Real compiler ignored the saved recovery order");
                require(result["detail_inference"].toObject()["policy"]=="cells"
                    && result["detail_inference"].toObject()["work_limit"].toInt()==p.detailWorkLimit,"Real compiler ignored detail policy/budget");
                require(result["group_inference"].toObject()["policy"]=="surfaces"
                    && result["group_inference"].toObject()["work_limit"].toInt()==p.groupWorkLimit
                    && result["group_inference"].toObject()["exported_groups"].toInt()==2,"Real compiler did not recover two expected assemblies");
                const auto saved=Project::load(output+"/project.q3mapx.json");
                require(saved.brushOrder=="rebuild" && saved.detailPolicy==p.detailPolicy && saved.groupPolicy==p.groupPolicy
                    && saved.detailWorkLimit==p.detailWorkLimit && saved.groupWorkLimit==p.groupWorkLimit,"Run snapshot lost recovery policies/budgets");
                saveJson(root+"/inference-run.json",{{"recovery_report",result},{"snapshot",saved.toJson()}});
            }
        }
        Job bad; bad.group="bad"; bad.label="Broken executable"; bad.program=root+"/missing-compiler"; bad.directory=root; bad.logPath=root+"/bad.log";
        Job skipped=bad; skipped.label="Dependent"; skipped.logPath=root+"/skipped.log";
        queue.enqueue({bad,skipped}); finishQueue(queue);
        require(queue.jobs()[queue.jobs().size()-2].state=="Failed" && queue.jobs().back().state=="Skipped","Failed start did not skip dependency");
        Job slow; slow.group="cancel"; slow.label="Slow child"; slow.program=QCoreApplication::applicationFilePath(); slow.arguments={"--slow-child"}; slow.directory=root; slow.logPath=root+"/cancel.log";
        queue.enqueue({slow}); QTimer::singleShot(100,&queue,&JobQueue::cancel); finishQueue(queue);
        require(queue.jobs().back().state=="Cancelled","Cancellation was not reported");
        saveJson(root+"/queue-report.json",queue.report());
        std::cout<<"Project JSON, real compile/recovery/minimap, argument paths, failed start, dependent skip and cancellation passed\n";
    } catch(const std::exception& error){ std::cerr<<error.what()<<'\n'; return 1; }
}
