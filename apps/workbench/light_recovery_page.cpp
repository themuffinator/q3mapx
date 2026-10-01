// SPDX-License-Identifier: GPL-3.0-or-later
#include "light_recovery_page.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <cmath>

namespace workbench {
namespace {
class PreciseSpinBox final : public QDoubleSpinBox {
public:
    PreciseSpinBox() { setDecimals(323); }
protected:
    QString textFromValue(double value) const override { return locale().toString(value,'g',QLocale::FloatingPointShortest); }
};
QLabel* label(const QString& text,const char* name=nullptr) {
    auto* result=new QLabel(text); result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
    if(name) result->setObjectName(name); return result;
}
QWidget* scrollPage(QWidget* content) {
    auto* result=new QScrollArea; result->setWidget(content); result->setWidgetResizable(true); result->setFrameShape(QFrame::NoFrame); return result;
}
QString vectorText(const QJsonValue& value) {
    QStringList parts; for(const auto& item:value.toArray()) parts << QString::number(item.toDouble(),'f',2); return parts.join("  ");
}
QTableWidget* table(const QStringList& columns,const char* name) {
    auto* result=new QTableWidget(0,columns.size()); result->setObjectName(name); result->setHorizontalHeaderLabels(columns);
    result->verticalHeader()->hide(); result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection); result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); result->horizontalHeader()->setStretchLastSection(true);
    return result;
}
}
LightRecoveryPage::LightRecoveryPage(QWidget* parent):QWidget(parent),reader_(this) {
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    layout->addWidget(label("Light recovery","pageTitle"));
    context_=label("Choose a baked BSP in Project settings.","lightRecoveryContext"); layout->addWidget(context_);
    tabs_=new QTabWidget; tabs_->setObjectName("lightRecoveryTabs"); layout->addWidget(tabs_,1);
    auto* fitPage=new QWidget; auto* fitLayout=new QVBoxLayout(fitPage);
    fitLayout->addWidget(label("Search for missing entity lights while keeping surviving lights and material/sky illumination fixed. Results depend on the assets and bake settings below.","muted"));
    auto* columns=new QHBoxLayout; auto* search=new QFormLayout; auto* bake=new QFormLayout;
    search->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); bake->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto integer=[this](const char* name,int lo,int hi){ auto* spin=new QSpinBox; spin->setObjectName(name); spin->setRange(lo,hi); connect(spin,&QSpinBox::valueChanged,this,&LightRecoveryPage::notifySettings); return spin; };
    const auto real=[this](const char* name,double lo,double hi){ auto* spin=new PreciseSpinBox; spin->setObjectName(name); spin->setRange(lo,hi); connect(spin,&QDoubleSpinBox::valueChanged,this,&LightRecoveryPage::notifySettings); return spin; };
    const auto flag=[this](const char* name,const QString& text){ auto* check=new QCheckBox(text); check->setObjectName(name); connect(check,&QCheckBox::toggled,this,&LightRecoveryPage::notifySettings); return check; };
    family_=new QComboBox; family_->setObjectName("lightFitFamily"); family_->addItem("Point lights","point"); family_->addItem("Spotlights and targets","spot");
    connect(family_,&QComboBox::currentIndexChanged,this,&LightRecoveryPage::notifySettings);
    stride_=integer("lightFitStride",1,256); count_=integer("lightFitCount",1,16); refine_=integer("lightFitRefinements",0,10);
    style_=integer("lightFitStyle",0,253); work_=integer("lightFitWork",1,1'000'000'000); work_->setGroupSeparatorShown(true); work_->setSingleStep(50'000'000);
    spacing_=real("lightFitSpacing",1,1e6); rmse_=real("lightFitRMSE",.01,64); improvement_=real("lightFitImprovement",.01,64);
    search->addRow("&Source family",family_); search->addRow("Maximum &lights",count_); search->addRow("Grid spacing · units",spacing_);
    search->addRow("Sample stride · texels",stride_); search->addRow("Refinement steps",refine_); search->addRow("Light style",style_);
    search->addRow("Maximum RMSE · bytes",rmse_); search->addRow("Minimum improvement",improvement_); search->addRow("Search work limit",work_);
    stride_->setToolTip("Larger strides reduce observations and cost, but can miss small spotlight footprints.");
    work_->setToolTip("Maximum charged search work, not a time estimate. Exhaustion preserves existing reports.");
    gamma_=real("lightFitGamma",.01,16); compensate_=real("lightFitCompensate",.01,64); extra_=real("lightFitExtraDistance",0,1e6);
    wolf_=flag("lightFitWolf","Wolf light defaults"); lightSRGB_=flag("lightFitLightSRGB","sRGB lightmaps");
    textureSRGB_=flag("lightFitTextureSRGB","sRGB texture colors"); colorSRGB_=flag("lightFitColorSRGB","sRGB entity colors");
    bake->addRow("Gamma",gamma_); bake->addRow("Compensation",compensate_); bake->addRow("Extra distance",extra_);
    bake->addRow(wolf_); bake->addRow(lightSRGB_); bake->addRow(textureSRGB_); bake->addRow(colorSRGB_);
    bake->addRow(label("These are explicit hypotheses. Unknown original settings, indirect lighting and missing assets can prevent a useful fit."));
    columns->addLayout(search,1); columns->addSpacing(24); columns->addLayout(bake,1); fitLayout->addLayout(columns);
    auto* fixedRow=new QHBoxLayout; fixed_=label({},"lightFitFixedCount"); auto* loadFixed=new QPushButton("Load fixed lights…"); auto* clearFixed=new QPushButton("Clear fixed lights");
    fixedRow->addWidget(fixed_,1); fixedRow->addWidget(loadFixed); fixedRow->addWidget(clearFixed); fitLayout->addLayout(fixedRow);
    loadFixed->setToolTip("Optional JSON array of native light-probe light objects. These sources remain fixed during fitting and are included in MAP export.");
    connect(loadFixed,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,"Load fixed light hypotheses",{},"JSON (*.json)"); if(path.isEmpty()) return;
        QFile file(path); QJsonParseError error;
        if(!file.open(QIODevice::ReadOnly) || file.size()>65536) { QMessageBox::warning(this,"Fixed lights","Choose a JSON array no larger than 64 KiB."); return; }
        const auto doc=QJsonDocument::fromJson(file.read(65537),&error);
        LightFitSettings settings; settings.fixedLights=doc.array();
        if(error.error!=QJsonParseError::NoError || !doc.isArray() || !settings.validate().isEmpty()) { QMessageBox::warning(this,"Fixed lights","Expected up to 256 light objects in a JSON array."); return; }
        fixedLights_=doc.array(); notifySettings();
    });
    connect(clearFixed,&QPushButton::clicked,this,[this]{ fixedLights_={}; notifySettings(); });
    fitLayout->addStretch(); fit_=new QPushButton("Fit missing lights"); fit_->setObjectName("fitMissingLights");
    connect(fit_,&QPushButton::clicked,this,&LightRecoveryPage::fitRequested);
    auto* fitContainer=new QWidget; auto* fitContainerLayout=new QVBoxLayout(fitContainer); fitContainerLayout->setContentsMargins(0,0,0,0);
    fitContainerLayout->addWidget(scrollPage(fitPage),1); fitContainerLayout->addWidget(fit_,0,Qt::AlignLeft);
    tabs_->addTab(fitContainer,"Fit lights");

    auto* review=new QWidget; auto* reviewLayout=new QVBoxLayout(review);
    auto* pathRow=new QHBoxLayout; report_=new QLineEdit; report_->setObjectName("lightFitReportPath"); report_->setPlaceholderText("Select a light fitting report…"); report_->setAccessibleName("Light fitting report");
    auto* browse=new QPushButton("Browse…"); refresh_=new QPushButton("Refresh"); cancel_=new QPushButton("Cancel read");
    pathRow->addWidget(report_,1); pathRow->addWidget(browse); pathRow->addWidget(refresh_); pathRow->addWidget(cancel_); reviewLayout->addLayout(pathRow);
    connect(browse,&QPushButton::clicked,this,[this]{ const auto path=QFileDialog::getOpenFileName(this,"Review light fit",report_->text(),"JSON (*.json)"); if(!path.isEmpty()) reviewReport(path); });
    connect(report_,&QLineEdit::textChanged,this,[this]{
        notifySettings(); if(populating_) return;
        reader_.cancel(); refreshResult();
        const auto path=report_->text(); QTimer::singleShot(250,this,[this,path]{ if(report_->text()==path) refreshReview(); });
    });
    connect(refresh_,&QPushButton::clicked,this,&LightRecoveryPage::refreshReview); connect(cancel_,&QPushButton::clicked,&reader_,&LightFitReader::cancel);
    summary_=label({},"lightFitSummary"); reviewLayout->addWidget(summary_);
    scores_=table({"Samples","Count","Before RMSE","After RMSE"},"lightFitScores"); scores_->setMinimumHeight(120); scores_->setMaximumHeight(190); reviewLayout->addWidget(scores_);
    lights_=table({"Light","Position","Intensity","Color","Target"},"lightFitProposals"); lights_->setMinimumHeight(135); reviewLayout->addWidget(lights_,1);
    connect(lights_,&QTableWidget::itemSelectionChanged,this,&LightRecoveryPage::selectLight);
    details_=new QPlainTextEdit; details_->setObjectName("lightFitDetails"); details_->setReadOnly(true); details_->setMaximumHeight(110); reviewLayout->addWidget(details_);
    application_=label({},"lightFitApplication");
    auto* applyRow=new QHBoxLayout; apply_=new QCheckBox("Apply this report when decompiling"); apply_->setObjectName("applyLightReport");
    use_=new QPushButton("Use for MAP recovery"); use_->setObjectName("useLightFit"); applyRow->addWidget(apply_,1); applyRow->addWidget(use_);
    connect(apply_,&QCheckBox::toggled,this,[this](bool enabled){ if(enabled && reader_.result().canApply()) approvedHash_=reader_.result().reportHash; notifySettings(); refreshResult(); });
    connect(use_,&QPushButton::clicked,this,[this]{
        if(reader_.loading() || !reader_.result().canApply() || !canApply_) return;
        approvedHash_=reader_.result().reportHash; { const QSignalBlocker blocker(apply_); apply_->setChecked(true); }
        notifySettings(); refreshResult(); emit useReportRequested();
    });
    auto* reviewContainer=new QWidget; auto* reviewContainerLayout=new QVBoxLayout(reviewContainer); reviewContainerLayout->setContentsMargins(0,0,0,0);
    reviewContainerLayout->addWidget(scrollPage(review),1); reviewContainerLayout->addWidget(application_); reviewContainerLayout->addLayout(applyRow);
    tabs_->addTab(reviewContainer,"Review report");
    settings_=new QPlainTextEdit; settings_->setObjectName("lightFitRecordedSettings"); settings_->setReadOnly(true); tabs_->addTab(settings_,"Recorded bake settings");
    connect(&reader_,&LightFitReader::changed,this,[this]{ refreshResult(); emit eligibilityChanged(); });
    setProject(Project{});
}
void LightRecoveryPage::notifySettings() {
    fixed_->setText(QString("%1 fixed light hypotheses").arg(fixedLights_.size()));
    if(!populating_) emit settingsChanged();
}
void LightRecoveryPage::applyTo(Project& p) const {
    auto& s=p.lightFit; s.family=family_->currentData().toString(); s.stride=stride_->value(); s.maxLights=count_->value();
    s.refinements=refine_->value(); s.style=style_->value(); s.maxWork=work_->value(); s.spacing=spacing_->value();
    s.gamma=gamma_->value(); s.compensate=compensate_->value(); s.extraDistance=extra_->value(); s.maxRMSE=rmse_->value(); s.minImprovement=improvement_->value();
    s.wolf=wolf_->isChecked(); s.lightmapsSRGB=lightSRGB_->isChecked(); s.texturesSRGB=textureSRGB_->isChecked(); s.colorsSRGB=colorSRGB_->isChecked(); s.fixedLights=fixedLights_;
    p.applyLightReport=apply_->isChecked(); p.lightReport=QDir::fromNativeSeparators(report_->text()); p.lightReportHash=approvedHash_;
}
void LightRecoveryPage::setProject(const Project& p) {
    populating_=true; const auto& s=p.lightFit;
    family_->setCurrentIndex(family_->findData(s.family)); stride_->setValue(s.stride); count_->setValue(s.maxLights); refine_->setValue(s.refinements);
    style_->setValue(s.style); work_->setValue(s.maxWork); spacing_->setValue(s.spacing); gamma_->setValue(s.gamma); compensate_->setValue(s.compensate);
    extra_->setValue(s.extraDistance); rmse_->setValue(s.maxRMSE); improvement_->setValue(s.minImprovement);
    wolf_->setChecked(s.wolf); lightSRGB_->setChecked(s.lightmapsSRGB); textureSRGB_->setChecked(s.texturesSRGB); colorSRGB_->setChecked(s.colorsSRGB);
    fixedLights_=s.fixedLights; report_->setText(QDir::toNativeSeparators(p.lightReport)); apply_->setChecked(p.applyLightReport); approvedHash_=p.lightReportHash;
    populating_=false; fixed_->setText(QString("%1 fixed light hypotheses").arg(fixedLights_.size())); refreshReview();
}
void LightRecoveryPage::setContext(const QString& source,const QString& game,bool canFit,bool canApply) {
    source_=source; game_=game; canFit_=canFit; canApply_=canApply;
    context_->setText(source.isEmpty()?"Choose a baked BSP in Project settings.":QString("Source: %1 · %2").arg(QFileInfo(source).fileName(),game));
    fit_->setEnabled(canFit && QFileInfo(source).suffix().compare("bsp",Qt::CaseInsensitive)==0);
    fit_->setToolTip(canFit?"Run a cancellable fitting job in a new output folder.":"The selected compiler/profile must advertise light fitting.");
    if(reviewSource_!=source_ || reviewGame_!=game_ || reviewPath_!=report_->text()) refreshReview(); else refreshResult();
}
void LightRecoveryPage::refreshReview() {
    reviewSource_=source_; reviewGame_=game_; reviewPath_=report_->text(); reader_.refresh(report_->text(),source_,game_);
}
void LightRecoveryPage::reviewReport(const QString& path) {
    { const QSignalBlocker blocker(report_); report_->setText(QDir::toNativeSeparators(path)); }
    notifySettings(); tabs_->setCurrentIndex(1); refreshReview();
}
QString LightRecoveryPage::applicationError() const {
    if(!apply_->isChecked()) return {};
    if(!canApply_) return "The selected compiler/profile does not support applying light reports.";
    if(reader_.loading()) return "Wait for the selected light report to finish loading.";
    if(!reader_.error().isEmpty()) return reader_.error();
    if(!reader_.result().canApply()) return "The selected light report must be accepted and match this BSP and game.";
    if(approvedHash_!=reader_.result().reportHash) return "The selected report changed. Review it and select Use for MAP recovery again.";
    return {};
}
void LightRecoveryPage::refreshResult() {
    const auto& r=reader_.result();
    const auto clear=[this]{ scores_->setRowCount(0); lights_->setRowCount(0); details_->clear(); settings_->clear(); displayedHash_.clear(); };
    cancel_->setEnabled(reader_.loading()); refresh_->setEnabled(!report_->text().isEmpty());
    const bool ready=!reader_.loading() && reader_.error().isEmpty() && r.canApply() && canApply_;
    use_->setEnabled(ready); apply_->setEnabled(ready || apply_->isChecked());
    application_->setText(apply_->isChecked() ? applicationError().isEmpty()?"Selected for MAP recovery. The exact reviewed report will be staged with the BSP.":applicationError()
        : !canApply_?"Applying light reports requires advertised support from the selected compiler and game profile."
        : "Review the proposal before use. MAP export rechecks the report; rebuild and compare lighting before adopting the result.");
    if(report_->text().isEmpty()) { clear(); summary_->setText("Choose a fitting report or run Fit missing lights."); return; }
    if(reader_.loading()) { clear(); summary_->setText("Reading report and checking the source BSP…"); return; }
    if(!reader_.error().isEmpty()) { clear(); summary_->setText(reader_.error()); return; }
    if(r.path.isEmpty()) { clear(); summary_->setText("Choose a source BSP to compare with this report."); return; }
    QString status=r.accepted?"Accepted by fitter":"Not accepted by fitter";
    if(!r.sourceMatches) status+=" · different source BSP"; if(!r.gameMatches) status+=" · different game profile";
    QString reason=r.status; reason.replace('_',' '); if(!reason.isEmpty()) reason[0]=reason[0].toUpper();
    summary_->setText(QString("%1 · %2 fitted %3 lights · %4 fixed lights\n%5. Conditional proposals; original author lights and target identities are unproven.")
        .arg(status).arg(r.lights.size()).arg(r.family).arg(r.fixedLights).arg(reason));
    if(displayedHash_==r.reportHash) return;
    clear(); displayedHash_=r.reportHash;
    for(const auto& partition:QStringList{"training","withheld"}) {
        const auto metrics=r.scores[partition].toObject();
        for(const auto& pair:QList<QPair<QString,QString>>{{"baseline","trial"},{"illuminated_baseline","illuminated_trial"}}) {
            if(!metrics.contains(pair.first)) continue;
            const auto base=metrics[pair.first].toObject(),trial=metrics[pair.second].toObject(); const int row=scores_->rowCount(); scores_->insertRow(row);
            const auto metric=[](const QJsonValue& value){ return value.isNull()?QString("—"):QString::number(value.toDouble(),'f',3); };
            const QStringList values{(partition=="training"?"Training":"Withheld")+QString(pair.first.startsWith("illuminated")?" · lit support":""),
                QString::number(trial["samples"].toInt()),metric(base["rmse_bytes"]),metric(trial["rmse_bytes"])};
            for(int c=0;c<values.size();++c) scores_->setItem(row,c,new QTableWidgetItem(values[c]));
        }
    }
    for(const auto& value:r.lights) {
        const auto light=value.toObject(); const auto target=light["target_link"].toObject(); const int row=lights_->rowCount(); lights_->insertRow(row);
        const QStringList values{QString::number(row+1),vectorText(light["origin"]),QString::number(light["intensity"].toDouble(),'f',2),vectorText(light["color"]),target["targetname"].toString("—")};
        for(int c=0;c<values.size();++c) { auto* cell=new QTableWidgetItem(values[c]); cell->setToolTip(values[c]); lights_->setItem(row,c,cell); }
    }
    QStringList values{"Recorded hypothesis — use these settings when rebuilding the recovered MAP.",""};
    for(auto it=r.settings.begin();it!=r.settings.end();++it) {
        const auto value=it.value(); values << it.key()+": "+(value.isBool()?(value.toBool()?"Yes":"No"):value.isDouble()?QString::number(value.toDouble(),'g',9):value.toString());
    }
    values << "" << "Source SHA-256: "+r.sourceHash << "Report SHA-256: "+r.reportHash;
    settings_->setPlainText(values.join('\n')); if(lights_->rowCount()) lights_->selectRow(0);
}
void LightRecoveryPage::selectLight() {
    const int row=lights_->currentRow(); if(row<0 || row>=reader_.result().lights.size()) { details_->clear(); return; }
    const auto light=reader_.result().lights[row].toObject(); const auto target=light["target_link"].toObject();
    QStringList text{QString("Light %1 · style %2 · extra distance %3").arg(row+1).arg(light["style"].toInt()).arg(light["extra_distance"].toDouble())};
    if(!target.isEmpty()) text << "Target: "+target["targetname"].toString()+" · "+target["status"].toString()
        << "Target position: "+vectorText(target["origin"])+QString(" · cone half-angle %1° · radius %2").arg(light["half_angle_degrees"].toDouble(),0,'f',2).arg(light["radius"].toDouble(),0,'f',2);
    text << "Color uses the recorded entity color space. Existing gameplay links are not retargeted."; details_->setPlainText(text.join('\n'));
}
}
