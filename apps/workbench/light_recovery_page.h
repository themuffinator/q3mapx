// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "project.h"
#include <QWidget>
class QCheckBox; class QComboBox; class QDoubleSpinBox; class QLabel; class QLineEdit;
class QPlainTextEdit; class QPushButton; class QSpinBox; class QTableWidget; class QTabWidget;
namespace workbench {
class LightRecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit LightRecoveryPage(QWidget* parent=nullptr);
    void setProject(const Project& project);
    void applyTo(Project& project) const;
    void setContext(const QString& source,const QString& game,bool canFit,bool canApply);
    void reviewReport(const QString& path);
    QString applicationError() const;
signals:
    void settingsChanged();
    void eligibilityChanged();
    void fitRequested();
    void useReportRequested();
private:
    LightFitReader reader_;
    QString source_,game_,reviewSource_,reviewGame_,reviewPath_,approvedHash_,displayedHash_;
    bool canFit_=false,canApply_=false,populating_=false;
    QJsonArray fixedLights_;
    QTabWidget* tabs_;
    QComboBox* family_;
    QSpinBox *stride_,*count_,*refine_,*style_,*work_;
    QDoubleSpinBox *spacing_,*gamma_,*compensate_,*extra_,*rmse_,*improvement_;
    QCheckBox *wolf_,*lightSRGB_,*textureSRGB_,*colorSRGB_,*apply_;
    QLineEdit* report_;
    QPushButton *fit_,*use_,*refresh_,*cancel_;
    QLabel *context_,*summary_,*fixed_,*application_;
    QTableWidget *scores_,*lights_;
    QPlainTextEdit *details_,*settings_;
    void refreshReview();
    void refreshResult();
    void selectLight();
    void notifySettings();
};
}
