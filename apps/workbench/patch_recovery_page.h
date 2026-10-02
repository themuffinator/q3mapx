// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_catalog.h"
#include "project.h"
#include "patch_review.h"
#include <QWidget>
class QComboBox; class QLabel; class QLineEdit; class QPlainTextEdit; class QPushButton;
class QSpinBox; class QTableWidget; class QTabWidget;
namespace workbench {
class PatchRecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit PatchRecoveryPage(QWidget* parent=nullptr);
    void setProject(const Project& project);
    void applyTo(Project& project) const;
    void setContext(const QString& source,const GameProfile* profile);
    QString applicationError() const;
    void reviewReport(const QString& path);
signals:
    void settingsChanged();
    void recoverRequested();
private:
    PatchReviewReader reader_;
    GameProfile profile_;
    QString source_;
    bool known_=false,populating_=false;
    QTabWidget* tabs_;
    QComboBox *mode_,*colors_,*subdivisions_,*filter_;
    QSpinBox* work_;
    QLabel *context_,*hint_,*summary_;
    QLineEdit* report_;
    QPushButton *recover_,*refresh_,*cancel_;
    QTableWidget* decisions_;
    QPlainTextEdit* details_;
    void updateControls();
    void updateReview();
    void populateDecisions();
    void selectDecision();
};
}
