// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_inventory.h"
#include <QWidget>
class QLabel; class QPlainTextEdit; class QPushButton; class QTableWidget;

namespace workbench {
class HardwarePage : public QWidget {
    Q_OBJECT
public:
    explicit HardwarePage(QWidget* parent=nullptr);
    void setCompiler(const QString& compiler);
    void cancel() { inventory_.reset("Device query cancelled"); }
private:
    DeviceInventory inventory_;
    QString compiler_;
    QLabel *compilerName_, *summary_;
    QPushButton *refresh_, *cancel_;
    QTableWidget* devices_;
    QPlainTextEdit *details_, *json_;
    void refresh();
    void displayDevice();
};
}
