// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "project.h"
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QStringDecoder>

namespace workbench {
class JobQueue : public QObject {
    Q_OBJECT
public:
    explicit JobQueue(QObject* parent=nullptr);
    ~JobQueue() override;
    const QVector<Job>& jobs() const { return jobs_; }
    int activeIndex() const { return active_; }
    bool running() const { return active_>=0; }
    void enqueue(const QVector<Job>& jobs);
    void start();
    void cancel();
    void clearFinished();
    QJsonObject report() const;
signals:
    void changed();
    void output(int index,const QString& text);
    void activity(const QString& text);
    void completed(int index);
    void idle();
private:
    QVector<Job> jobs_;
    QProcess* process_ = nullptr;
    QFile log_;
    QElapsedTimer timer_;
    int active_ = -1;
    bool cancelled_ = false, stopped_ = true;
    QByteArray pendingLine_;
    QStringDecoder decoder_{QStringDecoder::Utf8};
    void next();
    void read();
    void finish(int exitCode,QProcess::ExitStatus status,const QString& error={});
};
}
