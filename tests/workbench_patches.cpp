// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "patch_recovery_page.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace workbench;
static void require(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
static QByteArray read(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly),qPrintable(path)); return f.readAll(); }
static void write(const QString& path,const QByteArray& data) { QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(data)==data.size(),"Cannot write test input"); }
static bool until(const std::function<bool()>& ready,int timeout=15000) {
    QElapsedTimer t; t.start();
    while(!ready() && t.elapsed()<timeout) { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,20); QThread::msleep(1); }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); return ready();
}
int main(int argc,char** argv) {
    if(argc==2 && QString::fromLocal8Bit(argv[1])=="-games") {
        QJsonArray profiles;
        for(const auto* game:{"quake3","ja"}) profiles.append(QJsonObject{{"id",game},{"title","Older compiler fixture"},{"base_directory","baseq3"},
            {"shader_directory","scripts"},{"bsp_ident","IBSP"},{"bsp_version",46},{"native_write",true},{"aliases",QJsonArray{}},{"workflows",QJsonArray{"decompile"}}});
        std::cout<<QJsonDocument(QJsonObject{{"schema_version",1},{"profiles",profiles}}).toJson().constData(); return 0;
    }
    QApplication app(argc,argv);
    try {
        require(argc==3,"Expected project and work directory");
        const auto p=Project::load(argv[1]); const auto root=QDir(argv[2]).absolutePath(); require(QDir().mkpath(root),"Cannot create work directory");
        const auto source=read(p.source); QJsonObject checks;
        require(Project::fromJson(p.toJson()).toJson()==p.toJson(),"Patch settings lost on project round trip");
        auto legacy=p.toJson(); for(const auto* key:{"patch_recovery","patch_colors","patch_color_subdivisions","patch_fit_work_limit"}) legacy.remove(key);
        const auto old=Project::fromJson(legacy);
        require(old.patchRecovery=="none" && old.patchColors=="none" && old.patchColorSubdivisions==16 && old.patchFitWorkLimit==50'000'000,"Older project enabled patch recovery");
        const auto oldArgs=buildPlan(old,"decompile",root).front().arguments;
        for(const auto* flag:{"-patch-recovery","-patch-colors","-patch-color-subdivisions","-patch-fit-work"}) require(!oldArgs.contains(flag),"Default command changed");
        for(const auto* key:{"patch_recovery","patch_colors"}) for(const auto& value:QJsonArray{17,false,"bad","",QJsonValue(QJsonValue::Null)}) {
            auto bad=p.toJson(); bad[key]=value; bool rejected=false;
            try { Project::fromJson(bad); } catch(const std::exception&) { rejected=true; }
            require(rejected,"Invalid patch project policy accepted");
        }
        for(const auto* key:{"patch_color_subdivisions","patch_fit_work_limit"}) for(const auto& value:QJsonArray{0,-1,1.5,false,"16",QJsonValue(QJsonValue::Null),1'000'000'001}) {
            auto bad=p.toJson(); bad[key]=value; bool rejected=false;
            try { Project::fromJson(bad); } catch(const std::exception&) { rejected=true; }
            require(rejected,"Invalid patch project number accepted");
        }
        auto bad=p.toJson(); bad["patch_color_subdivisions"]=3; bool rejected=false;
        try { Project::fromJson(bad); } catch(const std::exception&) { rejected=true; }
        require(rejected,"Non-power-of-two sampling accepted");
        for(const auto* workflow:{"bsp","light","obj","minimap"}) for(const auto& job:buildPlan(p,workflow,root))
            for(const auto* flag:{"-patch-recovery","-patch-colors","-patch-color-subdivisions","-patch-fit-work"}) require(!job.arguments.contains(flag),"Patch setting escaped MAP recovery");
        checks["project_roundtrip_defaults_validation_workflow_scope"]=true;
        std::cout<<"Project checks passed"<<std::endl;

        Window window(root+"/state"); window.loadProject(argv[1]); window.show();
        auto* page=window.findChild<PatchRecoveryPage*>(); auto* reader=window.findChild<PatchReviewReader*>(); auto* queue=window.findChild<JobQueue*>();
        auto* mode=window.findChild<QComboBox*>("patchRecoveryMode"); auto* colors=window.findChild<QComboBox*>("patchRecoveryColors");
        auto* sampling=window.findChild<QComboBox*>("patchRecoverySubdivisions"); auto* work=window.findChild<QSpinBox*>("patchRecoveryWorkLimit");
        auto* run=window.findChild<QPushButton*>("primary"); auto* recover=window.findChild<QPushButton*>("recoverPatches");
        auto* navigation=window.findChild<QListWidget*>("navigation"); auto* tabs=window.findChild<QTabWidget*>("patchRecoveryTabs");
        auto* table=window.findChild<QTableWidget*>("patchReviewDecisions"); auto* filter=window.findChild<QComboBox*>("patchReviewFilter");
        auto* summary=window.findChild<QLabel*>("patchReviewSummary"); auto* compiler=window.findChild<QLineEdit*>("compilerPath");
        auto* preview=window.findChild<QPlainTextEdit*>("commandPreview");
        require(page && reader && queue && mode && colors && sampling && work && recover && navigation && tabs && table && filter && summary && compiler && preview,"Patch recovery UI missing");
        require(until([&]{return !window.discoveringGames() && recover->isEnabled();}),"Patch recovery capabilities unavailable");
        Project displayed=p; page->applyTo(displayed);
        require(displayed.toJson()==p.toJson(),"Patch controls changed saved settings");
        work->setValue(7'654'321); sampling->setCurrentIndex(sampling->findData(8));
        require((p.patchRecovery!="auto" && p.patchRecovery!="fit") || preview->toPlainText().contains("-patch-fit-work 7654321"),"Fitting budget missing from preview");
        require(p.patchColors=="none" || preview->toPlainText().contains("-patch-color-subdivisions 8"),"Native color sampling missing from preview");
        QAction* save=nullptr; QAction* runAction=nullptr; QAction* theme=nullptr;
        for(auto* action:window.findChildren<QAction*>()) {
            if(action->shortcut()==QKeySequence::Save) save=action;
            if(action->shortcut()==QKeySequence(Qt::Key_F5)) runAction=action;
            if(action->text().contains("light / dark")) theme=action;
        }
        require(save && runAction && theme,"Window actions missing"); save->trigger();
        const auto saved=Project::load(argv[1]);
        require(saved.patchRecovery==p.patchRecovery && saved.patchColors==p.patchColors && saved.patchColorSubdivisions==8 && saved.patchFitWorkLimit==7'654'321,"Window save lost patch settings");
        recover->click();
        require(queue->jobs().size()==1 && queue->jobs()[0].label=="DECOMPILE","Patch page did not queue MAP recovery");
        require(until([&]{return !queue->running() && queue->jobs()[0].state!="Queued";},120000),"Recovery queue did not finish");
        const auto job=queue->jobs()[0]; require(job.state=="Succeeded",qPrintable(job.error));
        require(until([&]{return !reader->loading() && (!reader->result().path.isEmpty() || !reader->error().isEmpty());}),"Recovery results did not load");
        require(reader->error().isEmpty(),qPrintable(reader->error()));
        const auto reviewed=reader->result();
        require(!reviewed.decisions.isEmpty() && table->rowCount()==reviewed.decisions.size(),"Recovery decision table is incomplete");
        const bool archived=p.patchRecovery=="source" || (p.patchRecovery=="auto" && source.endsWith(QByteArray("Q3MAPX_PATCH_V1\0",16)));
        require(reviewed.archived==(archived?2:0) && reviewed.fitted==((p.patchRecovery=="fit" || (p.patchRecovery=="auto" && !archived))?2:0)
            && reviewed.native==(p.patchRecovery=="none"?2:0),"Report evidence categories are incorrect");
        require(summary->textFormat()==Qt::PlainText && summary->accessibleDescription().contains(reviewed.input)
            && summary->text().contains(QFileInfo(reviewed.input).fileName()),"Report source or plain text summary missing");
        const auto snapshot=Project::load(job.directory+"/project.q3mapx.json");
        require(snapshot.patchRecovery==saved.patchRecovery && snapshot.patchColors==saved.patchColors && snapshot.patchFitWorkLimit==saved.patchFitWorkLimit
            && snapshot.patchColorSubdivisions==saved.patchColorSubdivisions,"Run snapshot lost patch choices");
        require(read(p.source)==source && read(snapshot.source)==source,"Recovery staging modified source bytes");
        checks["native_window_queue_report_snapshot_and_source_preservation"]=true;
        std::cout<<"Native queue and report checks passed"<<std::endl;
        navigation->setCurrentRow(6);
        for(int t=0;t<2;++t) {
            for(auto size:{QSize(1380,920),QSize(1024,720)}) for(int tab:{0,1}) {
                window.resize(size); tabs->setCurrentIndex(tab); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
                require(window.renderPreview(root+QString("/patch-%1-%2-%3.png").arg(t).arg(size.width()).arg(tab)),"Window failed to render patch page");
                if(tab==0) {
                    auto* scroll=qobject_cast<QScrollArea*>(tabs->widget(0)); require(scroll && scroll->horizontalScrollBar()->maximum()==0,"Settings clip horizontally");
                    for(auto* control:{mode,colors,sampling}) require(control->height()>=control->sizeHint().height(),"Settings squeezed below readable height");
                } else require(table->viewport()->height()>=3*table->rowHeight(0),"Results show fewer than three rows");
            }
            theme->trigger();
        }
        filter->setCurrentIndex(2); require(table->rowCount()==int(reviewed.skipped),"Skipped filter lost decisions");
        filter->setCurrentIndex(1); require(table->rowCount()>0,"Accepted filter lost decisions"); filter->setCurrentIndex(0);
        checks["themes_sizes_filters_and_owned_widget_rendering"]=true;
        std::cout<<"Owned widget rendering and filter checks passed"<<std::endl;

        const auto report=job.outputPath+".recovery.json"; const auto bytes=read(report); auto document=QJsonDocument::fromJson(bytes).object();
        int rejections=0;
        const auto reject=[&](const QByteArray& value){
            std::cout<<"Malformed report check "<<rejections<<std::endl;
            const auto path=root+"/invalid.json"; write(path,value); bool failed=false;
            try { readPatchReview(path); } catch(const std::exception&) { failed=true; }
            require(failed,"Malformed recovery report accepted"); ++rejections;
        };
        reject("{broken"); reject("[]"); reject(bytes.left(bytes.size()/2));
        auto duplicate=bytes; duplicate.insert(duplicate.indexOf('{')+1,"\"schema_version\":1,"); reject(duplicate);
        reject("{\"nested\":"+QByteArray(80,'[')+"0"+QByteArray(80,']')+"}");
        reject("{\"bad\":\""+QByteArray(1,char(0xff))+"\"}");
        for(const auto& mutation:QList<QPair<QString,QJsonValue>>{{"schema_version",2},{"input",false},{"format","obj"}}) {
            auto changed=document; changed[mutation.first]=mutation.second; reject(QJsonDocument(changed).toJson());
        }
        if(document.contains("patch_recovery")) {
            auto changed=document; auto recovery=changed["patch_recovery"].toObject(); recovery["author_identity_authenticated"]=true;
            changed["patch_recovery"]=recovery; reject(QJsonDocument(changed).toJson());
            if(p.patchRecovery=="source") { recovery["author_identity_authenticated"]=false; recovery["geometry_binding_verified"]=false; changed["patch_recovery"]=recovery; reject(QJsonDocument(changed).toJson()); }
            else {
                recovery=document["patch_recovery"].toObject(); auto fit=recovery["triangle_fitting"].toObject(); fit["omitted_records"]=17;
                recovery["triangle_fitting"]=fit; changed["patch_recovery"]=recovery; reject(QJsonDocument(changed).toJson());
                const auto originalFit=document["patch_recovery"].toObject()["triangle_fitting"].toObject();
                for(const auto& mutation:QList<QPair<QString,QJsonValue>>{{"fitted_patches",99999},{"work_limit",0},{"work_used",1'000'000'001},
                    {"basis","original_source"},{"density_basis","original_author_value"},{"color_policy","unknown"}}) {
                    fit=originalFit; fit[mutation.first]=mutation.second; recovery["triangle_fitting"]=fit;
                    changed["patch_recovery"]=recovery; reject(QJsonDocument(changed).toJson());
                }
            }
        } else {
            auto changed=document; auto channels=changed["patch_colors"].toObject(); channels["omitted_records"]=17;
            changed["patch_colors"]=channels; reject(QJsonDocument(changed).toJson());
        }
        if(document.contains("patch_colors")) for(const auto& mutation:QList<QPair<QString,QJsonValue>>{{"basis","original_source"},
            {"original_paint_proven",true},{"counts",QJsonArray{}},{"omitted_records",-1}}) {
            auto changed=document; auto channels=changed["patch_colors"].toObject(); channels[mutation.first]=mutation.second;
            changed["patch_colors"]=channels; reject(QJsonDocument(changed).toJson());
        }
        const auto oversized=root+"/oversized.json";
        { QFile f(oversized); require(f.open(QIODevice::WriteOnly) && f.resize(64*1024*1024+1),"Cannot create size-limit fixture"); }
        rejected=false; try { readPatchReview(oversized); } catch(const std::exception&) { rejected=true; }
        require(rejected && QFile::remove(oversized),"Oversized report accepted or disposable fixture could not be removed"); ++rejections;
        reader->refresh(root+"/invalid.json"); reader->refresh(report);
        require(until([&]{return !reader->loading();}) && reader->result().reportHash==reviewed.reportHash,"Stale report replaced newest request");
        reader->refresh(report); reader->cancel();
        require(!reader->loading() && reader->result().path.isEmpty() && reader->error().contains("cancelled"),"Cancelled report remained published");
        page->reviewReport(report); require(until([&]{return !reader->loading();}) && reader->error().isEmpty(),"Reader failed after cancellation");
        checks["malformed_report_rejections"]=rejections; checks["asynchronous_supersession_and_cancellation"]=true;
        if(p.game=="quake3" && p.patchRecovery=="fit") {
            auto large=document; auto recovery=large["patch_recovery"].toObject(); auto fit=recovery["triangle_fitting"].toObject();
            QJsonObject accepted;
            for(const auto& v:fit["decisions"].toArray()) if(v.toObject()["status"]=="fitted") { accepted=v.toObject(); break; }
            require(!accepted.isEmpty(),"Large report lacks a fitted fixture"); QJsonArray fits,channels;
            for(int i=0;i<10000;++i) { fits.append(accepted); channels.append(QJsonObject{{"surface",i},{"status","recovered"}}); }
            fit["decisions"]=fits; fit["counts"]=QJsonObject{{"fitted",10007}}; fit["fitted_patches"]=10007; fit["omitted_records"]=7;
            recovery["triangle_fitting"]=fit; large["patch_recovery"]=recovery;
            auto native=large["patch_colors"].toObject(); native["patches"]=channels; native["counts"]=QJsonObject{{"recovered",10000}};
            native["omitted_records"]=0; large["patch_colors"]=native;
            const auto path=root+"/large.json"; write(path,QJsonDocument(large).toJson(QJsonDocument::Compact)); page->reviewReport(path);
            require(until([&]{return !reader->loading();}) && reader->error().isEmpty(),"Bounded large report rejected");
            require(table->rowCount()==20000 && reader->result().omitted==7 && reader->result().fitted==10007
                && summary->text().contains("7 omitted records"),"Truncated report lost totals or bounded rows");
            filter->setCurrentIndex(2); require(table->rowCount()==0,"Large skipped filter retained accepted rows"); filter->setCurrentIndex(0);
            require(table->rowCount()==20000,"Large report filter lost rows");
            fit["omitted_records"]=0; recovery["triangle_fitting"]=fit; large["patch_recovery"]=recovery;
            reject(QJsonDocument(large).toJson(QJsonDocument::Compact));
            page->reviewReport(report); require(until([&]{return !reader->loading();}) && reader->error().isEmpty(),"Reader failed after large report");
            require(QFile::remove(path),"Cannot remove disposable large report fixture");
            checks["large_report_rows"]=20000; checks["omitted_report_records"]=7; checks["malformed_report_rejections"]=rejections;
        }
        std::cout<<"Reader checks passed"<<std::endl;

        auto* catalog=window.findChild<GameCatalog*>(); require(catalog,"Window catalog missing");
        const auto switchCompiler=[&](const QString& path){
            QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool finished=false;
            QObject::connect(catalog,&GameCatalog::changed,&loop,[&]{ if(!catalog->loading()) { finished=true; loop.quit(); } });
            QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
            compiler->setText(path); require(!run->isEnabled(),"Pending compiler change allowed patch recovery");
            timeout.start(10000); loop.exec(); require(finished,"Replacement compiler catalog timed out");
        };
        switchCompiler(QCoreApplication::applicationFilePath());
        require(!run->isEnabled() && !recover->isEnabled(),"Older compiler allowed patch recovery");
        require(mode->currentData()==p.patchRecovery && colors->currentData()==p.patchColors,"Unsupported compiler silently reset choices");
        const auto folders=QDir(p.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        bool blocked=false; QTimer::singleShot(0,&app,[&]{
            auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            blocked=dialog && dialog->text().contains("does not advertise");
            if(dialog) dialog->accept();
        }); runAction->trigger();
        require(blocked && queue->jobs().size()==1 && QDir(p.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==folders,"Unsupported recovery staged work");
        mode->setCurrentIndex(mode->findData("none")); colors->setCurrentIndex(colors->findData("none"));
        require(run->isEnabled() && recover->isEnabled() && !preview->toPlainText().contains("-patch-recovery"),"Older compiler lost ordinary recovery");
        switchCompiler(p.compiler);
        auto* profiles=window.findChild<QComboBox*>("gameProfiles"); profiles->setCurrentText("alice"); mode->setCurrentIndex(mode->findData("fit"));
        require(!run->isEnabled() && !recover->isEnabled(),"Recovery-only profile allowed triangle fitting");
        checks["older_compiler_menu_and_native_profile_guards"]=true;
        saveJson(root+"/validation.json",{{"checks",checks},{"recovered_map",job.outputPath},{"report",report},{"snapshot",snapshot.toJson()}});
        std::cout<<"Native patch UI, queued compiler recovery, report review, persistence, compatibility and malformed-data checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
