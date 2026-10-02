// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <atomic>
#include <memory>
class QThread;
namespace workbench {
struct PatchDecision {
    QString basis, status, location, shape;
    QJsonObject details;
};
struct PatchReview {
    QString path, reportHash, input, output, game, policy="none", colors="none";
    quint64 archived=0, fitted=0, native=0, skipped=0, omitted=0;
    bool binding=false;
    QVector<PatchDecision> decisions;
};
PatchReview readPatchReview(const QString& path,const std::atomic_bool* cancelled=nullptr);
class PatchReviewReader : public QObject {
    Q_OBJECT
public:
    explicit PatchReviewReader(QObject* parent=nullptr):QObject(parent){}
    ~PatchReviewReader() override;
    void refresh(const QString& path);
    void cancel();
    bool loading() const { return loading_; }
    const PatchReview& result() const { return result_; }
    const QString& error() const { return error_; }
signals:
    void changed();
private:
    QString path_,error_;
    PatchReview result_;
    bool loading_=false;
    quint64 generation_=0;
    QPointer<QThread> worker_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    void start();
};
}
