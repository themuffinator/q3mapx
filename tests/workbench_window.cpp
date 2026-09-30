// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "inspection_page.h"
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
#include <QListWidget>
#include <QTableWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QDir>
#include <QTimer>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    require(argc == 3, "Expected project and isolated settings directory");
    require(QDir().mkpath(argv[2]),"Cannot create isolated window test output directory");
    const auto project = workbench::Project::load(argv[1]);
    QFile source(project.source); require(source.open(QIODevice::ReadOnly), "Cannot read fixture source");
    const auto original = source.readAll(); source.close();
    workbench::Window window(argv[2]);
    window.loadProject(argv[1]);
    auto* queue = window.findChild<workbench::JobQueue*>();
    require(queue, "Window has no build queue");
    bool launched = false;
    auto* inspector=window.findChild<workbench::BspInspector*>();
    auto* inspectionPage=window.findChild<workbench::InspectionPage*>();
    auto* inspectFile=window.findChild<QLineEdit*>("inspectionFile");
    auto* inspectButton=window.findChild<QPushButton*>("inspectBsp");
    auto* lumpTable=window.findChild<QTableWidget*>("inspectionLumps");
    require(inspector && inspectionPage && inspectFile && inspectButton && lumpTable,"Inspector controls missing");
    int inspectionPhase=0;
    QString inspectedSource;
    QObject::connect(inspector,&workbench::BspInspector::changed,&app,[&]{
        if(inspector->loading() || inspector->report().isEmpty()) return;
        require(inspector->error().isEmpty(),"Window inspection query failed");
        if(inspectionPhase==1) {
            require(inspector->report()["valid"].toBool() && lumpTable->rowCount()==17,"Valid directory missing from window");
            require(window.renderPreview(QDir(argv[2]).filePath("bsp-inspection.png")),"Inspector did not render");
            const auto fullSize=window.size(); window.resize(1024,720);
            require(window.renderPreview(QDir(argv[2]).filePath("bsp-inspection-compact.png")),"Compact inspector did not render");
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
            std::cout << "Actual window build, inspection, source protection, report export and invalid-input diagnostics passed without input injection\n";
            app.quit();
        }
    });
    QTimer ready;
    QObject::connect(&ready, &QTimer::timeout, &app, [&] {
        if (window.discoveringGames() || launched) return;
        auto* profiles = window.findChild<QComboBox*>("gameProfiles");
        require(profiles && profiles->count() >= 19, "Window did not use the compiler catalog");
        require(profiles->currentText() == project.game, "Catalog replaced the saved project selection");
        auto* button=window.findChild<QPushButton*>("primary");
        require(button, "Run button missing");
        profiles->setCurrentText("alice");
        require(!button->isEnabled(), "Recovery-only profile enabled compilation in the window");
        auto* workflow=window.findChild<QComboBox*>("workflow");
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
        require(window.renderPreview(QDir(argv[2]).filePath("mesh-options.png")),"Mesh options did not render");
        options->setCurrentIndex(0); workflow->setCurrentIndex(workflow->findData("build"));
        profiles->setCurrentText(project.game);
        require(button->isEnabled(), "Returning to a writable profile did not enable build");
        QAction* run = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence(Qt::Key_F5)) run = action;
        require(run, "Run workflow action missing");
        launched = true;
        // Invoke the application action directly in an offscreen widget tree.
        // No mouse/keyboard events or operating-system input are synthesized.
        run->trigger();
        require(queue->jobs().size() == 3, "Window failed to enqueue the full pipeline");
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
