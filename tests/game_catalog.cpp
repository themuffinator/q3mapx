// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_catalog.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static void finish(workbench::GameCatalog& catalog) {
    QEventLoop loop;
    QObject::connect(&catalog, &workbench::GameCatalog::changed, &loop, [&] { if (!catalog.loading()) loop.quit(); });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    if (catalog.loading()) loop.exec();
    require(!catalog.loading(), "Catalog query did not finish");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("-games")) {
        const auto mode = qEnvironmentVariable("Q3MAPX_TEST_CATALOG_MODE");
        if (mode == "slow") { QTimer::singleShot(30000, &app, &QCoreApplication::quit); return app.exec(); }
        if (mode == "large") std::cout << std::string(2 * 1024 * 1024, 'x');
        else std::cout << "{bad json";
        return 0;
    }
    require(argc == 2, "Expected compiler path");
    workbench::GameCatalog catalog;
    catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.profiles().size() >= 19, "Real compiler catalog failed");
    require(catalog.find("JKA-SP") && catalog.find("JKA-SP")->id == "ja", "Case-insensitive alias resolution failed");
    require(catalog.find("sof2") && catalog.find("prophecy"), "Catalog omitted previously hidden profiles");
    require(!catalog.find("not-a-profile"), "Unknown profile was accepted");
    catalog.refresh("missing-q3mapx-executable"); catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.find("quake3"), "Stale query replaced current catalog");
    for (const auto& mode : {"invalid", "large", "slow"}) {
        qputenv("Q3MAPX_TEST_CATALOG_MODE", mode);
        catalog.refresh(QCoreApplication::applicationFilePath(), QString(mode) == "slow" ? 100 : 10000);
        finish(catalog);
        require(!catalog.error().isEmpty() && catalog.profiles().isEmpty(), "Bad or stalled query published a catalog");
        if (QString(mode) == "large") require(catalog.error().contains("1 MiB"), "Oversized response was not bounded");
        if (QString(mode) == "slow") require(catalog.error().contains("timed out"), "Stalled query was not terminated");
    }
    qunsetenv("Q3MAPX_TEST_CATALOG_MODE");
    QProcess query; query.start(argv[1], {"-games"}); require(query.waitForFinished(15000), "Catalog parse fixture failed");
    const auto original = QJsonDocument::fromJson(query.readAllStandardOutput()).object();
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto object = original;
        if (mutation == 0) object["schema_version"] = 2;
        else {
            auto profiles = object["profiles"].toArray();
            auto first = profiles[0].toObject();
            if (mutation == 1) profiles.append(first);
            if (mutation == 2) { first["bsp_version"] = 1.5; profiles[0] = first; }
            if (mutation == 3) { first["aliases"] = QJsonArray{first["id"]}; profiles[0] = first; }
            object["profiles"] = profiles;
        }
        bool rejected = false;
        try { (void)workbench::parseGameCatalog(QJsonDocument(object).toJson()); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "Malformed catalog metadata accepted");
    }
    catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.find("ja"), "Catalog did not recover after failed queries");
    std::cout << "Compiler catalog, aliases, stale queries, response bounds, timeout and malformed metadata passed\n";
}
