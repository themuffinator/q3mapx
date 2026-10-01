// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "light_recovery_page.h"
#include <QtWidgets>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace workbench;
static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
static QByteArray read(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly),qPrintable(path)); return f.readAll(); }
static void write(const QString& path,const QByteArray& data) { QFile f(path); require(f.open(QIODevice::WriteOnly),qPrintable(path)); require(f.write(data)==data.size(),"Write failed"); }
static bool until(const std::function<bool()>& ready,int timeout=15000) {
    QElapsedTimer timer; timer.start();
    while(!ready() && timer.elapsed()<timeout) { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,20); QThread::msleep(1); }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); return ready();
}
int main(int argc,char** argv) {
    if(argc==2 && QString::fromLocal8Bit(argv[1])=="-games") {
        const QJsonObject profile{{"id","quake3"},{"title","Legacy test compiler"},{"base_directory","baseq3"},{"shader_directory","scripts"},
            {"bsp_ident","IBSP"},{"bsp_version",46},{"native_write",true},{"aliases",QJsonArray{"q3"}},
            {"workflows",QJsonArray{"build","decompile"}},{"recovery_brush_orders",QJsonArray{"bsp","rebuild"}}};
        auto qfusion=profile; qfusion["id"]="qfusion"; qfusion["base_directory"]="base"; qfusion["bsp_ident"]="FBSP"; qfusion["bsp_version"]=1; qfusion["aliases"]=QJsonArray{};
        std::cout<<QJsonDocument(QJsonObject{{"schema_version",1},{"profiles",QJsonArray{profile,qfusion}}}).toJson().constData(); return 0;
    }
    QApplication app(argc,argv);
    try {
        require(argc==3,"Expected project and isolated work directory");
        const auto p=Project::load(argv[1]); const auto root=QDir(argv[2]).absolutePath(); require(QDir().mkpath(root),"Cannot create test directory");
        const auto originalSource=read(p.source); QJsonObject checks;
        auto old=p.toJson(); for(const auto* key:{"light_fit","apply_light_report","light_report","light_report_sha256"}) old.remove(key);
        const auto migrated=Project::fromJson(old);
        require(!migrated.applyLightReport && migrated.lightReport.isEmpty() && migrated.lightFit.family=="point","Legacy project enabled light recovery");
        require(Project::fromJson(p.toJson()).toJson()==p.toJson(),"Light project did not round trip");
        for(const auto& mutation:QList<QPair<QString,QJsonValue>>{{"family","unknown"},{"stride",0},{"max_lights",17},{"max_work",1'000'000'001},
                {"gamma",false},{"compensate",0},{"extra_distance",-1},{"refinement_steps",1.5},{"style",254},{"fixed_lights",QJsonArray{17}}}) {
            auto settings=p.lightFit.toJson(); settings[mutation.first]=mutation.second; bool rejected=false;
            try { LightFitSettings::fromJson(settings); } catch(const std::exception&) { rejected=true; }
            require(rejected,"Malformed fitting project setting accepted");
        }
        checks["project_schema_and_legacy_defaults"]=true;
        {
            LightRecoveryPage precision;
            auto precise=p; auto& settings=precise.lightFit;
            settings.spacing=96.12345678901235; settings.gamma=.012345678901234568; settings.compensate=1.1234567890123457;
            settings.extraDistance=1.1234567890123457e-12; settings.maxRMSE=2.1234567890123457; settings.minImprovement=1.1234567890123457;
            precision.setProject(precise); Project restored; precision.applyTo(restored);
            require(restored.lightFit.toJson()==precise.lightFit.toJson(),"UI rounded saved fitting settings");
            precision.findChild<QDoubleSpinBox*>("lightFitGamma")->setValue(1.2345678901234567); precision.applyTo(restored);
            require(restored.lightFit.gamma==1.2345678901234567,"UI lost edited setting precision");
        }
        checks["fitting_controls_preserve_numeric_precision"]=true;

        Window window(root+"/state"); window.loadProject(argv[1]); window.show();
        auto* page=window.findChild<LightRecoveryPage*>(); auto* reader=window.findChild<LightFitReader*>(); auto* queue=window.findChild<JobQueue*>();
        auto* navigation=window.findChild<QListWidget*>("navigation"); auto* tabs=window.findChild<QTabWidget*>("lightRecoveryTabs");
        auto* run=window.findChild<QPushButton*>("primary"); auto* fitButton=window.findChild<QPushButton*>("fitMissingLights");
        auto* use=window.findChild<QPushButton*>("useLightFit"); auto* apply=window.findChild<QCheckBox*>("applyLightReport");
        auto* workflow=window.findChild<QComboBox*>("workflow"); auto* family=window.findChild<QComboBox*>("lightFitFamily");
        auto* compiler=window.findChild<QLineEdit*>("compilerPath"); auto* proposals=window.findChild<QTableWidget*>("lightFitProposals");
        require(page && reader && queue && navigation && tabs && run && fitButton && use && apply && workflow && family && compiler && proposals,"Light recovery controls missing");
        require(until([&]{return !window.discoveringGames() && fitButton->isEnabled();}),"Light fitting capability not enabled");
        navigation->setCurrentRow(5); require(family->currentData()==p.lightFit.family,"Saved light family was lost");
        Project displayed=p; page->applyTo(displayed); require(displayed.lightFit.toJson()==p.lightFit.toJson(),"UI changed saved fitting settings");
        workflow->setCurrentIndex(workflow->findData("light-fit"));
        bool preview=false;
        for(auto* view:window.findChildren<QPlainTextEdit*>()) preview|=view->toPlainText().contains("light-request.json") && view->toPlainText().contains("fit_"+p.lightFit.family+"_lights");
        require(preview,"Fitting request/command preview missing");
        fitButton->click();
        require(queue->jobs().size()==1 && queue->jobs()[0].label=="LIGHT-FIT","Fit button did not enqueue a native fitting job");
        require(until([&]{return !queue->running() && queue->jobs()[0].state!="Queued";},600000),"Native fitting did not finish");
        require(queue->jobs()[0].state=="Succeeded",qPrintable(queue->jobs()[0].error));
        require(until([&]{return !reader->loading() && (!reader->result().path.isEmpty() || !reader->error().isEmpty());}),"Fit report not loaded after job completion");
        require(reader->error().isEmpty() && reader->result().canApply(),qPrintable(reader->error().isEmpty()?"Native fixture did not qualify":reader->error()));
        require(proposals->rowCount()==1 && use->isEnabled(),"Qualified proposal unavailable in review");
        const auto fitJob=queue->jobs()[0]; const auto report=fitJob.outputPath; const auto reportBytes=read(report); const auto canonical=reader->result().game;
        require(QJsonDocument::fromJson(read(fitJob.directory+"/light-request.json")).object()==p.lightFit.request(),"Staged fit request differs from controls");
        require(read(p.source)==originalSource,"Fitting modified source BSP");
        require(Project::load(fitJob.directory+"/project.q3mapx.json").lightFit.toJson()==p.lightFit.toJson(),"Run snapshot lost fitting settings");
        checks["native_fit_from_window_and_snapshot"]=true;
        checks["typed_settings_and_request_preview"]=true;

        navigation->setCurrentRow(5);
        for(int theme=0;theme<2;++theme) {
            for(const auto size:{QSize(1380,920),QSize(1024,720)}) for(int tab:{0,1}) {
                window.resize(size); tabs->setCurrentIndex(tab);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
                auto* area=tabs->widget(tab)->findChild<QScrollArea*>(); require(area,"Light recovery scroll area missing"); area->verticalScrollBar()->setValue(0);
                const auto path=QString("%1/%2-%3-%4.png").arg(root).arg(theme).arg(size.width()).arg(tab);
                require(window.renderPreview(path),"Light recovery preview failed");
                require(area->horizontalScrollBar()->maximum()==0,"Light recovery page needs horizontal scrolling");
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                auto* action=tab==0?fitButton:use;
                require(!area->isAncestorOf(action) && action->isVisible() && action->mapTo(tabs->widget(tab),QPoint(0,action->height())).y()<=tabs->widget(tab)->height(),"Light recovery action is not pinned within the visible page");
            }
            for(auto* action:window.findChildren<QAction*>()) if(action->text().startsWith("Toggle &light")) action->trigger();
        }
        checks["both_themes_and_compact_actions_accessible"]=true;
        tabs->setCurrentIndex(1); window.resize(1380,920);
        use->click(); require(apply->isChecked() && workflow->currentData()=="decompile" && run->isEnabled(),"Use report did not select safe decompilation");
        Project selected=p; page->applyTo(selected);
        require(selected.lightReportHash==QString::fromLatin1(QCryptographicHash::hash(reportBytes,QCryptographicHash::Sha256).toHex()),"Review selection did not pin report bytes");
        const auto folders=QDir(p.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        write(report,reportBytes+' '); bool rejected=false;
        try { prepareRun(selected,"decompile"); } catch(const std::exception& e) { rejected=QString::fromUtf8(e.what()).contains("changed"); }
        require(rejected && QDir(p.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==folders,"Changed report was staged without another review");
        write(report,reportBytes);
        checks["changed_report_rejected_before_staging"]=true;

        compiler->setText(QCoreApplication::applicationFilePath());
        require(until([&]{
            const auto* catalog=window.findChild<GameCatalog*>(); const auto* profile=catalog?catalog->find(p.game):nullptr;
            return !window.discoveringGames() && !fitButton->isEnabled() && profile && profile->title=="Legacy test compiler";
        }),"Legacy compiler still permits fitting");
        require(!run->isEnabled() && apply->isChecked() && !page->applicationError().isEmpty(),"Legacy compiler enabled selected light export or discarded choice");
        bool menuRejected=false;
        QTimer closeDialog; closeDialog.setInterval(5);
        QObject::connect(&closeDialog,&QTimer::timeout,&app,[&]{ for(auto* widget:QApplication::topLevelWidgets()) if(auto* message=qobject_cast<QMessageBox*>(widget)) { menuRejected=message->text().contains("light reports"); message->accept(); } });
        closeDialog.start();
        for(auto* action:window.findChildren<QAction*>()) if(action->text()=="&Run workflow") action->trigger();
        closeDialog.stop();
        require(menuRejected && QDir(p.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==folders,"Menu bypassed unsupported light export guard");
        compiler->setText(QDir::toNativeSeparators(p.compiler));
        require(until([&]{return !window.discoveringGames() && fitButton->isEnabled() && run->isEnabled();}),"Current compiler did not restore light workflows");
        checks["legacy_compiler_and_menu_guard"]=true;

        const auto badSource=root+"/other.bsp"; write(badSource,originalSource+'x');
        auto mismatch=readLightFitReport(report,badSource,canonical); require(!mismatch.sourceMatches && !mismatch.canApply(),"Different BSP accepted");
        mismatch=readLightFitReport(report,p.source,"different-game"); require(!mismatch.gameMatches && !mismatch.canApply(),"Different profile accepted");
        auto rejectedReport=QJsonDocument::fromJson(reportBytes).object(); const QString key=p.lightFit.family=="spot"?"spot_fit":"point_fit";
        auto fit=rejectedReport[key].toObject(); fit["accepted"]=false; fit["status"]="validation_rejected"; rejectedReport[key]=fit;
        const auto rejectedPath=root+"/rejected.json"; saveJson(rejectedPath,rejectedReport); page->reviewReport(rejectedPath);
        require(until([&]{return !reader->loading();}) && !use->isEnabled() && !run->isEnabled(),"Rejected trial enabled application");
        auto multiple=rejectedReport; auto multipleFit=multiple[key].toObject(); auto candidates=multipleFit["best_trial"].toArray(); candidates.append(candidates[0]);
        multipleFit["best_trial"]=candidates; multiple[key]=multipleFit; const auto multiplePath=root+"/multiple.json"; saveJson(multiplePath,multiple); page->reviewReport(multiplePath);
        require(until([&]{return !reader->loading();}) && proposals->rowCount()==2,"Multiple trial review failed");
        proposals->selectRow(1); auto* refinements=window.findChild<QSpinBox*>("lightFitRefinements");
        refinements->setValue(p.lightFit.refinements-1); refinements->setValue(p.lightFit.refinements);
        require(proposals->currentRow()==1,"Settings change reset the selected proposal");
        checks["review_selection_survives_settings_changes"]=true;
        auto unscored=rejectedReport; auto noFit=unscored[key].toObject(); noFit["best_trial"]=QJsonArray{}; noFit["status"]="unresolved_materials";
        const QJsonObject emptyMetric{{"samples",0},{"mae_bytes",QJsonValue(QJsonValue::Null)},{"rmse_bytes",QJsonValue(QJsonValue::Null)},{"maximum_error_bytes",QJsonValue(QJsonValue::Null)}};
        for(const auto* partition:{"training","withheld"}) noFit[partition]=QJsonObject{{"baseline",emptyMetric},{"trial",emptyMetric}};
        unscored[key]=noFit; const auto unscoredPath=root+"/unscored.json"; saveJson(unscoredPath,unscored); page->reviewReport(unscoredPath);
        require(until([&]{return !reader->loading();}) && reader->error().isEmpty() && !reader->result().accepted && !use->isEnabled(),"Unscored native rejection could not be reviewed");
        auto* scores=window.findChild<QTableWidget*>("lightFitScores"); require(scores->rowCount()==2 && scores->item(0,3)->text()=="—","Unavailable score appeared as zero error");
        checks["unscored_rejections_display_unavailable_metrics"]=true;
        page->reviewReport(report); require(until([&]{return !reader->loading() && use->isEnabled();}),"Valid report did not restore application");
        checks["mismatched_sources_and_rejected_trials"]=true;

        LightFitReader asynchronous;
        asynchronous.refresh(report,p.source,canonical); asynchronous.cancel();
        require(!asynchronous.loading() && asynchronous.result().path.isEmpty() && asynchronous.error().contains("cancelled"),"Cancellation retained a usable result");
        asynchronous.refresh(report,badSource,canonical); asynchronous.refresh(report,p.source,canonical);
        require(until([&]{return !asynchronous.loading();}) && asynchronous.result().canApply(),"Stale background read replaced current result");
        checks["reader_cancellation_and_stale_result_guard"]=true;
        const auto invalidPath=root+"/invalid.json";
        for(const auto& bytes:QList<QByteArray>{"[]",QByteArray(70,'[')+"0"+QByteArray(70,']'),
                QByteArray(reportBytes).replace("\"accepted\": true","\"accepted\": true, \"accepted\": false"),
                QByteArray(reportBytes).replace("\"source_sha256\": \"","\"source_sha256\": \"bad"),QByteArray("{\"x\":\"\0\"}",9)}) {
            write(invalidPath,bytes); bool failed=false;
            try { (void)readLightFitReport(invalidPath,p.source,canonical); } catch(const std::exception&) { failed=true; }
            require(failed,"Malformed fitting report accepted by review parser");
        }
        { QFile large(invalidPath); require(large.open(QIODevice::WriteOnly) && large.resize(64*1024*1024+1),"Cannot create oversized report control"); }
        bool largeRejected=false;
        try { (void)readLightFitReport(invalidPath,p.source,canonical); } catch(const std::exception&) { largeRejected=true; }
        require(largeRejected,"Oversized report accepted"); write(invalidPath,"oversized control completed\n");
        checks["bounded_report_parser"]=true;

        use->click(); run->click();
        require(queue->jobs().size()==2 && queue->jobs().back().label=="DECOMPILE","Reviewed report was not sent to decompilation");
        require(until([&]{return !queue->running() && queue->jobs().back().state!="Queued";},120000),"Decompilation did not finish");
        const auto exported=queue->jobs().back(); require(exported.state=="Succeeded",qPrintable(exported.error));
        require(read(exported.directory+"/selected-light-report.json")==reportBytes,"Staged report bytes changed");
        const auto snapshot=Project::load(exported.directory+"/project.q3mapx.json");
        require(snapshot.lightReport==exported.directory+"/selected-light-report.json" && snapshot.applyLightReport && snapshot.lightReportHash==selected.lightReportHash,"Recovery snapshot lost reviewed report identity");
        const auto recovery=QJsonDocument::fromJson(read(exported.outputPath+".recovery.json")).object()["light_recovery"].toObject();
        require(recovery["fitted_lights"].toInt()==1 && recovery["fixed_hypothesis_lights"].toInt()==p.lightFit.fixedLights.size(),"Native export lost fitted/fixed lights");
        require(read(p.source)==originalSource,"Workflow changed source BSP");
        for(const auto& workflowName:QStringList{"build","bsp","vis","light","obj","ase","minimap"})
            for(const auto& job:buildPlan(snapshot,workflowName,root)) require(!job.arguments.contains("-light-proposals") && !job.arguments.contains("-probes"),"Recovery options escaped their workflow");
        checks["reviewed_native_export_and_isolated_snapshot"]=true;
        checks["source_preserved_and_other_workflows_unchanged"]=true;
        fitButton->click(); require(queue->jobs().size()==3 && queue->running(),"Fit cancellation fixture did not start");
        queue->cancel();
        require(until([&]{return !queue->running();}) && queue->jobs().back().state=="Cancelled","Native fit cancellation failed");
        require(read(report)==reportBytes && read(p.source)==originalSource && reader->result().path==QFileInfo(report).absoluteFilePath(),"Cancelled fitting changed prior evidence or selection");
        checks["native_fit_cancellation_preserves_prior_report"]=true;
        checks["no_os_input_or_capture"]=true;
        saveJson(root+"/result.json",{{"fit_report",report},{"recovered_map",exported.outputPath},{"checks",checks}});
        std::cout<<"Window fit/review/export, parser, staging, capability and layout checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
