// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "inspection_page.h"
#include "hardware_page.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QScrollArea>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QDir>
#include <QTimer>
#include <QToolButton>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    if(argc==2 && QString::fromLocal8Bit(argv[1])=="-games") {
        QCoreApplication app(argc,argv);
        // Older compiler: writable and rebuild-order capable, but no inference metadata.
        const QJsonObject profile{{"id","quake3"},{"title","Older compiler fixture"},{"base_directory","baseq3"},
            {"shader_directory","scripts"},{"bsp_ident","IBSP"},{"bsp_version",46},{"native_write",true},
            {"aliases",QJsonArray()},{"workflows",QJsonArray{"build","decompile"}},
            {"recovery_brush_orders",QJsonArray{"bsp","rebuild"}}};
        std::cout << QJsonDocument(QJsonObject{{"schema_version",1},{"profiles",QJsonArray{profile}}}).toJson().constData();
        return 0;
    }
    QApplication app(argc, argv);
    require(argc == 4, "Expected project, isolated settings directory and device-query fixture executable");
    require(QDir().mkpath(argv[2]),"Cannot create isolated window test output directory");
    const auto project = workbench::Project::load(argv[1]);
    QFile source(project.source); require(source.open(QIODevice::ReadOnly), "Cannot read fixture source");
    const auto original = source.readAll(); source.close();
    workbench::Window window(argv[2]);
    const auto editableProject=QDir(argv[2]).filePath("editable-project.q3mapx.json");
    project.save(editableProject); window.loadProject(editableProject);
    window.show(); // The offscreen platform never creates a visible desktop window.
    const auto settleLayouts=[] {
        QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const auto renderPreview=[&](const QString& path) {
        settleLayouts();
        return window.renderPreview(path);
    };
    auto* queue = window.findChild<workbench::JobQueue*>();
    require(queue, "Window has no build queue");
    bool launched = false;
    auto* inspector=window.findChild<workbench::BspInspector*>();
    auto* inspectionPage=window.findChild<workbench::InspectionPage*>();
    auto* inspectFile=window.findChild<QLineEdit*>("inspectionFile");
    auto* inspectButton=window.findChild<QPushButton*>("inspectBsp");
    auto* lumpTable=window.findChild<QTableWidget*>("inspectionLumps");
    require(inspector && inspectionPage && inspectFile && inspectButton && lumpTable,"Inspector controls missing");
    auto* inventory=window.findChild<workbench::DeviceInventory*>();
    auto* devices=window.findChild<QTableWidget*>("computeDevices");
    auto* refreshDevices=window.findChild<QPushButton*>("refreshDevices");
    auto* cancelDevices=window.findChild<QPushButton*>("cancelDevices");
    auto* deviceDetails=window.findChild<QPlainTextEdit*>("deviceDetails");
    auto* deviceJson=window.findChild<QPlainTextEdit*>("deviceJson");
    auto* compilerPath=window.findChild<QLineEdit*>("compilerPath");
    require(inventory && devices && refreshDevices && cancelDevices && deviceDetails && deviceJson && compilerPath,"Device controls missing");
    int devicePhase=0;
    QObject::connect(inventory,&workbench::DeviceInventory::changed,&app,[&]{
        if(inventory->loading() || inventory->report().isEmpty()) return;
        require(inventory->error().isEmpty(),"Window device inventory failed");
        require(devices->rowCount()==inventory->report()["devices"].toArray().size(),"Device table omitted inventory rows");
        require(QJsonDocument::fromJson(deviceJson->toPlainText().toUtf8()).object()==inventory->report(),"Raw device report changed");
        require(refreshDevices->isEnabled() && !cancelDevices->isEnabled(),"Finished query left incorrect controls");
        if(devicePhase==1) {
            require(renderPreview(QDir(argv[2]).filePath("hardware-native.png")),"Native hardware page did not render");
            devicePhase=2;
            compilerPath->setText(argv[3]);
            require(inventory->report().isEmpty() && devices->rowCount()==0 && deviceJson->toPlainText().isEmpty(),"Changed compiler retained stale devices");
            qputenv("Q3MAPX_TEST_DEVICES_MODE","valid"); refreshDevices->click();
        } else if(devicePhase==2) {
            require(devices->rowCount()==2 && devices->item(1,1)->text().contains("<b>plain text</b>"),"Device names were not preserved as text");
            devices->selectRow(1);
            require(deviceDetails->toPlainText().contains("OpenCL 1.2") && deviceDetails->toPlainText().contains("shared with the host"),"Device selection did not update capabilities");
            require(renderPreview(QDir(argv[2]).filePath("hardware-devices.png")),"Hardware page did not render");
            window.resize(1024,720);
            require(renderPreview(QDir(argv[2]).filePath("hardware-compact.png")),"Compact hardware page did not render");
            require(devices->viewport()->height()>=3*devices->verticalHeader()->defaultSectionSize(),"Compact hardware page cannot show three complete rows");
            require(devices->horizontalScrollBar()->maximum()==0,"Compact hardware columns exceed the viewport");
            for(auto* action:window.findChildren<QAction*>()) if(action->text().startsWith("Toggle &light")) action->trigger();
            require(renderPreview(QDir(argv[2]).filePath("hardware-light.png")),"Light hardware page did not render");
            devicePhase=3;
            qputenv("Q3MAPX_TEST_DEVICES_MODE","slow"); refreshDevices->click();
            require(inventory->loading() && !refreshDevices->isEnabled() && cancelDevices->isEnabled(),"Active query controls incorrect");
            cancelDevices->click();
            require(!inventory->loading() && inventory->error().contains("cancelled") && devices->rowCount()==0,"UI cancellation retained devices");
            refreshDevices->click(); compilerPath->setText(QDir::toNativeSeparators(project.compiler));
            require(!inventory->loading() && inventory->error().isEmpty() && inventory->report().isEmpty() && deviceJson->toPlainText().isEmpty(),"Compiler change did not cancel and clear an active query");
            qunsetenv("Q3MAPX_TEST_DEVICES_MODE");
            // Give killed children their finish events: they must not repopulate the UI.
            QTimer::singleShot(100,&app,[&]{
                require(devices->rowCount()==0 && inventory->report().isEmpty() && inventory->error().isEmpty(),"Late query updated changed compiler UI");
                workbench::saveJson(QDir(argv[2]).filePath("hardware-checks.json"),{
                    {"real_inventory",true},{"synthetic_devices",2},{"selection_capabilities",true},{"plain_text_names",true},
                    {"inventory_json",true},{"compact_columns_fit",true},{"compact_three_rows",true},{"light_theme",true},
                    {"cancellation",true},{"compiler_change_cancels",true},{"stale_ui_results_cleared",true}});
                std::cout << "Actual window build, inspection, hardware devices, cancellation and compiler changes passed without input injection\n";
                app.exit(0); // End this test without invoking the window's unsaved-project close prompt.
            });
        }
    });
    int inspectionPhase=0;
    QString inspectedSource;
    QObject::connect(inspector,&workbench::BspInspector::changed,&app,[&]{
        if(inspector->loading() || inspector->report().isEmpty()) return;
        require(inspector->error().isEmpty(),"Window inspection query failed");
        if(inspectionPhase==1) {
            require(inspector->report()["valid"].toBool() && lumpTable->rowCount()==17,"Valid directory missing from window");
            require(renderPreview(QDir(argv[2]).filePath("bsp-inspection.png")),"Inspector did not render");
            const auto fullSize=window.size(); window.resize(1024,720);
            require(renderPreview(QDir(argv[2]).filePath("bsp-inspection-compact.png")),"Compact inspector did not render");
            require(lumpTable->viewport()->height()>=3*lumpTable->rowHeight(0),"Compact inspection cannot show three complete directory rows");
            window.resize(fullSize);
            const auto saved=QDir(argv[2]).filePath("inspection.json"); inspectionPage->saveReport(saved);
            QFile report(saved); require(report.open(QIODevice::ReadOnly),"Inspection report not saved");
            require(QJsonDocument::fromJson(report.readAll()).object()==inspector->report(),"Saved inspection changed its fields");
            bool protectedSource=false;
            try { inspectionPage->saveReport(inspectedSource); } catch(const std::exception&) { protectedSource=true; }
            require(protectedSource,"Report could overwrite the BSP source");
#ifdef Q_OS_WIN
            protectedSource=false;
            try { inspectionPage->saveReport(inspectedSource.toUpper()); } catch(const std::exception&) { protectedSource=true; }
            require(protectedSource,"Case-folded report path could overwrite the BSP source");
#endif
            const auto ambiguous=QDir(argv[2]).filePath("ambiguous.bsp");
            QByteArray header(152,0); header.replace(0,4,"IBSP"); qToLittleEndian<qint32>(47,header.data()+4);
            QFile bsp(ambiguous); require(bsp.open(QIODevice::WriteOnly) && bsp.write(header)==header.size(),"Cannot create ambiguous directory fixture"); bsp.close();
            inspectionPhase=2; inspectFile->setText(ambiguous);
            require(inspector->report().isEmpty() && lumpTable->rowCount()==0,"Changed input retained stale results");
            inspectButton->click();
        } else if(inspectionPhase==2) {
            auto* layouts=window.findChild<QComboBox*>("inspectionLayouts");
            require(inspector->report()["ambiguous_game"].toBool() && layouts->count()==2,"Ambiguous layouts missing in the window");
            layouts->setCurrentIndex(0); const int firstRows=lumpTable->rowCount();
            layouts->setCurrentIndex(1); const int secondRows=lumpTable->rowCount();
            require((firstRows==17 && secondRows==18) || (firstRows==18 && secondRows==17),"Layout selection did not switch directories");
            inspectionPhase=3; inspectFile->setText(QDir(argv[2]).filePath("does-not-exist.bsp")); inspectButton->click();
        } else if(inspectionPhase==3) {
            require(!inspector->report()["valid"].toBool() && !inspector->report()["errors"].toArray().isEmpty(),"Invalid directory diagnostics not shown");
            require(window.findChild<QPushButton*>("saveInspection")->isEnabled(),"Invalid report cannot be exported");
            window.findChild<QListWidget*>("navigation")->setCurrentRow(3);
            devicePhase=1; refreshDevices->click();
        }
    });
    QTimer ready;
    QObject::connect(&ready, &QTimer::timeout, &app, [&] {
        if (window.discoveringGames() || launched) return;
        ready.stop(); // Rendering and modal error checks can enter the event loop.
        auto* profiles = window.findChild<QComboBox*>("gameProfiles");
        require(profiles && profiles->count() >= 19, "Window did not use the compiler catalog");
        require(profiles->currentText() == project.game, "Catalog replaced the saved project selection");
        auto* button=window.findChild<QPushButton*>("primary");
        require(button, "Run button missing");
        auto* order=window.findChild<QComboBox*>("recoveryBrushOrder");
        auto* orderHint=window.findChild<QLabel*>("recoveryOrderHint");
        auto* preview=window.findChild<QPlainTextEdit*>("commandPreview");
        auto* workflow=window.findChild<QComboBox*>("workflow");
        auto* projectTabs=window.findChild<QTabWidget*>("projectOptions");
        auto* flags=window.findChild<QComboBox*>("recoveryDetailPolicy");
        auto* groups=window.findChild<QComboBox*>("recoveryGroupPolicy");
        auto* detailBudget=window.findChild<QSpinBox*>("recoveryDetailWorkLimit");
        auto* groupBudget=window.findChild<QSpinBox*>("recoveryGroupWorkLimit");
        auto* limitsToggle=window.findChild<QToolButton*>("recoveryLimitsToggle");
        require(order && orderHint && preview && workflow && projectTabs && flags && groups && detailBudget && groupBudget && limitsToggle,"Recovery controls missing");
        require(order->currentData().toString()==project.brushOrder && project.brushOrder=="rebuild","Saved recovery setting did not reach the window");
        require(flags->currentData()=="cells" && groups->currentData()=="surfaces"
            && detailBudget->value()==project.detailWorkLimit && groupBudget->value()==project.groupWorkLimit,"Saved inference choices/limits did not reach the window");
        const auto available=[&]{ return order->model()->flags(order->model()->index(order->findData("rebuild"),0)).testFlag(Qt::ItemIsEnabled); };
        const auto policyAvailable=[](QComboBox* combo,const char* value){ return combo->model()->flags(combo->model()->index(combo->findData(value),0)).testFlag(Qt::ItemIsEnabled); };
        workflow->setCurrentIndex(workflow->findData("decompile"));
        require(available() && button->isEnabled() && preview->toPlainText().contains("-brush-order rebuild"),"Writable profile did not enable rebuild order");
        require(policyAvailable(flags,"cells") && policyAvailable(groups,"surfaces") && !policyAvailable(order,"bsp")
            && preview->toPlainText().contains("-detail-policy cells") && preview->toPlainText().contains("-group-policy surfaces"),"Inference options/dependency missing");
        profiles->setCurrentText("alice");
        require(!available() && !button->isEnabled() && order->currentData()=="rebuild","Unsupported profile silently changed or accepted saved recovery order");
        require(!policyAvailable(flags,"cells") && !policyAvailable(groups,"surfaces")
            && flags->currentData()=="cells" && groups->currentData()=="surfaces","Native profile changed or enabled inference selections");
        require(orderHint->text().contains("Select Flat world geometry"),"Recovery-only restriction has no usable explanation");
        // The menu/shortcut action must enforce the same rule as the button.
        // Dismiss this isolated offscreen test dialog directly, without input events.
        QAction* runAction=nullptr;
        for(auto* action:window.findChildren<QAction*>()) if(action->shortcut()==QKeySequence(Qt::Key_F5)) runAction=action;
        require(runAction,"Run workflow action missing");
        const auto rejectRecovery=[&](const QString& reason){
            bool rejected=false;
            const auto outputs=QDir(project.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
            QTimer::singleShot(0,&app,[&]{
                auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                require(dialog && dialog->text().contains(reason),"Unsupported recovery was not rejected before staging");
                rejected=true; dialog->accept();
            });
            runAction->trigger();
            require(rejected && queue->jobs().isEmpty(),"Unsupported recovery enqueued compiler work");
            require(QDir(project.outputRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==outputs,"Unsupported recovery created an output folder");
        };
        rejectRecovery("Surface group inference");
        groups->setCurrentIndex(groups->findData("none"));
        order->setCurrentIndex(order->findData("bsp"));
        require(!button->isEnabled() && orderHint->text().contains("Cell detail inference"),"Unsupported detail inference was accepted");
        rejectRecovery("Cell detail inference");
        flags->setCurrentIndex(flags->findData("legacy"));
        require(button->isEnabled() && !preview->toPlainText().contains("-brush-order"),"Default native recovery was blocked or changed");
        profiles->setCurrentText("unknown-profile"); require(!available() && !button->isEnabled(),"Unknown profile enabled rebuild order");
        profiles->setCurrentText(project.game); order->setCurrentIndex(order->findData("rebuild"));
        require(available() && button->isEnabled(),"Writable profile did not restore recovery controls");
        order->setCurrentIndex(order->findData("bsp")); flags->setCurrentIndex(flags->findData("cells"));
        groups->setCurrentIndex(groups->findData("surfaces"));
        require(order->currentData()=="rebuild" && !policyAvailable(order,"bsp"),"Group selection did not choose/require rebuild order");
        detailBudget->setValue(3'456'789); groupBudget->setValue(4'567'890);
        require(preview->toPlainText().contains("-detail-max-work 3456789") && preview->toPlainText().contains("-group-max-work 4567890"),"Analysis limits absent from command preview");
        QAction* saveAction=nullptr;
        for(auto* action:window.findChildren<QAction*>()) if(action->shortcut()==QKeySequence::Save) saveAction=action;
        require(saveAction,"Save action missing"); saveAction->trigger();
        const auto saved=workbench::Project::load(editableProject);
        require(saved.detailPolicy=="cells" && saved.groupPolicy=="surfaces" && saved.brushOrder=="rebuild"
            && saved.detailWorkLimit==3'456'789 && saved.groupWorkLimit==4'567'890,"Window save lost inference choices/limits");
        auto* catalog=window.findChild<workbench::GameCatalog*>(); require(catalog,"Window catalog missing");
        const auto switchCompiler=[&](const QString& path){
            QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool finished=false;
            QObject::connect(catalog,&workbench::GameCatalog::changed,&loop,[&]{ if(!catalog->loading()) { finished=true; loop.quit(); } });
            QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
            compilerPath->setText(path); require(!button->isEnabled(),"Pending compiler change allowed inference");
            timeout.start(10000); loop.exec(); require(finished,"Replacement compiler catalog timed out");
        };
        switchCompiler(QCoreApplication::applicationFilePath());
        require(!policyAvailable(flags,"cells") && !policyAvailable(groups,"surfaces") && !button->isEnabled()
            && flags->currentData()=="cells" && groups->currentData()=="surfaces","Older compiler enabled or replaced inference choices");
        groups->setCurrentIndex(groups->findData("none")); flags->setCurrentIndex(flags->findData("legacy"));
        require(button->isEnabled(),"Older compiler blocked ordinary supported recovery");
        groups->setCurrentIndex(groups->findData("surfaces"));
        require(!button->isEnabled() && orderHint->text().contains("Surface group inference"),"Older compiler advertised unimplemented grouping");
        rejectRecovery("Surface group inference");
        workflow->setCurrentIndex(workflow->findData("geometry-optimize"));
        require(!button->isEnabled() && !policyAvailable(workflow,"geometry-optimize"),"Older compiler enabled geometry rewriting");
        rejectRecovery("Geometry analysis and optimization require advertised support");
        workflow->setCurrentIndex(workflow->findData("decompile"));
        flags->setCurrentIndex(flags->findData("cells")); switchCompiler(QDir::toNativeSeparators(project.compiler));
        require(button->isEnabled() && policyAvailable(flags,"cells") && policyAvailable(groups,"surfaces"),"Current compiler did not restore inference");
        require(projectTabs->tabText(2)=="Recovery","Recovery tab missing"); projectTabs->setCurrentIndex(2);
        const auto originalSize=window.size();
        for(int theme=0;theme<2;++theme) {
            const auto themeName=window.styleSheet().contains("#141a21") ? "dark" : "light";
            window.resize(1380,920);
            require(renderPreview(QDir(argv[2]).filePath(QString("recovery-%1.png").arg(themeName))),"Recovery options did not render");
            window.resize(1024,720);
            require(renderPreview(QDir(argv[2]).filePath(QString("recovery-%1-compact.png").arg(themeName))),"Compact recovery options did not render");
            require(order->geometry().right()<order->parentWidget()->width(),"Compact recovery order exceeds the page width");
            auto* recoveryPage=qobject_cast<QScrollArea*>(projectTabs->widget(2));
            require(recoveryPage && recoveryPage->horizontalScrollBar()->maximum()==0
                    && recoveryPage->verticalScrollBar()->maximum()==0,"Compact recovery page clips options or guidance");
            limitsToggle->setChecked(true);
            require(renderPreview(QDir(argv[2]).filePath(QString("recovery-%1-limits.png").arg(themeName))),"Analysis limits did not render");
            for(auto* combo:{window.findChild<QComboBox*>("recoveryFormat"),order,flags,groups}) {
                if(combo->height()<combo->sizeHint().height()) std::cerr << combo->objectName().toStdString()
                    << " height=" << combo->height() << " hint=" << combo->sizeHint().height()
                    << " page=" << recoveryPage->widget()->height() << " minimum=" << recoveryPage->widget()->minimumHeight() << '\n';
                require(combo->height()>=combo->sizeHint().height(),"Expanded limits compressed recovery choices below readable height");
            }
            require(recoveryPage->horizontalScrollBar()->maximum()==0 && detailBudget->isEnabled() && groupBudget->isEnabled(),"Analysis limits clipped horizontally or unavailable");
            // ensureWidgetVisible follows a spin box's edit cursor, which can
            // be visible while its frame is partly outside the viewport.
            recoveryPage->verticalScrollBar()->setValue(recoveryPage->verticalScrollBar()->maximum()); settleLayouts();
            require(groupBudget->mapTo(recoveryPage->viewport(),QPoint(0,groupBudget->height())).y()<=recoveryPage->viewport()->height(),"Cannot scroll to group work limit");
            require(renderPreview(QDir(argv[2]).filePath(QString("recovery-%1-limits-scrolled.png").arg(themeName))),"Scrolled limits did not render");
            window.resize(1380,920);
            require(renderPreview(QDir(argv[2]).filePath(QString("recovery-%1-limits-full.png").arg(themeName))),"Full-size limits did not render");
            require(recoveryPage->verticalScrollBar()->maximum()==0,"Full-size expanded limits need unnecessary scrolling");
            limitsToggle->setChecked(false);
            for(auto* action:window.findChildren<QAction*>()) if(action->text().startsWith("Toggle &light")) action->trigger();
        }
        window.resize(originalSize); projectTabs->setCurrentIndex(0);
        workbench::saveJson(QDir(argv[2]).filePath("recovery-checks.json"),{
            {"saved_policy_loaded",true},{"compatible_profile_enabled",true},{"native_rebuild_disabled",true},
            {"incompatible_selection_retained",true},{"native_default_enabled",true},{"unknown_profile_disabled",true},
            {"command_preview_policy",true},{"menu_action_rejected_before_staging",true},
            {"saved_inference_policies_and_limits",true},{"grouping_selects_rebuild_order",true},{"limits_command_preview",true},
            {"older_compiler_inference_disabled",true},{"older_compiler_defaults_available",true},{"older_compiler_menu_rejected",true},
            {"expanded_limits_accessible",true},{"compiler_switch_restores_support",true},
            {"unsupported_inference_creates_no_output_folders",true},{"native_cell_menu_rejected",true},
            {"expanded_choice_text_readable",true},{"full_size_limits_without_scrolling",true},
            {"both_themes",true},{"compact_page_without_clipping",true},{"os_input_or_capture_used",false}});
        workflow->setCurrentIndex(workflow->findData("build"));
        profiles->setCurrentText("alice");
        require(!button->isEnabled(), "Recovery-only profile enabled compilation in the window");
        auto* detail=window.findChild<QSpinBox*>("meshPatchSteps");
        require(workflow && detail && detail->value()==8,"Mesh controls/default missing");
        workflow->setCurrentIndex(workflow->findData("obj")); detail->setValue(5);
        require(button->isEnabled(),"Recovery-only profile did not enable mesh export");
        bool command=false;
        for(auto* edit:window.findChildren<QPlainTextEdit*>())
            command|=edit->toPlainText().contains("-patchsteps 5");
        require(command,"Mesh detail did not reach the command preview");
        QTabWidget* options=nullptr;
        for(auto* tabs:window.findChildren<QTabWidget*>())
            if(tabs->count()>1 && tabs->tabText(1).contains("Quality")) options=tabs;
        require(options,"Project options tabs missing");
        options->setCurrentIndex(1);
        require(renderPreview(QDir(argv[2]).filePath("mesh-options.png")),"Mesh options did not render");
        options->setCurrentIndex(0); workflow->setCurrentIndex(workflow->findData("build"));
        profiles->setCurrentText(project.game);
        require(button->isEnabled(), "Returning to a writable profile did not enable build");
        for(const auto* mode:{"geometry-analyze","geometry-optimize"}) {
            workflow->setCurrentIndex(workflow->findData(mode));
            require(button->isEnabled() && policyAvailable(workflow,mode),"Current compiler did not enable geometry workflows");
            require(preview->toPlainText().contains("-renderer quake3e-gl")
                && preview->toPlainText().contains("-optimize-geometry")
                && !preview->toPlainText().contains("-detail-policy"),"Geometry preview omitted renderer or included recovery options");
        }
        window.resize(1024,720);
        require(renderPreview(QDir(argv[2]).filePath("geometry-compact.png")),"Geometry workflow did not render");
        require(workflow->width()>=workflow->sizeHint().width(),"Geometry workflow text is clipped");
        window.resize(originalSize); workflow->setCurrentIndex(workflow->findData("build"));
        QAction* run = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence(Qt::Key_F5)) run = action;
        require(run, "Run workflow action missing");
        launched = true;
        // Invoke the application action directly in an offscreen widget tree.
        // No mouse/keyboard events or operating-system input are synthesized.
        run->trigger();
        require(queue->jobs().size() == 3, "Window failed to enqueue the full pipeline");
        for(const auto& job:queue->jobs()) for(const auto* option:{"-brush-order","-detail-policy","-group-policy","-detail-max-work","-group-max-work"})
            require(!job.arguments.contains(option),"Window applied recovery settings to compilation");
    });
    QObject::connect(queue, &workbench::JobQueue::idle, &app, [&] {
        if (!launched) return;
        for (const auto& job : queue->jobs())
            require(job.state == "Succeeded", "A window-launched compiler stage failed");
        require(source.open(QIODevice::ReadOnly) && source.readAll() == original, "Window modified source input");
        require(QFileInfo(queue->jobs().back().outputPath).size() > 100, "No BSP produced from window action");
        inspectedSource=queue->jobs().back().outputPath;
        window.findChild<QListWidget*>("navigation")->setCurrentRow(4);
        inspectionPhase=1; inspectFile->setText(inspectedSource); inspectButton->click();
    });
    QTimer::singleShot(30000, &app, [] { require(false, "Window catalog/build action timed out"); });
    ready.start(25);
    return app.exec();
}
