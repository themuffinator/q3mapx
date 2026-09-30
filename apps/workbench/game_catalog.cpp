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
static void stop(QProcess* process) {
    process->closeReadChannel(QProcess::StandardOutput);
    process->closeReadChannel(QProcess::StandardError);
    process->kill();
}

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
    const QRegularExpression identifier("\\A[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}\\z");
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
        const auto policies=[&](const char* key, QStringList& destination) {
            if (!source.contains(key)) return;
            if (!source[key].isArray()) invalid();
            const auto values=source[key].toArray();
            if (values.size()>32) invalid();
            destination.clear();
            for (const auto& policy:values) {
                if (!policy.isString() || !identifier.match(policy.toString()).hasMatch()
                    || destination.contains(policy.toString())) invalid();
                destination << policy.toString();
            }
        };
        policies("recovery_brush_orders",profile.recoveryBrushOrders);
        policies("recovery_detail_policies",profile.recoveryDetailPolicies);
        policies("recovery_group_policies",profile.recoveryGroupPolicies);
        profiles.append(profile);
    }
    return profiles;
}

QString recoverySupportError(const GameProfile* profile, const QString& order, const QString& detail, const QString& groups) {
    // Resolve grouping first: its active UI selection locks BSP order out.
    if (groups=="surfaces") {
        if (!profile || !profile->supportsSurfaceGroups())
            return "Surface group inference is unavailable for this compiler and profile. Select Flat world geometry or a supported compiler/profile.";
        if (order!="rebuild") return "Surface grouping requires Rebuild brush order.";
    }
    if (detail=="cells" && (!profile || !profile->supportsCellDetail()))
        return "Cell detail inference is unavailable for this compiler and profile. Select Legacy detail flags or a supported compiler/profile.";
    if (order=="rebuild") {
        if (!profile || !profile->nativeWrite)
            return "Rebuild brush order requires a verified game profile with BSP writing support. Select BSP order for this recovery.";
        if (!profile->supportsRebuildOrder())
            return "The selected compiler does not advertise rebuild-order recovery. Update the compiler or select BSP order.";
    }
    return {};
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
    if (pending_) stop(pending_);
    loading_ = true; error_.clear(); profiles_.clear();
    struct Reply { QByteArray bytes; qint64 total=0; QString failure; };
    auto reply = std::make_shared<Reply>();
    auto* process = new QProcess(this);
    pending_ = process;
    const auto collect = [process, reply] {
        if (!reply->failure.isEmpty()) return;
        for (const auto channel : {QProcess::StandardOutput,QProcess::StandardError}) {
            process->setReadChannel(channel);
            const auto bytes=process->read(maxCatalogBytes-reply->total+1);
            reply->total+=bytes.size();
            if (channel==QProcess::StandardOutput) reply->bytes+=bytes;
            if (reply->total>maxCatalogBytes) {
                reply->failure="Compiler game catalog exceeds 1 MiB (stdout and stderr combined)";
                stop(process); return;
            }
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, collect);
    connect(process, &QProcess::readyReadStandardError, this, collect);
    connect(process, &QProcess::started, this, [this,process,generation] {
        if (generation!=generation_) stop(process);
    });
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
    connect(process, &QProcess::errorOccurred, this, [this, process, generation, reply](QProcess::ProcessError error) {
        if (error==QProcess::ReadError) { reply->failure="Cannot read compiler game catalog"; stop(process); }
        if (error != QProcess::FailedToStart) return;
        if (generation == generation_) {
            loading_ = false; error_ = process->errorString(); pending_ = nullptr; emit changed();
        }
        process->deleteLater();
    });
    QTimer::singleShot(qBound(1, timeoutMs, 60000), process, [process, reply] {
        if (process->state() != QProcess::NotRunning) {
            if (reply->failure.isEmpty()) reply->failure = "Compiler game catalog query timed out";
            stop(process);
        }
    });
    process->start(compiler, {"-games"});
    emit changed();
}
} // namespace workbench
