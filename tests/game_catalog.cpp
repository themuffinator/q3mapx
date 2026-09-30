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
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

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
static QByteArray example() {
    return QJsonDocument(QJsonObject{{"schema_version",1},{"profiles",QJsonArray{
        QJsonObject{{"id","fixture"},{"title","Synthetic catalog"},{"base_directory","baseq3"},{"shader_directory","scripts"},
            {"bsp_ident","IBSP"},{"bsp_version",46},{"native_write",true},{"aliases",QJsonArray()},{"workflows",QJsonArray{"build"}}}}}}).toJson();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("-games")) {
#ifdef Q_OS_WIN
        _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
#endif
        const auto mode = qEnvironmentVariable("Q3MAPX_TEST_CATALOG_MODE");
        if (mode == "slow") { QTimer::singleShot(30000, &app, &QCoreApplication::quit); return app.exec(); }
        if (mode == "large") std::cout << std::string(2 * 1024 * 1024, 'x');
        else if (mode == "stderr") { std::cout << example().constData() << std::flush; std::cerr << std::string(2*1024*1024,'x'); }
        else if (mode == "mixed") { std::cout << example().constData() << std::string(600*1024,' ') << std::flush; std::cerr << std::string(600*1024,'x'); }
        else if (mode == "boundary" || mode == "over-boundary" || mode == "valid") {
            const auto bytes=example(); std::cout << bytes.constData() << std::flush;
            if (mode != "valid") std::cerr << std::string(1024*1024-bytes.size()+(mode=="over-boundary"?1:0),'x');
        }
        else std::cout << "{bad json";
        return 0;
    }
    require(argc == 2, "Expected compiler path");
    workbench::GameCatalog catalog;
    catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.profiles().size() >= 19, "Real compiler catalog failed");
    require(catalog.find("JKA-SP") && catalog.find("JKA-SP")->id == "ja", "Case-insensitive alias resolution failed");
    require(catalog.find("sof2") && catalog.find("prophecy"), "Catalog omitted previously hidden profiles");
    for (const auto& profile:catalog.profiles()) {
        require(profile.recoveryBrushOrders.contains("bsp"),"Current compiler omitted default recovery policy");
        require(profile.supportsRebuildOrder()==profile.nativeWrite,"Rebuild order capability does not match the current compiler");
    }
    for(const auto* id:{"alice","fakk2","q3-ihv","q3test44","q3test45"}) {
        const auto* profile=catalog.find(id);
        require(profile && !profile->nativeWrite && profile->workflows.contains("decompile")
            && !profile->workflows.contains("build"), "Native recovery profile advertised an unsupported writer");
    }
    require(!catalog.find("not-a-profile"), "Unknown profile was accepted");
    require(catalog.find("mohaa") && !catalog.find("mohaa")->workflows.contains("minimap"),
            "MOHAA advertised a brush-only minimap that omits terrain");
    catalog.refresh("missing-q3mapx-executable"); catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.find("quake3"), "Stale query replaced current catalog");
    for (const auto& mode : {"invalid", "large", "stderr", "mixed", "over-boundary", "slow"}) {
        qputenv("Q3MAPX_TEST_CATALOG_MODE", mode);
        catalog.refresh(QCoreApplication::applicationFilePath(), QString(mode) == "slow" ? 100 : 10000);
        finish(catalog);
        require(!catalog.error().isEmpty() && catalog.profiles().isEmpty(), "Bad or stalled query published a catalog");
        if (QStringList{"large","stderr","mixed","over-boundary"}.contains(mode)) require(catalog.error().contains("1 MiB"), "Combined output budget was not enforced");
        if (QString(mode) == "slow") require(catalog.error().contains("timed out"), "Stalled query was not terminated");
    }
    for (const auto& mode : {"valid","boundary"}) {
        qputenv("Q3MAPX_TEST_CATALOG_MODE",mode);
        catalog.refresh(QCoreApplication::applicationFilePath()); finish(catalog);
        require(catalog.error().isEmpty() && catalog.find("fixture"),"Valid catalog within the combined budget was rejected");
        require(catalog.find("fixture")->nativeWrite && !catalog.find("fixture")->supportsRebuildOrder()
                && catalog.find("fixture")->recoveryBrushOrders==QStringList{"bsp"},"Older compiler catalog incorrectly enabled rebuild order");
    }
    qputenv("Q3MAPX_TEST_CATALOG_MODE","slow"); catalog.refresh(QCoreApplication::applicationFilePath());
    qputenv("Q3MAPX_TEST_CATALOG_MODE","valid"); catalog.refresh(QCoreApplication::applicationFilePath()); finish(catalog);
    require(catalog.error().isEmpty() && catalog.find("fixture"),"Superseded child changed the current catalog");
    QEventLoop drain; QTimer probe;
    QObject::connect(&probe,&QTimer::timeout,&drain,[&] {
        for(auto* child:catalog.findChildren<QProcess*>()) if(child->state()!=QProcess::NotRunning) return;
        drain.quit();
    }); probe.start(10); QTimer::singleShot(3000,&drain,&QEventLoop::quit); drain.exec();
    for(auto* child:catalog.findChildren<QProcess*>()) require(child->state()==QProcess::NotRunning,"Superseded catalog process left running");
    qunsetenv("Q3MAPX_TEST_CATALOG_MODE");
    QProcess query; query.start(argv[1], {"-games"}); require(query.waitForFinished(15000), "Catalog parse fixture failed");
    const auto original = QJsonDocument::fromJson(query.readAllStandardOutput()).object();
    for (unsigned mutation = 0; mutation < 10; ++mutation) {
        auto object = original;
        if (mutation == 0) object["schema_version"] = 2;
        else {
            auto profiles = object["profiles"].toArray();
            auto first = profiles[0].toObject();
            if (mutation == 1) profiles.append(first);
            if (mutation == 2) { first["bsp_version"] = 1.5; profiles[0] = first; }
            if (mutation == 3) { first["aliases"] = QJsonArray{first["id"]}; profiles[0] = first; }
            if (mutation >= 4) {
                if (mutation == 4) first["recovery_brush_orders"]="rebuild";
                if (mutation == 5) first["recovery_brush_orders"]=QJsonArray{"bsp","rebuild","rebuild"};
                if (mutation == 6) first["recovery_brush_orders"]=QJsonArray{"bsp",17};
                if (mutation == 7) first["recovery_brush_orders"]=QJsonArray{"bad identifier"};
                if (mutation == 8) { QJsonArray large; for(int i=0;i<33;++i) large.append(QString("policy%1").arg(i)); first["recovery_brush_orders"]=large; }
                if (mutation == 9) first["recovery_brush_orders"]=QJsonArray{"rebuild\n"};
                profiles[0]=first;
            }
            object["profiles"] = profiles;
        }
        bool rejected = false;
        try { (void)workbench::parseGameCatalog(QJsonDocument(object).toJson()); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "Malformed catalog metadata accepted");
    }
    catalog.refresh(argv[1]); finish(catalog);
    require(catalog.error().isEmpty() && catalog.find("ja"), "Catalog did not recover after failed queries");
    std::cout << "Compiler catalog, aliases, stale queries, combined output boundaries, timeout and malformed metadata passed\n";
}
