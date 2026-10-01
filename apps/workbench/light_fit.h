// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <atomic>
#include <memory>

class QThread;
namespace workbench {
struct LightFitSettings {
    QString family="point";
    int stride=2, maxLights=2, refinements=7, style=0, maxWork=200'000'000;
    double spacing=96, gamma=1, compensate=1, extraDistance=0;
    double maxRMSE=2, minImprovement=1;
    bool wolf=false, lightmapsSRGB=false, texturesSRGB=false, colorsSRGB=false;
    QJsonArray fixedLights;
    QJsonObject toJson() const;
    static LightFitSettings fromJson(const QJsonObject& object);
    QStringList validate() const;
    QJsonObject request() const;
    QStringList arguments() const;
};
struct LightFitReview {
    QString path, reportHash, sourceHash, game, family, status;
    bool accepted=false, sourceMatches=false, gameMatches=false;
    int fixedLights=0;
    QJsonObject scores, settings;
    QJsonArray lights;
    bool canApply() const { return accepted && sourceMatches && gameMatches; }
};
LightFitReview readLightFitReport(const QString& path,const QString& source,const QString& game,
                                 const std::atomic_bool* cancelled=nullptr);

// One worker at a time. New contexts cancel the old read; stale replies are discarded.
class LightFitReader : public QObject {
    Q_OBJECT
public:
    explicit LightFitReader(QObject* parent=nullptr):QObject(parent){}
    ~LightFitReader() override;
    void refresh(const QString& report,const QString& source,const QString& game);
    void cancel();
    bool loading() const { return loading_; }
    const QString& error() const { return error_; }
    const LightFitReview& result() const { return result_; }
signals:
    void changed();
private:
    QString report_,source_,game_,error_;
    LightFitReview result_;
    bool loading_=false;
    quint64 generation_=0;
    QPointer<QThread> worker_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    void start();
};
}
