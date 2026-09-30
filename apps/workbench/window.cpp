// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "inspection_page.h"
#include "hardware_page.h"
#include <QtWidgets>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardItemModel>
#include <stdexcept>

namespace workbench {
static QStringList optionLines(const QPlainTextEdit* editor){
    QStringList result;
    for (const auto& line:editor->toPlainText().split('\n')) if (!line.trimmed().isEmpty()) result.append(line.trimmed());
    return result;
}
static QPlainTextEdit* codeView(QWidget* parent=nullptr){
    auto* view=new QPlainTextEdit(parent); view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap); return view;
}
static QScrollArea* scrollable(QWidget* content){
    auto* scroll=new QScrollArea; scroll->setWidget(content); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); return scroll;
}
Window::Window(const QString& stateDirectory):stateDirectory_(stateDirectory),queue_(this),catalog_(this){
    setWindowTitle("q3mapx Workbench"); resize(1380,920); setMinimumSize(1024,720);
    auto* central=new QWidget; auto* outer=new QVBoxLayout(central); outer->setContentsMargins(24,20,24,16); outer->setSpacing(18);
    auto* header=new QHBoxLayout;
    auto* brand=new QLabel("q3map<span style='color:#4fd1bb'>x</span>"); brand->setObjectName("brand");
    auto* heading=new QVBoxLayout; title_=new QLabel("Untitled project"); title_->setObjectName("projectTitle");
    auto* subtitle=new QLabel("MAP COMPILER  /  DESKTOP WORKBENCH"); subtitle->setObjectName("eyebrow");
    heading->addWidget(subtitle); heading->addWidget(title_); header->addWidget(brand); header->addSpacing(24); header->addLayout(heading); header->addStretch();
    run_=new QPushButton("Run workflow  ·  F5"); run_->setObjectName("primary"); run_->setMinimumHeight(42);
    cancel_=new QPushButton("Cancel"); cancel_->setEnabled(false); cancel_->setMinimumHeight(42);
    header->addWidget(cancel_); header->addWidget(run_); outer->addLayout(header);
    auto* body=new QHBoxLayout; body->setSpacing(22);
    navigation_=new QListWidget; navigation_->setObjectName("navigation"); navigation_->setFixedWidth(172);
    navigation_->addItems({"01   Project","02   Build queue","03   History","04   Hardware","05   BSP inspection"});
    navigation_->setSpacing(5); navigation_->setCurrentRow(0); navigation_->setAccessibleName("Workbench pages");
    pages_=new QStackedWidget; pages_->addWidget(configuration()); pages_->addWidget(queuePage()); pages_->addWidget(historyPage());
    hardware_=new HardwarePage; pages_->addWidget(hardware_);
    inspection_=new InspectionPage; pages_->addWidget(inspection_);
    body->addWidget(navigation_); body->addWidget(pages_,1); outer->addLayout(body,1);
    auto* footer=new QHBoxLayout; status_=new QLabel("Ready · configure a project to begin");
    progress_=new QProgressBar; progress_->setFixedWidth(240); progress_->setRange(0,1); progress_->setValue(0); progress_->setTextVisible(false);
    footer->addWidget(status_,1); footer->addWidget(progress_); outer->addLayout(footer); setCentralWidget(central);
    auto* file=menuBar()->addMenu("&Project");
    auto* newAction=file->addAction("&New project",QKeySequence::New,this,[this]{ if(confirmDiscard()) { Project p; p.compiler=compiler_->text(); p.gameRoot=gameRoot_->text(); p.outputRoot=outputRoot_->text(); projectPath_.clear(); setProject(p); } });
    Q_UNUSED(newAction);
    file->addAction("&Open…",QKeySequence::Open,this,[this]{ if(!confirmDiscard()) return; const auto path=QFileDialog::getOpenFileName(this,"Open project",{},"q3mapx projects (*.q3mapx.json *.json)"); if(!path.isEmpty()) { try { loadProject(path); } catch(const std::exception& e){ showError(e.what()); } } });
    file->addAction("&Save",QKeySequence::Save,this,[this]{ saveProject(false); });
    file->addAction("Save &as…",QKeySequence::SaveAs,this,[this]{ saveProject(true); });
    file->addSeparator(); file->addAction("E&xit",QKeySequence::Quit,this,&QWidget::close);
    auto* build=menuBar()->addMenu("&Build");
    build->addAction("&Run workflow",QKeySequence(Qt::Key_F5),this,[this]{ enqueue(true); });
    build->addAction("&Add to queue",QKeySequence("Ctrl+Return"),this,[this]{ enqueue(false); });
    build->addAction("&Cancel active job",QKeySequence("Shift+Escape"),&queue_,&JobQueue::cancel);
    auto* view=menuBar()->addMenu("&View");
    view->addAction("Toggle &light / dark theme",this,[this]{ theme_=theme_=="dark" ? "light" : "dark"; applyTheme(); });
    menuBar()->addMenu("&Help")->addAction("About q3mapx",this,[this]{ QMessageBox::about(this,"q3mapx", "q3mapx Workbench " Q3MAPX_VERSION "\nStandalone map compilation and BSP recovery.\nBased on NetRadiant-custom q3map2.\nGPL-3.0-or-later · Qt 6\nGPU acceleration currently applies to minimaps."); });
    connect(navigation_,&QListWidget::currentRowChanged,pages_,&QStackedWidget::setCurrentIndex);
    connect(run_,&QPushButton::clicked,this,[this]{ enqueue(true); }); connect(cancel_,&QPushButton::clicked,&queue_,&JobQueue::cancel);
    connect(&queue_,&JobQueue::changed,this,&Window::refreshQueue);
    connect(&queue_,&JobQueue::output,this,&Window::appendOutput);
    connect(&queue_,&JobQueue::activity,status_,&QLabel::setText);
    connect(&queue_,&JobQueue::completed,this,[this](int){ recordHistory(); selectJob(); });
    connect(&queue_,&JobQueue::idle,this,[this]{
        int succeeded=0,failed=0,remaining=0;
        for(const auto& job:queue_.jobs()) { succeeded+=job.state=="Succeeded"; failed+=job.state=="Failed" || job.state=="Cancelled"; remaining+=job.state=="Queued"; }
        status_->setText(QString("Queue stopped · %1 succeeded · %2 failed / cancelled · %3 queued").arg(succeeded).arg(failed).arg(remaining)); recordHistory();
    });
    for(auto* edit:{name_,source_,gameRoot_,outputRoot_,compiler_,mod_}) connect(edit,&QLineEdit::textChanged,this,&Window::updatePreview);
    for(auto* combo:{game_,quality_,backend_,format_,brushOrder_,detailPolicy_,workflow_}) connect(combo,&QComboBox::currentTextChanged,this,&Window::updatePreview);
    connect(groupPolicy_,&QComboBox::currentTextChanged,this,[this]{
        if(populating_) return;
        if(groupPolicy_->currentData()=="surfaces") {
            const QSignalBlocker blocker(brushOrder_);
            brushOrder_->setCurrentIndex(brushOrder_->findData("rebuild"));
        }
        updatePreview();
    });
    for(auto* spin:{workers_,gpu_,size_,samples_,patchSteps_,detailWork_,groupWork_}) connect(spin,&QSpinBox::valueChanged,this,&Window::updatePreview);
    for(auto* edit:{bspOptions_,visOptions_,lightOptions_}) connect(edit,&QPlainTextEdit::textChanged,this,&Window::updatePreview);
    connect(&catalog_,&GameCatalog::changed,this,[this]{
        if(!catalog_.loading() && !catalog_.profiles().isEmpty()) {
            const QString selected=game_->currentText();
            const QSignalBlocker blocker(game_);
            game_->clear();
            for(const auto& profile:catalog_.profiles()) {
                game_->addItem(profile.id);
                game_->setItemData(game_->count()-1,profile.title,Qt::ToolTipRole);
            }
            game_->setCurrentText(selected);
        }
        refreshGameHint();
    });
    connect(compiler_,&QLineEdit::textChanged,this,[this](const QString& path){
        hardware_->setCompiler(path);
        refreshGameHint();
        QTimer::singleShot(250,this,[this,path]{
            if(compiler_->text()==path && catalogCompiler_!=path) refreshGames();
        });
    });
    Project initial;
#ifdef Q_OS_WIN
    initial.compiler=QCoreApplication::applicationDirPath()+"/q3mapx.exe";
#else
    initial.compiler=QCoreApplication::applicationDirPath()+"/q3mapx";
#endif
    initial.outputRoot=QDir::current().absoluteFilePath("q3mapx-output");
    setProject(initial);
    QFile settings(QDir(stateDirectory_).filePath("ui.json"));
    if(settings.open(QIODevice::ReadOnly)) {
        const auto o=QJsonDocument::fromJson(settings.read(1024*1024)).object();
        theme_=o.value("theme").toString("dark"); restoreGeometry(QByteArray::fromBase64(o.value("geometry").toString().toLatin1()));
    }
    QFile history(QDir(stateDirectory_).filePath("history.json"));
    if(history.open(QIODevice::ReadOnly)) history_=QJsonDocument::fromJson(history.read(4*1024*1024)).object().value("runs").toArray();
    refreshHistory(); applyTheme(); dirty_=false;
}
QWidget* Window::pathField(QLineEdit*& edit,const QString& placeholder,int kind){
    auto* widget=new QWidget; auto* row=new QHBoxLayout(widget); row->setContentsMargins(0,0,0,0);
    edit=new QLineEdit; edit->setPlaceholderText(placeholder); edit->setAccessibleName(placeholder); widget->setFocusProxy(edit); widget->setFocusPolicy(Qt::StrongFocus);
    if(kind==2) edit->setObjectName("compilerPath");
    auto* browse=new QPushButton("Browse…"); row->addWidget(edit,1); row->addWidget(browse);
    connect(browse,&QPushButton::clicked,this,[this,edit,kind]{
        const QString path=kind==1 ? QFileDialog::getExistingDirectory(this,"Choose folder",edit->text())
            : QFileDialog::getOpenFileName(this,kind==2 ? "Choose compiler" : "Choose source",edit->text(),kind==2 ? "Executables (*)" : "Map sources (*.map *.bsp)");
        if(!path.isEmpty()) {
            edit->setText(QDir::toNativeSeparators(path));
            if(kind==0) {
                if(name_->text()=="Untitled project") name_->setText(QFileInfo(path).completeBaseName());
                if(gameRoot_->text().isEmpty()) { QDir root=QFileInfo(path).absoluteDir(); root.cdUp(); root.cdUp(); gameRoot_->setText(root.absolutePath()); }
                workflow_->setCurrentIndex(QFileInfo(path).suffix().compare("bsp",Qt::CaseInsensitive)==0 ? 5 : 0);
            }
        }
    }); return widget;
}
QWidget* Window::configuration(){
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0);
    auto* intro=new QLabel("Project settings"); intro->setObjectName("pageTitle"); layout->addWidget(intro);
    auto* description=new QLabel("Configure the source and assets, inspect the command, then run or queue a workflow."); description->setObjectName("muted"); layout->addWidget(description);
    auto* tabs=new QTabWidget; tabs->setObjectName("projectOptions");
    auto* general=new QWidget; auto* form=new QFormLayout(general); form->setContentsMargins(20,20,20,20); form->setVerticalSpacing(14); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    name_=new QLineEdit; form->addRow("Project &name",name_);
    form->addRow("&Source MAP / BSP",pathField(source_,"Source map or BSP",0));
    form->addRow("Game &root",pathField(gameRoot_,"Game root containing baseq3 or the game asset folder",1));
    form->addRow("&Output folder",pathField(outputRoot_,"Each run gets a separate folder",1));
    game_=new QComboBox; game_->setObjectName("gameProfiles"); game_->setEditable(true);
    auto* profileRow=new QWidget; auto* profileLayout=new QHBoxLayout(profileRow); profileLayout->setContentsMargins(0,0,0,0);
    auto* refresh=new QPushButton("Refresh"); refresh->setAccessibleName("Refresh game profiles from compiler");
    connect(refresh,&QPushButton::clicked,this,&Window::refreshGames);
    profileLayout->addWidget(game_,1); profileLayout->addWidget(refresh);
    auto* profileLabel=new QLabel("&Game profile"); profileLabel->setBuddy(game_); form->addRow(profileLabel,profileRow);
    gameHint_=new QLabel; gameHint_->setTextFormat(Qt::PlainText); gameHint_->setWordWrap(true); gameHint_->setObjectName("muted");
    form->addRow(gameHint_);
    mod_=new QLineEdit; mod_->setPlaceholderText("Optional mod directory, e.g. mymod"); form->addRow("&Mod",mod_);
    form->addRow("&Compiler",pathField(compiler_,"q3mapx executable",2));
    auto* notice=new QLabel("Source files stay untouched. The workbench stages inputs and writes logs, BSPs and reports into a new run folder."); notice->setWordWrap(true); notice->setObjectName("notice"); form->addRow(notice);
    tabs->addTab(scrollable(general),"Source && paths");
    auto* tuning=new QWidget; auto* options=new QFormLayout(tuning); options->setContentsMargins(20,20,20,20); options->setVerticalSpacing(12);
    quality_=new QComboBox; quality_->addItem("Draft · quick visibility and 1 light sample","draft"); quality_->addItem("Balanced · full visibility and 2 samples","balanced"); quality_->addItem("Production · 4 samples and 2 bounces","production");
    options->addRow("&Quality",quality_);
    workers_=new QSpinBox; workers_->setRange(0,1024); workers_->setSpecialValueText("Automatic"); options->addRow("CPU &workers",workers_);
    reproducibleVis_=new QCheckBox("&Reproducible visibility across worker counts");
    reproducibleVis_->setToolTip("Use fixed VIS publication batches. May take longer than unrestricted scheduling.");
    options->addRow(reproducibleVis_); connect(reproducibleVis_,&QCheckBox::toggled,this,&Window::updatePreview);
    backend_=new QComboBox; backend_->addItems({"auto","cpu","gpu","reference"}); options->addRow("Minimap &backend",backend_);
    gpu_=new QSpinBox; gpu_->setRange(-1,1023); gpu_->setSpecialValueText("Automatic"); options->addRow("GPU &device index",gpu_);
    size_=new QSpinBox; size_->setRange(1,8192); size_->setSingleStep(256); options->addRow("Minimap &size",size_);
    samples_=new QSpinBox; samples_->setRange(1,256); options->addRow("Minimap sa&mples",samples_);
    patchSteps_=new QSpinBox; patchSteps_->setObjectName("meshPatchSteps"); patchSteps_->setRange(1,32); options->addRow("Mesh curve &detail",patchSteps_);
    patchSteps_->setToolTip("Samples along each curve span for OBJ/ASE export. Higher values create smoother, larger meshes. Default: 8.");
    auto* note=new QLabel("GPU selection here affects minimaps. Lighting defaults to CPU workers. Recovery includes a JSON report of retained and approximated data."); note->setWordWrap(true); note->setObjectName("notice"); options->addRow(note); tabs->addTab(scrollable(tuning),"Quality && compute");
    auto* recovery=new QWidget; auto* recoveryOptions=new QVBoxLayout(recovery);
    recoveryOptions->setContentsMargins(16,10,16,10); recoveryOptions->setSpacing(8);
    // Propagate newly visible limits to the scroll area's content minimum, so
    // Qt scrolls instead of squeezing the dropdown text out of its controls.
    recoveryOptions->setSizeConstraint(QLayout::SetMinimumSize);
    auto* choices=new QGridLayout; choices->setHorizontalSpacing(18); choices->setVerticalSpacing(5);
    choices->setColumnStretch(0,1); choices->setColumnStretch(1,1);
    const auto choice=[&](const QString& text,QComboBox* combo,int row,int column){
        combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        auto* label=new QLabel(text); label->setBuddy(combo);
        choices->addWidget(label,row,column); choices->addWidget(combo,row+1,column);
    };
    format_=new QComboBox; format_->setObjectName("recoveryFormat");
    format_->addItem("Valve 220 · recommended","map_220"); format_->addItem("Brush primitives","map_bp"); format_->addItem("Classic Quake coordinates","map");
    choice("Recovery &format",format_,0,0);
    brushOrder_=new QComboBox; brushOrder_->setObjectName("recoveryBrushOrder"); brushOrder_->setAccessibleName("Recovery brush order");
    brushOrder_->addItem("BSP order · default","bsp"); brushOrder_->addItem("Rebuild order","rebuild");
    brushOrder_->setToolTip("Rebuild order can reduce partition changes when recompiling the recovered MAP with matching game and shader assets.");
    choice("Brush &order",brushOrder_,0,1);
    detailPolicy_=new QComboBox; detailPolicy_->setObjectName("recoveryDetailPolicy"); detailPolicy_->setAccessibleName("Recovery detail flags");
    detailPolicy_->addItem("Legacy detail flags · default","legacy"); detailPolicy_->addItem("Infer from brush interiors","cells");
    detailPolicy_->setToolTip("Use bounded brush/tree intersections and current material semantics to propose detail flags. Ambiguous cases retain the baseline.");
    choice("&Detail flags",detailPolicy_,2,0);
    groupPolicy_=new QComboBox; groupPolicy_->setObjectName("recoveryGroupPolicy"); groupPolicy_->setAccessibleName("Recovery brush groups");
    groupPolicy_->addItem("Flat world geometry · default","none"); groupPolicy_->addItem("Infer func_group assemblies","surfaces");
    groupPolicy_->setToolTip("Shared rendered surfaces propose brush assemblies independently of detail flags. Requires Rebuild order; incompatible proposals stay flat.");
    choice("Brush &groups",groupPolicy_,2,1);
    recoveryOptions->addLayout(choices);
    recoveryHint_=new QLabel; recoveryHint_->setObjectName("recoveryOrderHint"); recoveryHint_->setTextFormat(Qt::PlainText); recoveryHint_->setWordWrap(true);
    recoveryOptions->addWidget(recoveryHint_);
    auto* limitsToggle=new QToolButton; limitsToggle->setObjectName("recoveryLimitsToggle"); limitsToggle->setText("Analysis work limits");
    limitsToggle->setCheckable(true); limitsToggle->setArrowType(Qt::RightArrow); limitsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* recoveryFooter=new QHBoxLayout; recoveryFooter->addWidget(limitsToggle);
    auto* recoveryNote=new QLabel("Review inferred choices in the recovery report.");
    recoveryNote->setObjectName("recoveryLimitations"); recoveryNote->setWordWrap(true);
    recoveryFooter->addWidget(recoveryNote,1); recoveryOptions->addLayout(recoveryFooter);
    auto* limits=new QWidget; limits->setObjectName("recoveryLimits"); auto* limitsForm=new QFormLayout(limits); limitsForm->setContentsMargins(0,0,0,0);
    detailWork_=new QSpinBox; detailWork_->setObjectName("recoveryDetailWorkLimit");
    groupWork_=new QSpinBox; groupWork_->setObjectName("recoveryGroupWorkLimit");
    for(auto* spin:{detailWork_,groupWork_}) {
        spin->setRange(1,100'000'000); spin->setSingleStep(1'000'000); spin->setGroupSeparatorShown(true);
        spin->setToolTip("Maximum additional analysis work units, not elapsed time. Exhaustion fails recovery and preserves previous outputs. Default: 50,000,000.");
    }
    limitsForm->addRow("Detail work limit",detailWork_); limitsForm->addRow("Group work limit",groupWork_);
    recoveryOptions->addWidget(limits); limits->hide();
    connect(limitsToggle,&QToolButton::toggled,this,[limits,limitsToggle](bool expanded){
        limits->setVisible(expanded); limitsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    recoveryOptions->addStretch();
    tabs->addTab(scrollable(recovery),"Recovery");
    auto* advanced=new QWidget; auto* advancedLayout=new QVBoxLayout(advanced);
    auto* tip=new QLabel("Additional arguments · one argument per line. Values containing spaces stay a single argument; do not add shell quotes."); tip->setWordWrap(true); advancedLayout->addWidget(tip);
    auto* extra=new QTabWidget;
    bspOptions_=new QPlainTextEdit; visOptions_=new QPlainTextEdit; lightOptions_=new QPlainTextEdit;
    extra->addTab(bspOptions_,"BSP"); extra->addTab(visOptions_,"VIS"); extra->addTab(lightOptions_,"LIGHT"); advancedLayout->addWidget(extra); tabs->addTab(advanced,"Advanced arguments"); layout->addWidget(tabs,1);
    auto* commandHeader=new QHBoxLayout; auto* commandLabel=new QLabel("Workflow & command preview"); commandLabel->setObjectName("sectionTitle"); commandHeader->addWidget(commandLabel); commandHeader->addStretch();
    workflow_=new QComboBox; workflow_->addItem("Full build · BSP → VIS → LIGHT","build"); workflow_->addItem("BSP only","bsp"); workflow_->addItem("Visibility","vis"); workflow_->addItem("Lighting","light"); workflow_->addItem("Minimap","minimap"); workflow_->addItem("Decompile BSP","decompile");
    workflow_->addItem("Export OBJ mesh","obj"); workflow_->addItem("Export ASE mesh","ase");
    workflow_->addItem("Analyze geometry · Quake3e GL","geometry-analyze"); workflow_->addItem("Optimize geometry · Quake3e GL","geometry-optimize");
    for(const auto* value:{"geometry-analyze","geometry-optimize"})
        workflow_->setItemData(workflow_->findData(value),"For Quake3e OpenGL: final baked Quake III BSP and matching shader assets required. Only eligible horizontal surfaces with authored nomarks and nodlight are reduced. Outputs go into a new run folder.",Qt::ToolTipRole);
    workflow_->setObjectName("workflow");
    workflow_->setAccessibleName("Workflow"); commandHeader->addWidget(workflow_);
    auto* enqueueButton=new QPushButton("Add to queue"); connect(enqueueButton,&QPushButton::clicked,this,[this]{ enqueue(false); }); commandHeader->addWidget(enqueueButton); layout->addLayout(commandHeader);
    preview_=codeView(); preview_->setObjectName("commandPreview"); preview_->setMaximumHeight(135); preview_->setAccessibleName("Command preview"); layout->addWidget(preview_); return page;
}

QWidget* Window::queuePage(){
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0);
    auto* row=new QHBoxLayout; auto* title=new QLabel("Build queue"); title->setObjectName("pageTitle"); row->addWidget(title); row->addStretch();
    auto* start=new QPushButton("Start queue"); connect(start,&QPushButton::clicked,&queue_,&JobQueue::start); row->addWidget(start);
    auto* clear=new QPushButton("Clear finished"); connect(clear,&QPushButton::clicked,this,[this]{ if(!queue_.running()) { queue_.clearFinished(); logs_.clear(); diagnostics_->clear(); logView_->clear(); } }); row->addWidget(clear);
    auto* exportReport=new QPushButton("Export report…"); connect(exportReport,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,"Export queue report",{},"JSON (*.json)"); if(!path.isEmpty()) try { saveJson(path,queue_.report()); } catch(const std::exception& e){ showError(e.what()); }
    }); row->addWidget(exportReport); layout->addLayout(row);
    jobs_=new QTableWidget(0,4); jobs_->setHorizontalHeaderLabels({"Stage","State","Elapsed","Run folder"});
    jobs_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch); jobs_->verticalHeader()->hide(); jobs_->setSelectionBehavior(QAbstractItemView::SelectRows);
    jobs_->setSelectionMode(QAbstractItemView::SingleSelection); jobs_->setEditTriggers(QAbstractItemView::NoEditTriggers); jobs_->setMaximumHeight(235); jobs_->setMinimumHeight(140);
    connect(jobs_,&QTableWidget::itemSelectionChanged,this,&Window::selectJob); layout->addWidget(jobs_);
    auto* tabs=new QTabWidget;
    auto* logs=new QWidget; auto* logsLayout=new QVBoxLayout(logs);
    auto* searchRow=new QHBoxLayout; search_=new QLineEdit; search_->setPlaceholderText("Find in selected log…"); search_->setAccessibleName("Search log");
    auto* find=new QPushButton("Find next"); auto* open=new QPushButton("Open output folder"); searchRow->addWidget(search_,1); searchRow->addWidget(find); searchRow->addWidget(open); logsLayout->addLayout(searchRow);
    logView_=codeView(); logView_->document()->setMaximumBlockCount(20000); logsLayout->addWidget(logView_);
    const auto findText=[this]{ if(!logView_->find(search_->text())) { auto cursor=logView_->textCursor(); cursor.movePosition(QTextCursor::Start); logView_->setTextCursor(cursor); logView_->find(search_->text()); } };
    connect(find,&QPushButton::clicked,this,findText); connect(search_,&QLineEdit::returnPressed,this,findText);
    connect(open,&QPushButton::clicked,this,[this]{ const int i=jobs_->currentRow(); if(i>=0 && i<queue_.jobs().size()) QDesktopServices::openUrl(QUrl::fromLocalFile(queue_.jobs()[i].directory)); });
    tabs->addTab(logs,"Live log");
    diagnostics_=new QTreeWidget; diagnostics_->setHeaderLabels({"Severity","Stage","Message"}); diagnostics_->setRootIsDecorated(false); diagnostics_->header()->setSectionResizeMode(2,QHeaderView::Stretch);
    connect(diagnostics_,&QTreeWidget::itemActivated,this,[this,tabs](QTreeWidgetItem* item,int){ jobs_->selectRow(item->data(0,Qt::UserRole).toInt()); search_->setText(item->text(2)); tabs->setCurrentIndex(0); logView_->find(item->text(2)); }); tabs->addTab(diagnostics_,"Diagnostics");
    auto* reportsPage=new QWidget; auto* reportLayout=new QVBoxLayout(reportsPage); reports_=new QComboBox; reports_->setAccessibleName("Generated report"); reportView_=codeView(); reportLayout->addWidget(reports_); reportLayout->addWidget(reportView_);
    auto* reportLimit=new QLabel("Preview shows up to 5 MiB. Open the run folder for complete report files.");
    reportLimit->setWordWrap(true); reportLayout->addWidget(reportLimit);
    connect(reports_,&QComboBox::currentIndexChanged,this,[this]{ QFile file(reports_->currentData().toString()); reportView_->clear(); if(file.open(QIODevice::ReadOnly)) reportView_->setPlainText(QString::fromUtf8(file.read(5*1024*1024))); }); tabs->addTab(reportsPage,"Reports && profiles");
    layout->addWidget(tabs,1); return page;
}
QWidget* Window::historyPage(){
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0);
    auto* title=new QLabel("Build history"); title->setObjectName("pageTitle"); layout->addWidget(title);
    auto* note=new QLabel("Recent runs retain their project snapshot, logs and output files. Double-click a run to open its folder."); note->setWordWrap(true); note->setObjectName("muted"); layout->addWidget(note);
    historyView_=new QTableWidget(0,4); historyView_->setHorizontalHeaderLabels({"Finished","Result","Stages","Output folder"});
    historyView_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch); historyView_->verticalHeader()->hide(); historyView_->setEditTriggers(QAbstractItemView::NoEditTriggers); historyView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(historyView_,&QTableWidget::cellDoubleClicked,this,[this](int row,int){ const auto path=historyView_->item(row,3)->text(); QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }); layout->addWidget(historyView_,1);
    auto* reload=new QPushButton("Load selected run's project"); connect(reload,&QPushButton::clicked,this,[this]{ const int row=historyView_->currentRow(); if(row<0 || !confirmDiscard()) return; try { loadProject(QDir(historyView_->item(row,3)->text()).filePath("project.q3mapx.json")); navigation_->setCurrentRow(0); } catch(const std::exception& e){ showError(e.what()); } }); layout->addWidget(reload,0,Qt::AlignLeft); return page;
}
Project Window::project() const {
    Project p; p.name=name_->text(); p.source=QDir::fromNativeSeparators(source_->text()); p.gameRoot=QDir::fromNativeSeparators(gameRoot_->text());
    p.outputRoot=QDir::fromNativeSeparators(outputRoot_->text()); p.compiler=QDir::fromNativeSeparators(compiler_->text());
    p.game=game_->currentText(); p.mod=mod_->text(); p.quality=quality_->currentData().toString(); p.backend=backend_->currentText(); p.mapFormat=format_->currentData().toString();
    p.brushOrder=brushOrder_->currentData().toString();
    p.detailPolicy=detailPolicy_->currentData().toString(); p.groupPolicy=groupPolicy_->currentData().toString();
    p.detailWorkLimit=detailWork_->value(); p.groupWorkLimit=groupWork_->value();
    p.workers=workers_->value(); p.gpuDevice=gpu_->value(); p.minimapSize=size_->value(); p.minimapSamples=samples_->value();
    p.meshPatchSteps=patchSteps_->value();
    p.reproducibleVis=reproducibleVis_->isChecked();
    p.bspOptions=optionLines(bspOptions_); p.visOptions=optionLines(visOptions_); p.lightOptions=optionLines(lightOptions_); return p;
}
void Window::setProject(const Project& p){
    populating_=true;
    name_->setText(p.name); source_->setText(QDir::toNativeSeparators(p.source)); gameRoot_->setText(QDir::toNativeSeparators(p.gameRoot));
    outputRoot_->setText(QDir::toNativeSeparators(p.outputRoot)); compiler_->setText(QDir::toNativeSeparators(p.compiler));
    game_->setCurrentText(p.game); mod_->setText(p.mod); quality_->setCurrentIndex(quality_->findData(p.quality)); backend_->setCurrentText(p.backend); format_->setCurrentIndex(format_->findData(p.mapFormat));
    brushOrder_->setCurrentIndex(brushOrder_->findData(p.brushOrder));
    detailPolicy_->setCurrentIndex(detailPolicy_->findData(p.detailPolicy)); groupPolicy_->setCurrentIndex(groupPolicy_->findData(p.groupPolicy));
    detailWork_->setValue(p.detailWorkLimit); groupWork_->setValue(p.groupWorkLimit);
    workers_->setValue(p.workers); gpu_->setValue(p.gpuDevice); size_->setValue(p.minimapSize); samples_->setValue(p.minimapSamples);
    patchSteps_->setValue(p.meshPatchSteps);
    reproducibleVis_->setChecked(p.reproducibleVis);
    bspOptions_->setPlainText(p.bspOptions.join('\n')); visOptions_->setPlainText(p.visOptions.join('\n')); lightOptions_->setPlainText(p.lightOptions.join('\n'));
    workflow_->setCurrentIndex(QFileInfo(p.source).suffix().compare("bsp",Qt::CaseInsensitive)==0 ? 5 : 0);
    populating_=false; updatePreview(); dirty_=false; setWindowModified(false); refreshGames();
}
void Window::loadProject(const QString& path){ auto p=Project::load(path); projectPath_=QFileInfo(path).absoluteFilePath(); setProject(p); }
void Window::updatePreview(){
    if(populating_) return;
    dirty_=true; setWindowModified(true); const auto p=project(); title_->setText(p.name.isEmpty() ? "Untitled project" : p.name);
    setWindowTitle((p.name.isEmpty() ? "Untitled project" : p.name)+"[*] — q3mapx Workbench");
    const auto commands=buildPlan(p,workflow_->currentData().toString(),QDir(p.outputRoot).filePath("<new-run>"));
    QStringList lines;
    if(workflow_->currentData().toString().startsWith("geometry-"))
        lines << "For Quake3e OpenGL: use a final baked BSP and matching shader assets.\nOnly eligible horizontal surfaces already disabling marks and dynamic lights can be reduced.";
    for(const auto& job:commands) lines << job.label+"\n"+displayCommand(job);
    preview_->setPlainText(lines.join("\n\n"));
    inspection_->setContext(p.compiler,QDir::toNativeSeparators(p.source),p.game);
    refreshGameHint();
}
void Window::saveProject(bool saveAs){
    QString path=projectPath_;
    if(saveAs || path.isEmpty()) path=QFileDialog::getSaveFileName(this,"Save project",path.isEmpty() ? "project.q3mapx.json" : path,"q3mapx projects (*.q3mapx.json)");
    if(path.isEmpty()) return;
    try { project().save(path); projectPath_=path; dirty_=false; setWindowModified(false); status_->setText("Project saved · "+path); } catch(const std::exception& e){ showError(e.what()); }
}
void Window::enqueue(bool start){
    try {
        const auto p=project(); const QString workflow=workflow_->currentData().toString();
        if(catalog_.loading() || catalogCompiler_!=compiler_->text()) throw std::runtime_error("Wait for the compiler's game profiles to finish loading.");
        const auto* profile=catalog_.find(p.game);
        if(workflow.startsWith("geometry-") && (!profile || !profile->workflows.contains(workflow)))
            throw std::runtime_error("Geometry analysis and optimization require advertised support from the selected compiler and game profile.");
        if(workflow=="decompile") {
            const auto error=recoverySupportError(profile,p.brushOrder,p.detailPolicy,p.groupPolicy);
            if(!error.isEmpty()) throw std::runtime_error(error.toStdString());
        }
        if(!catalog_.profiles().isEmpty()) {
            if(!profile) throw std::runtime_error("Select a game profile supported by this compiler.");
            if(!profile->workflows.contains(workflow)) throw std::runtime_error("This game profile does not support the selected workflow.");
        }
        const auto directory=prepareRun(p,workflow); const int first=queue_.jobs().size();
        queue_.enqueue(buildPlan(p,workflow,directory)); navigation_->setCurrentRow(1); jobs_->selectRow(first);
        if(start) queue_.start(); else status_->setText("Workflow added to queue");
    } catch(const std::exception& e){ showError(e.what()); }
}
void Window::refreshQueue(){
    const QSignalBlocker blocker(jobs_); const int selected=jobs_->currentRow(); const auto& jobs=queue_.jobs(); jobs_->setRowCount(jobs.size());
    int finished=0;
    for(int i=0;i<jobs.size();++i) {
        const auto& j=jobs[i]; const QStringList cells{j.label,j.state,QString::number(j.elapsedMs/1000.0,'f',2)+" s",QFileInfo(j.directory).fileName()};
        for(int c=0;c<cells.size();++c) { auto* item=new QTableWidgetItem(cells[c]); item->setToolTip(c==3 ? j.directory : j.error); jobs_->setItem(i,c,item); }
        if(j.state=="Succeeded") jobs_->item(i,1)->setForeground(QColor("#37b99c"));
        if(j.state=="Failed" || j.state=="Cancelled") jobs_->item(i,1)->setForeground(QColor("#ec877f"));
        if(j.state!="Queued" && j.state!="Running") ++finished;
    }
    if(selected>=0 && selected<jobs.size()) jobs_->selectRow(selected);
    else if(queue_.activeIndex()>=0) jobs_->selectRow(queue_.activeIndex());
    cancel_->setEnabled(queue_.running()); progress_->setRange(0,std::max(1,int(jobs.size()))); progress_->setValue(finished);
    progress_->setToolTip(QString("%1 of %2 stages complete").arg(finished).arg(jobs.size()));
}
void Window::selectJob(){
    const int index=jobs_->currentRow(); if(index<0 || index>=queue_.jobs().size()) return;
    logView_->setPlainText(logs_.value(index)); auto cursor=logView_->textCursor(); cursor.movePosition(QTextCursor::End); logView_->setTextCursor(cursor);
    const auto& job=queue_.jobs()[index]; reports_->clear();
    for(const auto& file:QDir(job.directory).entryList({"*.json"},QDir::Files,QDir::Name)) reports_->addItem(file,QDir(job.directory).filePath(file));
    if(!job.error.isEmpty()) status_->setText(job.error);
}
void Window::appendOutput(int index,const QString& text){
    auto& log=logs_[index]; log+=text; if(log.size()>2*1024*1024) log=log.right(2*1024*1024);
    if(jobs_->currentRow()==index) { auto cursor=logView_->textCursor(); cursor.movePosition(QTextCursor::End); cursor.insertText(text); logView_->setTextCursor(cursor); logView_->ensureCursorVisible(); }
    for(const auto& line:text.split('\n')) {
        const auto cleaned=line.trimmed(); const bool error=cleaned.contains("ERROR",Qt::CaseInsensitive);
        if(error || cleaned.contains("WARNING",Qt::CaseInsensitive)) {
            auto* item=new QTreeWidgetItem(diagnostics_,{error ? "Error" : "Warning",queue_.jobs()[index].label,cleaned});
            item->setData(0,Qt::UserRole,index); item->setForeground(0,QColor(error ? "#ec877f" : "#d7b56d"));
        }
    }
}
void Window::recordHistory(){
    QStringList groups;
    for(const auto& job:queue_.jobs()) if(!groups.contains(job.group)) groups.append(job.group);
    for(const auto& group:groups) {
        if(recordedGroups_.contains(group)) continue;
        QJsonArray stages; bool pending=false,failed=false;
        for(const auto& job:queue_.jobs()) if(job.group==group) {
            stages.append(job.toJson()); pending|=job.state=="Queued" || job.state=="Running"; failed|=job.state!="Succeeded";
        }
        if(pending) continue;
        QJsonObject entry{{"finished",QDateTime::currentDateTime().toString(Qt::ISODate)},{"state",failed ? "Incomplete" : "Succeeded"},{"directory",group},{"stages",stages}};
        history_.prepend(entry); recordedGroups_.append(group);
    }
    while(history_.size()>100) history_.removeLast();
    if(!QDir().mkpath(stateDirectory_)) { status_->setText("Cannot create settings folder"); return; }
    try { saveJson(QDir(stateDirectory_).filePath("history.json"),{{"schema_version",1},{"runs",history_}}); } catch(const std::exception& e){ status_->setText(e.what()); }
    refreshHistory();
}
void Window::refreshHistory(){
    historyView_->setRowCount(history_.size());
    for(int i=0;i<history_.size();++i) {
        const auto o=history_[i].toObject(); const QStringList values{o.value("finished").toString(),o.value("state").toString(),QString::number(o.value("stages").toArray().size()),o.value("directory").toString()};
        for(int c=0;c<values.size();++c) historyView_->setItem(i,c,new QTableWidgetItem(values[c]));
    }
}
void Window::refreshGames(){
    catalogCompiler_=compiler_->text();
    catalog_.refresh(catalogCompiler_);
}
void Window::refreshGameHint(){
    const bool current=!catalog_.loading() && catalogCompiler_==compiler_->text();
    const auto* profile=current ? catalog_.find(game_->currentText()) : nullptr;
    const bool canRebuild=profile && profile->supportsRebuildOrder();
    const bool canDetail=profile && profile->supportsCellDetail(), canGroup=profile && profile->supportsSurfaceGroups();
    const bool grouped=groupPolicy_->currentData()=="surfaces", cells=detailPolicy_->currentData()=="cells";
    const auto enable=[](QComboBox* combo,const char* value,bool available){
        if(auto* model=qobject_cast<QStandardItemModel*>(combo->model())) model->item(combo->findData(value))->setEnabled(available);
    };
    for(const auto* value:{"geometry-analyze","geometry-optimize"})
        enable(workflow_,value,profile && profile->workflows.contains(value));
    enable(brushOrder_,"rebuild",canRebuild); enable(brushOrder_,"bsp",!grouped);
    enable(detailPolicy_,"cells",canDetail); enable(groupPolicy_,"surfaces",canGroup);
    detailWork_->setEnabled(cells && canDetail); groupWork_->setEnabled(grouped && canGroup);
    const auto recoveryError=recoverySupportError(profile,brushOrder_->currentData().toString(),detailPolicy_->currentData().toString(),groupPolicy_->currentData().toString());
    const bool recoveryAllowed=workflow_->currentData()!="decompile" || recoveryError.isEmpty();
    if(!recoveryError.isEmpty()) recoveryHint_->setText(recoveryError);
    else if(grouped) recoveryHint_->setText("Groups require Rebuild order. Shared surfaces suggest assemblies; detail flags are evaluated separately.");
    else if(cells) recoveryHint_->setText("Brush interiors and materials guide detail proposals. Uncertain cases retain legacy flags; rebuilt visibility is not guaranteed.");
    else if(!profile) recoveryHint_->setText("Inference becomes available when the compiler confirms support for the selected profile.");
    else if(!profile->nativeWrite) recoveryHint_->setText("This profile supports recovery only. Select BSP order to decompile; native BSP rebuilding is unavailable.");
    else if(!canRebuild) recoveryHint_->setText("The selected compiler does not advertise rebuild-order recovery. Update the compiler or select BSP order.");
    else if(brushOrder_->currentData()=="rebuild") recoveryHint_->setText("Use matching game assets and compiler settings. Rebuild order can reduce partition changes; identical visibility is not guaranteed.");
    else recoveryHint_->setText("BSP order keeps the existing export sequence. Choose Rebuild order when comparing recompilation against the source BSP.");
    if(!current) {
        gameHint_->setText("Loading game profiles from the selected compiler…"); run_->setEnabled(false); return;
    }
    if(profile) {
        gameHint_->setText(QString("%1 · %2 %3\nAssets: %4/%5 · Workflows: %6")
            .arg(profile->title,profile->bspIdent).arg(profile->bspVersion)
            .arg(profile->baseDirectory,profile->shaderDirectory,profile->workflows.join(", ")));
        run_->setEnabled(profile->workflows.contains(workflow_->currentData().toString()) && recoveryAllowed);
    }
    else if(!catalog_.profiles().isEmpty()) {
        gameHint_->setText("This compiler does not recognize the selected game profile."); run_->setEnabled(false);
    }
    else {
        gameHint_->setText(catalog_.error()+". Enter a legacy profile manually or choose another compiler.");
        run_->setEnabled(recoveryAllowed && !workflow_->currentData().toString().startsWith("geometry-"));
    }
}
void Window::applyTheme(){
    const bool dark=theme_!="light";
    const QString bg=dark ? "#141a21" : "#f2f5f7", panel=dark ? "#1c2530" : "#ffffff", text=dark ? "#e3ebf2" : "#202e3c", muted=dark ? "#9aaec1" : "#506377", border=dark ? "#314151" : "#cad5df";
    setStyleSheet(QString(R"(
        QMainWindow,QWidget { background:%1; color:%3; font-family:'Segoe UI','Noto Sans'; font-size:10pt; }
        QMenuBar,QMenu { background:%2; } QMenu::item:selected { background:#276b64; color:white; }
        QLabel#brand { font-size:28pt; font-weight:700; } QLabel#eyebrow { color:%4; font-size:8pt; letter-spacing:2px; }
        QLabel#projectTitle { font-size:15pt; font-weight:600; } QLabel#pageTitle { font-size:21pt; font-weight:600; margin-bottom:6px; }
        QLabel#muted { color:%4; margin-bottom:12px; } QLabel#sectionTitle { font-size:11pt; font-weight:600; margin:8px 0; }
        QLabel#notice { background:%2; border-left:3px solid #37b99c; padding:12px; color:%4; }
        QLabel#recoveryLimitations { color:%4; padding-left:12px; }
        QLineEdit,QSpinBox,QComboBox,QPlainTextEdit,QTableWidget,QTreeWidget { background:%2; border:1px solid %5; border-radius:5px; padding:7px; selection-background-color:#276b64; selection-color:white; }
        QPlainTextEdit { padding:10px; font-family:'Consolas','Liberation Mono',monospace; } QLineEdit:focus,QSpinBox:focus,QComboBox:focus { border:1px solid #4fd1bb; }
        QPushButton { background:%2; border:1px solid %5; border-radius:5px; padding:8px 14px; font-weight:600; }
        QPushButton:hover { border-color:#4fd1bb; } QPushButton:disabled { color:%4; border-color:%5; }
        QPushButton#primary { background:#4fd1bb; color:#102823; border:none; padding:10px 20px; }
        QPushButton#primary:hover { background:#70dfcb; } QTabWidget::pane { border:1px solid %5; border-radius:6px; }
        QTabBar::tab { padding:11px 18px; color:%4; border:none; border-bottom:2px solid transparent; } QTabBar::tab:selected { color:%3; border-bottom:2px solid #4fd1bb; }
        QListWidget#navigation { background:transparent; border:none; outline:none; padding-top:10px; }
        QListWidget#navigation::item { padding:15px 10px; border-radius:6px; color:%4; }
        QListWidget#navigation::item:selected { color:%3; background:%2; border-left:3px solid #4fd1bb; }
        QHeaderView::section { background:%2; color:%4; padding:8px; border:none; border-bottom:1px solid %5; }
        QProgressBar { background:%2; border:none; border-radius:3px; max-height:6px; } QProgressBar::chunk { background:#4fd1bb; border-radius:3px; }
        QToolTip { background:%2; color:%3; border:1px solid %5; padding:5px; }
        QScrollBar:vertical { background:%1; width:10px; margin:0; } QScrollBar:horizontal { background:%1; height:10px; margin:0; }
        QScrollBar::handle { background:%5; border-radius:4px; min-height:24px; min-width:24px; }
        QScrollBar::add-line,QScrollBar::sub-line { height:0; width:0; } QScrollBar::add-page,QScrollBar::sub-page { background:none; }
    )").arg(bg,panel,text,muted,border));
}
void Window::showError(const QString& message){ QMessageBox::warning(this,"q3mapx",message); }
bool Window::confirmDiscard(){ return !dirty_ || QMessageBox::question(this,"Unsaved project","Discard the unsaved project changes?",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Discard; }
void Window::closeEvent(QCloseEvent* event){
    if(queue_.running() && QMessageBox::question(this,"Build in progress","Cancel the active build and close?",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) { event->ignore(); return; }
    if(!confirmDiscard()) { event->ignore(); return; }
    queue_.cancel();
    inspection_->cancel();
    hardware_->cancel();
    if(QDir().mkpath(stateDirectory_)) try {
        saveJson(QDir(stateDirectory_).filePath("ui.json"),{{"schema_version",1},{"theme",theme_},{"geometry",QString::fromLatin1(saveGeometry().toBase64())}});
        saveJson(QDir(stateDirectory_).filePath("last-queue.json"),queue_.report());
    } catch(const std::exception& e){ status_->setText(e.what()); }
    event->accept();
}
bool Window::renderPreview(const QString& path){
    // Render the application's own widget tree directly, without reading any OS screen/window pixels.
    QImage image(size()*devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied); image.setDevicePixelRatio(devicePixelRatioF()); image.fill(Qt::transparent);
    render(&image); return image.save(path);
}
}
