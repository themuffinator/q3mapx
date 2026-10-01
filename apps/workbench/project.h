// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include "light_fit.h"

namespace workbench {
struct Project {
    QString name = "Untitled project";
    QString source, gameRoot, outputRoot, compiler;
    QString game = "quake3", mod, quality = "balanced", backend = "auto", mapFormat = "map_220";
    QString brushOrder = "bsp";
    QString detailPolicy = "legacy", groupPolicy = "none";
    int detailWorkLimit = 50'000'000, groupWorkLimit = 50'000'000;
    int workers = 0, gpuDevice = -1, minimapSize = 1024, minimapSamples = 4, meshPatchSteps = 8;
    bool reproducibleVis = true;
    LightFitSettings lightFit;
    bool applyLightReport = false;
    QString lightReport, lightReportHash;
    QStringList bspOptions, visOptions, lightOptions;
    QJsonObject toJson() const;
    static Project fromJson(const QJsonObject& object);
    static Project load(const QString& path);
    void save(const QString& path) const;
    QStringList validate(const QString& workflow) const;
};
struct Job {
    QString group, label, program, directory, logPath, outputPath, state = "Queued", error;
    QStringList arguments;
    qint64 elapsedMs = 0;
    int exitCode = -1;
    QJsonObject toJson() const;
};
QVector<Job> buildPlan(const Project& project, const QString& workflow, const QString& directory);
QString prepareRun(const Project& project, const QString& workflow);
QString displayCommand(const Job& job);
void saveJson(const QString& path, const QJsonObject& object);
}
