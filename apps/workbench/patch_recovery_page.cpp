// SPDX-License-Identifier: GPL-3.0-or-later
#include "patch_recovery_page.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QStandardItemModel>

namespace workbench {
namespace {
QLabel* label(const QString& text,const char* name=nullptr) {
    auto* out=new QLabel(text); out->setTextFormat(Qt::PlainText); out->setWordWrap(true);
    if(name) out->setObjectName(name); return out;
}
bool accepted(const QString& status) { return status=="retained" || status=="fitted" || status=="recovered"; }
}
PatchRecoveryPage::PatchRecoveryPage(QWidget* parent):QWidget(parent),reader_(this) {
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    layout->addWidget(label("Patch recovery","pageTitle"));
    context_=label("Choose a BSP and compiler in Project.","patchRecoveryContext"); layout->addWidget(context_);
    tabs_=new QTabWidget; tabs_->setObjectName("patchRecoveryTabs"); layout->addWidget(tabs_,1);
    auto* settings=new QWidget; auto* form=new QFormLayout(settings); form->setContentsMargins(18,18,18,18); form->setVerticalSpacing(16);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mode_=new QComboBox; mode_->setObjectName("patchRecoveryMode");
    mode_->addItem("Ordinary patches · default","none"); mode_->addItem("Retained sources only","source");
    mode_->addItem("Fit triangle meshes","fit"); mode_->addItem("Sources, then triangle fitting","auto");
    colors_=new QComboBox; colors_->setObjectName("patchRecoveryColors");
    colors_->addItem("No compiled colors · default","none"); colors_->addItem("Alpha · rebake RGB lighting","alpha"); colors_->addItem("RGBA · keep stored RGB as material","rgba");
    subdivisions_=new QComboBox; subdivisions_->setObjectName("patchRecoverySubdivisions");
    for(int n:{1,2,4,8,16,32}) subdivisions_->addItem(QString::number(n),n);
    subdivisions_->setToolTip("Sampling for recovered native control colors. Fitted triangles use observed sampling; archived sources retain authored sampling.");
    work_=new QSpinBox; work_->setObjectName("patchRecoveryWorkLimit"); work_->setRange(1,1'000'000'000); work_->setSingleStep(1'000'000); work_->setGroupSeparatorShown(true);
    work_->setToolTip("Bounds fitting work, not elapsed time. Exhausted candidates are reported and skipped; accepted patches still export. Default: 50,000,000.");
    form->addRow("Patch &geometry",mode_); form->addRow("Compiled &channels",colors_);
    form->addRow("Native color &sampling",subdivisions_); form->addRow("Fitting work &limit",work_);
    hint_=label({},"patchRecoveryHint"); form->addRow(hint_);
    form->addRow(label("Retained sources preserve pre-modifier controls and settings. Fitted and stored native channels describe compiled data; they do not prove original paint or complete rebuild equivalence."));
    recover_=new QPushButton("Recover MAP"); recover_->setObjectName("recoverPatches"); form->addRow(recover_);
    auto* scroll=new QScrollArea; scroll->setWidget(settings); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    tabs_->addTab(scroll,"Settings");
    auto* review=new QWidget; auto* body=new QVBoxLayout(review); body->setContentsMargins(12,12,12,12);
    auto* path=new QHBoxLayout; report_=new QLineEdit; report_->setObjectName("patchReviewPath"); report_->setAccessibleName("Patch recovery report path");
    report_->setPlaceholderText("Recovery JSON from a completed MAP export"); path->addWidget(report_,1);
    auto* browse=new QPushButton("Browse…"); path->addWidget(browse);
    refresh_=new QPushButton("Read report"); refresh_->setObjectName("patchReviewRefresh"); path->addWidget(refresh_);
    cancel_=new QPushButton("Cancel read"); cancel_->setObjectName("patchReviewCancel"); path->addWidget(cancel_); body->addLayout(path);
    summary_=label("Run MAP recovery or select an existing recovery report.","patchReviewSummary"); body->addWidget(summary_);
    filter_=new QComboBox; filter_->setObjectName("patchReviewFilter"); filter_->setAccessibleName("Patch decision filter");
    filter_->addItems({"All decisions","Restored / fitted","Skipped candidates"}); body->addWidget(filter_);
    auto* split=new QSplitter(Qt::Vertical);
    decisions_=new QTableWidget(0,4); decisions_->setObjectName("patchReviewDecisions"); decisions_->setAccessibleName("Patch recovery decisions");
    decisions_->setHorizontalHeaderLabels({"Evidence","Outcome","Model / surfaces","Control grid"}); decisions_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    decisions_->setSelectionBehavior(QAbstractItemView::SelectRows); decisions_->setSelectionMode(QAbstractItemView::SingleSelection);
    decisions_->verticalHeader()->hide(); decisions_->setWordWrap(false);
    decisions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); decisions_->horizontalHeader()->setStretchLastSection(true);
    decisions_->setColumnWidth(0,135); decisions_->setColumnWidth(1,220); decisions_->setColumnWidth(2,250);
    details_=new QPlainTextEdit; details_->setObjectName("patchReviewDetails"); details_->setReadOnly(true); details_->setAccessibleName("Selected patch decision details");
    split->addWidget(decisions_); split->addWidget(details_); split->setStretchFactor(0,3); split->setStretchFactor(1,1); body->addWidget(split,1);
    body->addWidget(label("Fits do not prove original controls. Skipped candidates may still have brush or native patch geometry."));
    tabs_->addTab(review,"Results");
    const auto changed=[this]{ if(populating_) return; updateControls(); emit settingsChanged(); };
    for(auto* combo:{mode_,colors_,subdivisions_}) connect(combo,&QComboBox::currentIndexChanged,this,changed);
    connect(work_,&QSpinBox::valueChanged,this,changed);
    connect(recover_,&QPushButton::clicked,this,&PatchRecoveryPage::recoverRequested);
    connect(browse,&QPushButton::clicked,this,[this]{ const auto file=QFileDialog::getOpenFileName(this,"Open recovery report",report_->text(),"Recovery reports (*.json)"); if(!file.isEmpty()) reviewReport(file); });
    connect(refresh_,&QPushButton::clicked,this,[this]{ reader_.refresh(QDir::fromNativeSeparators(report_->text())); });
    connect(cancel_,&QPushButton::clicked,&reader_,&PatchReviewReader::cancel);
    connect(report_,&QLineEdit::textChanged,this,[this]{ reader_.refresh({}); });
    connect(&reader_,&PatchReviewReader::changed,this,&PatchRecoveryPage::updateReview);
    connect(filter_,&QComboBox::currentIndexChanged,this,&PatchRecoveryPage::populateDecisions);
    connect(decisions_,&QTableWidget::itemSelectionChanged,this,&PatchRecoveryPage::selectDecision);
    setProject(Project{}); updateReview();
}
void PatchRecoveryPage::setProject(const Project& p) {
    populating_=true; mode_->setCurrentIndex(mode_->findData(p.patchRecovery)); colors_->setCurrentIndex(colors_->findData(p.patchColors));
    subdivisions_->setCurrentIndex(subdivisions_->findData(p.patchColorSubdivisions)); work_->setValue(p.patchFitWorkLimit);
    populating_=false; updateControls();
}
void PatchRecoveryPage::applyTo(Project& p) const {
    p.patchRecovery=mode_->currentData().toString(); p.patchColors=colors_->currentData().toString();
    p.patchColorSubdivisions=subdivisions_->currentData().toInt(); p.patchFitWorkLimit=work_->value();
}
void PatchRecoveryPage::setContext(const QString& source,const GameProfile* profile) {
    source_=source; known_=profile; if(profile) profile_=*profile;
    context_->setText(source.isEmpty()?"Choose a BSP and compiler in Project.":QString("%1\n%2").arg(profile?profile->title:"Compiler/profile support unavailable",QDir::toNativeSeparators(source)));
    updateControls();
}
QString PatchRecoveryPage::applicationError() const {
    return patchRecoverySupportError(known_?&profile_:nullptr,mode_->currentData().toString(),colors_->currentData().toString());
}
void PatchRecoveryPage::updateControls() {
    const auto enable=[&](QComboBox* combo,const QStringList& supported){
        auto* model=qobject_cast<QStandardItemModel*>(combo->model());
        for(int i=0;i<combo->count();++i) model->item(i)->setEnabled(combo->itemData(i)=="none" || (known_ && profile_.nativeWrite && supported.contains(combo->itemData(i).toString())));
    };
    enable(mode_,profile_.recoveryPatchPolicies); enable(colors_,profile_.recoveryPatchColors);
    const bool fitting=mode_->currentData()=="fit" || mode_->currentData()=="auto";
    work_->setEnabled(fitting && applicationError().isEmpty()); subdivisions_->setEnabled(colors_->currentData()!="none" && applicationError().isEmpty());
    const auto error=applicationError();
    QString hint=error;
    if(hint.isEmpty()) {
        if(mode_->currentData()=="source") hint="Requires a matching source archive. Missing or stale archives fail before publishing the MAP. Archived patches retain their own paint and settings.";
        else if(mode_->currentData()=="auto") hint="Restore retained sources first, then fit eligible remaining triangle grids. A stale archive fails recovery. Unsupported meshes are reported and skipped.";
        else if(fitting) hint="Infer eligible nonsolid quadratic grids from triangle samples. Original controls and density are unproven; unsupported meshes are reported and skipped.";
        else hint="Keep ordinary patch geometry. Choose compiled channels below to recover eligible native control colors.";
        if(colors_->currentData()=="rgba") hint+=" RGBA freezes stored RGB, which may include baked lighting.";
        else if(colors_->currentData()=="alpha") hint+=" Alpha keeps stored transparency and leaves RGB to rebaking.";
        else if(fitting) hint+=" Fitted patches use opaque alpha and white RGB when compiled channels are disabled.";
    }
    hint_->setText(hint);
    recover_->setEnabled(known_ && profile_.workflows.contains("decompile") && QFileInfo(source_).suffix().compare("bsp",Qt::CaseInsensitive)==0 && error.isEmpty());
}
void PatchRecoveryPage::reviewReport(const QString& path) {
    report_->setText(QDir::toNativeSeparators(path)); tabs_->setCurrentIndex(1); reader_.refresh(path);
}
void PatchRecoveryPage::updateReview() {
    cancel_->setEnabled(reader_.loading()); refresh_->setEnabled(!reader_.loading());
    const auto& r=reader_.result();
    if(reader_.loading()) summary_->setText("Reading recovery report…");
    else if(!reader_.error().isEmpty()) summary_->setText(reader_.error());
    else if(r.path.isEmpty()) summary_->setText("Run MAP recovery or select an existing recovery report.");
    else summary_->setText(QString("%1 retained sources · %2 fitted patches · %3 native colors · %4 skipped decisions\n%5 · mode %6 · colors %7 · %8 omitted records\nSource: %9 → %10")
        .arg(r.archived).arg(r.fitted).arg(r.native).arg(r.skipped).arg(r.game,r.policy,r.colors).arg(r.omitted).arg(QFileInfo(r.input).fileName(),QFileInfo(r.output).fileName()));
    summary_->setToolTip(r.path.isEmpty()?QString():QString("Source: %1\nOutput: %2\nCounts include omitted records.\nReport: %3").arg(r.input,r.output,r.path));
    summary_->setAccessibleDescription(summary_->toolTip());
    populateDecisions();
}
void PatchRecoveryPage::populateDecisions() {
    const QSignalBlocker blocker(decisions_); decisions_->setRowCount(0); details_->clear();
    const auto& records=reader_.result().decisions;
    QVector<qsizetype> visible;
    for(qsizetype i=0;i<records.size();++i) {
        const auto& r=records[i]; const bool good=accepted(r.status);
        if((filter_->currentIndex()==1 && !good) || (filter_->currentIndex()==2 && good)) continue;
        visible.append(i);
    }
    decisions_->setUpdatesEnabled(false); decisions_->setRowCount(int(visible.size()));
    for(qsizetype row=0;row<visible.size();++row) {
        const auto i=visible[row]; const auto& r=records[i];
        QString outcome=r.status; outcome.replace('_',' ');
        const QStringList fields{r.basis,outcome,r.location,r.shape};
        for(int c=0;c<4;++c) { auto* item=new QTableWidgetItem(fields[c]); item->setData(Qt::UserRole,i); item->setToolTip(fields[c]); decisions_->setItem(row,c,item); }
    }
    decisions_->setUpdatesEnabled(true);
    if(decisions_->rowCount()) decisions_->selectRow(0);
    selectDecision();
}
void PatchRecoveryPage::selectDecision() {
    const auto* item=decisions_->item(decisions_->currentRow(),0);
    if(!item) { details_->clear(); return; }
    const auto index=item->data(Qt::UserRole).toLongLong(); const auto& result=reader_.result();
    if(index<0 || index>=result.decisions.size()) return;
    details_->setPlainText(QString::fromUtf8(QJsonDocument(result.decisions[index].details).toJson())+"\nReport SHA-256: "+result.reportHash);
}
}
