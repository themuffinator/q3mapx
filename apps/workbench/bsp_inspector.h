// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QObject>
#include <QPointer>
class QProcess;

namespace workbench {
QJsonObject parseBspInspection(const QByteArray& bytes);

class BspInspector : public QObject {
    Q_OBJECT
public:
    explicit BspInspector(QObject* parent=nullptr) : QObject(parent) {}
    void inspect(const QString& compiler,const QString& file,const QString& profile={},int timeoutMs=10000);
    void reset(const QString& message={});
    bool loading() const { return loading_; }
    const QString& error() const { return error_; }
    const QJsonObject& report() const { return report_; }
signals:
    void changed();
private:
    QPointer<QProcess> pending_;
    quint64 generation_=0;
    bool loading_=false;
    QString error_;
    QJsonObject report_;
};
}
