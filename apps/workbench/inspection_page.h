// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bsp_inspector.h"
#include <QWidget>
class QCheckBox; class QComboBox; class QLabel; class QLineEdit; class QPlainTextEdit;
class QPushButton; class QTableWidget;

namespace workbench {
class InspectionPage : public QWidget {
    Q_OBJECT
public:
    explicit InspectionPage(QWidget* parent=nullptr);
    void setContext(const QString& compiler,const QString& source,const QString& profile);
    void cancel() { inspector_.reset("Inspection cancelled"); }
    void saveReport(const QString& path) const;
private:
    BspInspector inspector_;
    QString compiler_, projectSource_, profile_;
    QLineEdit* file_;
    QCheckBox* useProfile_;
    QPushButton *inspect_, *cancel_, *useProject_, *save_;
    QComboBox* layouts_;
    QLabel* summary_;
    QTableWidget* lumps_;
    QPlainTextEdit* diagnostics_;
    void refresh();
    void displayLayout();
};
}
