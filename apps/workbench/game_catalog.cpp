// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_catalog.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <memory>
#include <stdexcept>

namespace workbench {
static constexpr qsizetype maxCatalogBytes = 1024 * 1024;
static void invalid() { throw std::runtime_error("Compiler returned an invalid game catalog"); }

QVector<GameProfile> parseGameCatalog(const QByteArray& bytes) {
    if (bytes.size() > maxCatalogBytes) invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) invalid();
    const auto object = document.object();
    if (object.value("schema_version").toInt(-1) != 1 || !object.value("profiles").isArray()) invalid();
    const auto array = object.value("profiles").toArray();
    if (array.isEmpty() || array.size() > 256) invalid();
    QVector<GameProfile> profiles;
    QSet<QString> names;
    const QRegularExpression identifier("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$");
    const auto string = [](const QJsonObject& source, const char* key) {
        const auto value = source.value(key);
        if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 512
            || value.toString().contains(QChar('\0'))) invalid();
        return value.toString();
    };
    const auto name = [&](const QString& value) {
        if (!identifier.match(value).hasMatch() || names.contains(value.toCaseFolded())) invalid();
        names.insert(value.toCaseFolded());
    };
    for (const auto& value : array) {
        if (!value.isObject()) invalid();
        const auto source = value.toObject();
        GameProfile profile;
        profile.id = string(source, "id"); name(profile.id);
        profile.title = string(source, "title");
        profile.baseDirectory = string(source, "base_directory");
        profile.shaderDirectory = string(source, "shader_directory");
        profile.bspIdent = string(source, "bsp_ident");
        const auto version = source.value("bsp_version");
        if (profile.bspIdent.size() != 4 || !version.isDouble() || version.toInt(-1) < 0
            || version.toDouble() != version.toInt() || !source.value("native_write").isBool()
            || !source.value("aliases").isArray() || !source.value("workflows").isArray()) invalid();
        profile.bspVersion = version.toInt();
        profile.nativeWrite = source.value("native_write").toBool();
        const auto aliases = source.value("aliases").toArray();
        if (aliases.size() > 64) invalid();
        for (const auto& alias : aliases) {
            if (!alias.isString()) invalid();
            name(alias.toString()); profile.aliases << alias.toString();
        }
        const auto workflows = source.value("workflows").toArray();
        if (workflows.size() > 32) invalid();
        for (const auto& workflow : workflows) {
            if (!workflow.isString() || !identifier.match(workflow.toString()).hasMatch()
                || profile.workflows.contains(workflow.toString())) invalid();
            profile.workflows << workflow.toString();
        }
        profiles.append(profile);
    }
    return profiles;
}

const GameProfile* GameCatalog::find(const QString& name) const {
    for (const auto& profile : profiles_) {
        if (profile.id.compare(name, Qt::CaseInsensitive) == 0) return &profile;
        for (const auto& alias : profile.aliases)
            if (alias.compare(name, Qt::CaseInsensitive) == 0) return &profile;
    }
    return nullptr;
}

void GameCatalog::refresh(const QString& compiler, int timeoutMs) {
    const auto generation = ++generation_;
    if (pending_) pending_->kill();
    loading_ = true; error_.clear(); profiles_.clear(); emit changed();
    struct Reply { QByteArray bytes; QString failure; };
    auto reply = std::make_shared<Reply>();
    auto* process = new QProcess(this);
    pending_ = process;
    const auto collect = [process, reply] {
        reply->bytes += process->readAllStandardOutput();
        process->readAllStandardError();
        if (reply->bytes.size() > maxCatalogBytes) {
            reply->bytes.truncate(maxCatalogBytes);
            reply->failure = "Compiler game catalog exceeds 1 MiB";
            process->kill();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, collect);
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::finished, this, [this, process, generation, reply, collect](int code, QProcess::ExitStatus status) {
        collect();
        if (generation == generation_) {
            loading_ = false;
            if (!reply->failure.isEmpty()) error_ = reply->failure;
            else if (code != 0 || status != QProcess::NormalExit) error_ = "Compiler does not provide a game catalog";
            else {
                try { profiles_ = parseGameCatalog(reply->bytes); }
                catch (const std::exception& error) { error_ = QString::fromUtf8(error.what()); }
            }
            pending_ = nullptr; emit changed();
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (generation == generation_) {
            loading_ = false; error_ = process->errorString(); pending_ = nullptr; emit changed();
        }
        process->deleteLater();
    });
    QTimer::singleShot(qBound(1, timeoutMs, 60000), process, [process, reply] {
        if (process->state() != QProcess::NotRunning) {
            reply->failure = "Compiler game catalog query timed out"; process->kill();
        }
    });
    process->start(compiler, {"-games"});
}
} // namespace workbench
