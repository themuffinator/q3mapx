// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"
#include "job_queue.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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
        const auto path=root+"/project.q3mapx.json"; p.save(path); auto loaded=Project::load(path);
        require(loaded.toJson()==p.toJson(),"Project did not round-trip");
        auto invalid=p.toJson(); invalid["workers"]=-1; bool rejected=false;
        try { Project::fromJson(invalid); } catch(...) { rejected=true; }
        require(rejected,"Invalid project worker count accepted");
        require(p.validate("build").isEmpty(),"Valid project rejected");
        const auto directory=prepareRun(p,"build");
        auto plan=buildPlan(p,"build",directory);
        require(plan.size()==3 && plan[0].arguments.contains(p.gameRoot),"Command construction lost argument boundaries");
        const auto original=QFileInfo(p.source).lastModified();
        JobQueue queue; queue.enqueue(plan); finishQueue(queue);
        for(const auto& job:queue.jobs()) { require(job.state=="Succeeded",qPrintable(job.error)); require(QFileInfo(job.logPath).size()>0,"Missing persistent log"); }
        require(QFileInfo(plan.back().outputPath).size()>100,"Real compile did not produce BSP");
        require(QFileInfo(p.source).lastModified()==original,"Source was modified");
        const auto snapshot=Project::load(directory+"/project.q3mapx.json"); require(snapshot.source.startsWith(directory),"Run snapshot did not retain staged source");
        p.source=plan.back().outputPath;
        for(const auto& workflow:QStringList{"decompile","minimap"}) {
            const auto output=prepareRun(p,workflow); auto jobs=buildPlan(p,workflow,output);
            queue.enqueue(jobs); finishQueue(queue); require(queue.jobs().back().state=="Succeeded","Recovery/minimap failed");
            require(QFileInfo(jobs.back().outputPath).size()>0,"Missing workflow output");
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
