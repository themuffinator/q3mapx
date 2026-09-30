// SPDX-License-Identifier: GPL-3.0-or-later
#include "inspection_page.h"
#include "project.h"
#include <QtWidgets>
#include <QJsonArray>
#include <stdexcept>

namespace workbench {
InspectionPage::InspectionPage(QWidget* parent):QWidget(parent),inspector_(this){
    setObjectName("inspectionPage");
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* title=new QLabel("BSP inspection"); title->setObjectName("pageTitle"); layout->addWidget(title);
    auto* description=new QLabel("Examine a compiled map's format and file sections without game assets.");
    description->setWordWrap(true); description->setObjectName("muted"); layout->addWidget(description);
    auto* sourceRow=new QHBoxLayout;
    auto* label=new QLabel("BSP &file"); file_=new QLineEdit; file_->setObjectName("inspectionFile"); label->setBuddy(file_);
    file_->setPlaceholderText("Choose a BSP to inspect"); file_->setAccessibleName("BSP inspection source");
    auto* browse=new QPushButton("Browse…"); useProject_=new QPushButton("Use project source");
    sourceRow->addWidget(label); sourceRow->addWidget(file_,1); sourceRow->addWidget(browse); sourceRow->addWidget(useProject_); layout->addLayout(sourceRow);
    connect(browse,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,"Inspect BSP",file_->text(),"Compiled maps (*.bsp);;All files (*)");
        if(!path.isEmpty()) file_->setText(QDir::toNativeSeparators(path));
    });
    connect(useProject_,&QPushButton::clicked,this,[this]{ file_->setText(projectSource_); });
    auto* controls=new QHBoxLayout;
    useProfile_=new QCheckBox; useProfile_->setObjectName("inspectionUseProfile"); controls->addWidget(useProfile_); controls->addStretch();
    inspect_=new QPushButton("Inspect BSP"); inspect_->setObjectName("inspectBsp");
    cancel_=new QPushButton("Cancel inspection"); save_=new QPushButton("Save report…"); save_->setObjectName("saveInspection");
    controls->addWidget(inspect_); controls->addWidget(cancel_); controls->addWidget(save_); layout->addLayout(controls);
    connect(inspect_,&QPushButton::clicked,this,[this]{ inspector_.inspect(compiler_,file_->text(),useProfile_->isChecked()?profile_:QString()); });
    connect(cancel_,&QPushButton::clicked,this,&InspectionPage::cancel);
    connect(save_,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,"Save BSP inspection",{},"JSON (*.json)");
        if(path.isEmpty()) return;
        try { saveReport(path); }
        catch(const std::exception& error) { diagnostics_->setPlainText(QString::fromUtf8(error.what())); }
    });
    summary_=new QLabel; summary_->setObjectName("inspectionSummary"); summary_->setTextFormat(Qt::PlainText); summary_->setWordWrap(true); layout->addWidget(summary_);
    auto* scope=new QLabel("Directory checks cover ranges and record sizes. Geometry and game compatibility are not validated here.");
    scope->setObjectName("notice"); scope->setWordWrap(true); layout->addWidget(scope);
    auto* layoutRow=new QHBoxLayout; auto* layoutLabel=new QLabel("Directory &layout"); layouts_=new QComboBox;
    layouts_->setObjectName("inspectionLayouts"); layoutLabel->setBuddy(layouts_); layoutRow->addWidget(layoutLabel); layoutRow->addWidget(layouts_,1); layout->addLayout(layoutRow);
    lumps_=new QTableWidget(0,6); lumps_->setObjectName("inspectionLumps");
    lumps_->setStyleSheet("QTableWidget#inspectionLumps { padding:0; }");
    lumps_->setHorizontalHeaderLabels({"#","Section / lump","Bytes","Offset","Records","Record bytes"});
    lumps_->verticalHeader()->hide(); lumps_->setEditTriggers(QAbstractItemView::NoEditTriggers); lumps_->setSelectionBehavior(QAbstractItemView::SelectRows);
    lumps_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    lumps_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    lumps_->setMinimumHeight(140);
    auto* details=new QSplitter(Qt::Vertical); details->setObjectName("inspectionDetails");
    details->setChildrenCollapsible(false); details->addWidget(lumps_);
    diagnostics_=new QPlainTextEdit; diagnostics_->setObjectName("inspectionDiagnostics"); diagnostics_->setReadOnly(true);
    diagnostics_->setAccessibleName("Inspection diagnostics and compatibility notes"); diagnostics_->setMinimumHeight(60);
    details->addWidget(diagnostics_); details->setStretchFactor(0,1); details->setStretchFactor(1,0);
    details->setSizes({400,80}); layout->addWidget(details,1);
    connect(file_,&QLineEdit::textChanged,this,[this]{ inspector_.reset(); });
    connect(useProfile_,&QCheckBox::toggled,this,[this]{ inspector_.reset(); });
    connect(layouts_,&QComboBox::currentIndexChanged,this,&InspectionPage::displayLayout);
    connect(&inspector_,&BspInspector::changed,this,&InspectionPage::refresh);
    refresh();
}

void InspectionPage::setContext(const QString& compiler,const QString& source,const QString& profile){
    const bool invalidate=compiler_!=compiler || (useProfile_->isChecked() && profile_!=profile);
    const bool followSource=file_->text().isEmpty() || file_->text()==projectSource_;
    compiler_=compiler; projectSource_=source; profile_=profile;
    useProject_->setEnabled(!source.isEmpty());
    useProfile_->setText("Check against project profile: "+QString(profile).replace('&',"&&"));
    if(followSource) file_->setText(source);
    if(invalidate) inspector_.reset();
    else inspect_->setEnabled(!inspector_.loading() && !compiler_.isEmpty() && !file_->text().trimmed().isEmpty());
}

void InspectionPage::saveReport(const QString& path) const {
    if(inspector_.report().isEmpty()) throw std::runtime_error("Inspect a BSP before saving its report");
    const auto identity=[](const QString& path){
        const QFileInfo file(path); const auto canonical=file.canonicalFilePath();
        return canonical.isEmpty()?QDir::cleanPath(file.absoluteFilePath()):canonical;
    };
#ifdef Q_OS_WIN
    constexpr auto sensitivity=Qt::CaseInsensitive;
#else
    constexpr auto sensitivity=Qt::CaseSensitive;
#endif
    if(identity(inspector_.report()["file"].toString()).compare(identity(path),sensitivity)==0)
        throw std::runtime_error("An inspection report cannot replace its BSP source");
    saveJson(path,inspector_.report());
}

void InspectionPage::refresh(){
    const auto& report=inspector_.report();
    inspect_->setEnabled(!inspector_.loading() && !compiler_.isEmpty() && !file_->text().trimmed().isEmpty());
    cancel_->setEnabled(inspector_.loading()); save_->setEnabled(!report.isEmpty());
    const QSignalBlocker blocker(layouts_); layouts_->clear();
    for(const auto& value:report["layouts"].toArray()) {
        const auto entry=value.toObject();
        layouts_->addItem(entry["title"].toString()+(entry["valid"].toBool()?" · directory valid":" · directory invalid"));
    }
    layouts_->setEnabled(layouts_->count()>0);
    if(inspector_.loading()) summary_->setText("Reading the BSP directory…");
    else if(!inspector_.error().isEmpty()) summary_->setText("Inspection could not complete");
    else if(report.isEmpty()) summary_->setText("Choose a BSP, then inspect its directory. This does not change the project source.");
    else {
        QStringList profiles; for(const auto& value:report["profile_candidates"].toArray()) profiles << value.toString();
        const QString state=report["valid"].toBool()?"A valid directory was found":"Directory checks failed";
        const QString ident=report["ident"].toString().isEmpty()?"Unknown signature":report["ident"].toString();
        summary_->setText(state+" · "+ident+" "+QString::number(report["version"].toInteger())+" · "
            +QLocale().toString(report["file_bytes"].toInteger())+" bytes\nProfile candidates: "
            +(profiles.isEmpty()?"none":profiles.join(", "))+(report["ambiguous_game"].toBool()?" — game is ambiguous":""));
    }
    displayLayout();
}

void InspectionPage::displayLayout(){
    const auto& report=inspector_.report();
    const auto layouts=report["layouts"].toArray();
    const int selected=layouts_->currentIndex();
    const auto current=selected>=0 && selected<layouts.size()?layouts[selected].toObject():QJsonObject();
    const auto lumps=current["lumps"].toArray(); lumps_->setRowCount(lumps.size());
    for(int row=0;row<lumps.size();++row) {
        const auto lump=lumps[row].toObject();
        const QStringList cells{QString::number(lump["index"].toInteger()),lump["name"].toString(),
            QString::number(lump["bytes"].toInteger()),QString::number(lump["offset"].toInteger()),
            lump["records"].isNull()?QString("—"):QString::number(lump["records"].toInteger()),QString::number(lump["record_bytes"].toInteger())};
        for(int column=0;column<cells.size();++column) {
            auto* item=new QTableWidgetItem(cells[column]); item->setToolTip(cells[column]);
            if(column!=1) item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            lumps_->setItem(row,column,item);
        }
    }
    QStringList messages;
    if(!inspector_.error().isEmpty()) messages << inspector_.error();
    for(const auto& message:report["errors"].toArray()) messages << "ERROR: "+message.toString();
    for(const auto& message:current["errors"].toArray()) messages << "LAYOUT: "+message.toString();
    for(const auto& message:report["notes"].toArray()) messages << message.toString();
    if(!report.isEmpty() && messages.isEmpty()) messages << "Directory checks completed. Use the native loader to validate geometry before recovery or compilation.";
    diagnostics_->setPlainText(messages.join('\n'));
}
}
