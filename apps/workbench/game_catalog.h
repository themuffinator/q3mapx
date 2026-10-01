// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QProcess;
namespace workbench {
struct GameProfile {
    QString id, title, baseDirectory, shaderDirectory, bspIdent;
    QStringList aliases, workflows;
    QStringList recoveryBrushOrders{"bsp"}; // Catalogs before this option retain ordinary export.
    QStringList recoveryDetailPolicies{"legacy"}, recoveryGroupPolicies{"none"};
    int bspVersion = 0;
    bool nativeWrite = false;
    bool recoveryLightProposals = false;
    bool supportsRebuildOrder() const { return nativeWrite && recoveryBrushOrders.contains("rebuild"); }
    bool supportsCellDetail() const { return nativeWrite && recoveryDetailPolicies.contains("cells"); }
    bool supportsSurfaceGroups() const { return supportsRebuildOrder() && recoveryGroupPolicies.contains("surfaces"); }
};
QVector<GameProfile> parseGameCatalog(const QByteArray& bytes);
QString recoverySupportError(const GameProfile* profile, const QString& order, const QString& detail, const QString& groups);

class GameCatalog : public QObject {
    Q_OBJECT
public:
    explicit GameCatalog(QObject* parent = nullptr) : QObject(parent) {}
    void refresh(const QString& compiler, int timeoutMs = 10000);
    bool loading() const { return loading_; }
    const QString& error() const { return error_; }
    const QVector<GameProfile>& profiles() const { return profiles_; }
    const GameProfile* find(const QString& name) const;
signals:
    void changed();
private:
    QPointer<QProcess> pending_;
    quint64 generation_ = 0;
    bool loading_ = false;
    QString error_;
    QVector<GameProfile> profiles_;
};
} // namespace workbench
