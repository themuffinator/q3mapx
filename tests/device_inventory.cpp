// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_inventory.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

static void require(bool condition,const char* message){
    if(!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static QJsonObject example(){
    return {{"schema_version",1},{"reason",""},{"devices",QJsonArray{
        QJsonObject{{"index",0},{"name","Example discrete GPU"},{"vendor","Example vendor"},{"version","OpenCL 3.0 test driver"},
            {"memory_bytes",qint64(8)*1024*1024*1024},{"compute_units",24},{"unified_memory",false}},
        QJsonObject{{"index",1},{"name","Example shared GPU <b>plain text</b>"},{"vendor","Second vendor"},{"version","OpenCL 1.2 test driver"},
            {"memory_bytes",qint64(4)*1024*1024*1024},{"compute_units",12},{"unified_memory",true}}}}};
}
static QJsonObject empty(){
    return {{"schema_version",1},{"reason","OpenCL disabled at build time"},{"devices",QJsonArray()}};
}
static void finish(workbench::DeviceInventory& inventory){
    QEventLoop loop;
    QObject::connect(&inventory,&workbench::DeviceInventory::changed,&loop,[&]{ if(!inventory.loading()) loop.quit(); });
    QTimer::singleShot(20000,&loop,&QEventLoop::quit);
    if(inventory.loading()) loop.exec();
    require(!inventory.loading(),"Device query did not finish");
}
static void drain(workbench::DeviceInventory& inventory){
    // Confirm cancelled/superseded children actually leave, including cancellation during startup.
    QEventLoop events;
    QTimer check; QObject::connect(&check,&QTimer::timeout,&events,[&]{
        for(auto* child:inventory.findChildren<QProcess*>()) if(child->state()!=QProcess::NotRunning) return;
        events.quit();
    }); check.start(10); QTimer::singleShot(3000,&events,&QEventLoop::quit); events.exec();
    for(auto* child:inventory.findChildren<QProcess*>()) require(child->state()==QProcess::NotRunning,"Abandoned device query still running");
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("-devices")) {
#ifdef Q_OS_WIN
        // Boundary cases count transport bytes; CRT newline conversion would add CRs.
        _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
#endif
        const auto mode=qEnvironmentVariable("Q3MAPX_TEST_DEVICES_MODE");
        if(mode=="slow") { QTimer::singleShot(30000,&app,&QCoreApplication::quit); return app.exec(); }
        const auto reply=QJsonDocument(mode=="empty"?empty():example()).toJson();
        if(mode=="invalid") std::cout << "invalid json";
        else if(mode=="large") std::cout << std::string(2*1024*1024,'x');
        else if(mode=="stderr") std::cerr << std::string(2*1024*1024,'x');
        else if(mode=="mixed") { std::cout << std::string(600*1024,' ') << std::flush; std::cerr << std::string(600*1024,'x'); }
        else {
            std::cout << reply.constData();
            if(mode=="boundary") std::cout << std::string(1024*1024-reply.size(),' ');
            if(mode=="messages") std::cerr << "Driver diagnostic\n" << std::string(16000,'x');
            if(mode=="exact-messages") std::cerr << std::string(8192,'x');
        }
        return mode=="wrong-exit"?7:0;
    }
    require(argc==3,"Expected compiler and test directory");
    const QDir root(QDir(argv[2]).absolutePath()); require(QDir().mkpath(root.path()),"Cannot create device query output directory");
    workbench::DeviceInventory inventory;
    inventory.refresh(argv[1]); finish(inventory);
    require(inventory.error().isEmpty() && !inventory.report().isEmpty(),"Real compiler inventory failed");
    QFile native(root.filePath("native-devices.json")); require(native.open(QIODevice::WriteOnly),"Cannot record native device inventory");
    const auto nativeBytes=QJsonDocument(inventory.report()).toJson();
    require(native.write(nativeBytes)==nativeBytes.size(),"Cannot save native inventory"); native.close();
    const auto fake=QCoreApplication::applicationFilePath();
    QJsonArray cases;
    for(const auto* mode:{"valid","empty","boundary","messages","exact-messages","invalid","large","stderr","mixed","slow","wrong-exit"}) {
        qputenv("Q3MAPX_TEST_DEVICES_MODE",mode);
        inventory.refresh(fake,QString(mode)=="slow"?100:10000); finish(inventory);
        const bool good=QStringList{"valid","empty","boundary","messages","exact-messages"}.contains(mode);
        if(inventory.error().isEmpty()!=good) std::cerr << "Mode " << mode << ": " << inventory.error().toStdString() << '\n';
        require(inventory.error().isEmpty()==good,"Unexpected query error state");
        if(good) require(inventory.report()==(QString(mode)=="empty"?empty():example()),"Valid device inventory was changed or discarded");
        else require(inventory.report().isEmpty(),"Failed query published device data");
        if(QStringList{"large","stderr","mixed"}.contains(mode)) require(inventory.error().contains("1 MiB"),"Combined output ceiling failed");
        if(QString(mode)=="slow") require(inventory.error().contains("timed out"),"Timeout was not reported");
        if(QString(mode)=="wrong-exit") require(inventory.error().contains("exit 7"),"Exit status was hidden");
        if(QString(mode)=="messages") require(inventory.diagnostics().startsWith("Driver diagnostic") && inventory.diagnostics().contains("first 8 KiB"),"Bounded diagnostics missing");
        if(QString(mode)=="exact-messages") require(inventory.diagnostics().size()==8192,"Exact diagnostic limit falsely marked truncated");
        require(inventory.diagnostics().size()<8300,"Too much diagnostic output retained");
        cases.append(QJsonObject{{"case",mode},{"passed",true},{"error",inventory.error()}});
    }
    qputenv("Q3MAPX_TEST_DEVICES_MODE","slow");
    inventory.refresh(fake);
    // A direct caller can supersede a query even while the UI disables its refresh button.
    qputenv("Q3MAPX_TEST_DEVICES_MODE","empty"); inventory.refresh(fake); finish(inventory); drain(inventory);
    require(inventory.report()==empty() && inventory.error().isEmpty(),"Superseded result replaced current devices");
    inventory.refresh(root.filePath("missing-compiler")); inventory.refresh(fake); finish(inventory); drain(inventory);
    require(inventory.report()==empty() && inventory.error().isEmpty(),"Stale startup error replaced current devices");
    qputenv("Q3MAPX_TEST_DEVICES_MODE","slow");
    inventory.refresh(fake); inventory.reset("cancelled"); drain(inventory);
    require(!inventory.loading() && inventory.report().isEmpty() && inventory.error()=="cancelled","Cancelled query changed its result");
    inventory.refresh(fake);
    QTimer::singleShot(100,&inventory,[&]{ for(auto* child:inventory.findChildren<QProcess*>()) child->kill(); });
    finish(inventory);
    require(inventory.report().isEmpty() && inventory.error().contains("crashed"),"Abnormal exit was hidden");
    inventory.refresh(root.filePath("missing-compiler")); finish(inventory);
    require(!inventory.error().isEmpty() && inventory.report().isEmpty(),"Failed process startup was hidden");
    inventory.refresh("  "); require(!inventory.loading() && !inventory.error().isEmpty(),"Empty compiler path accepted");
    qunsetenv("Q3MAPX_TEST_DEVICES_MODE");
    using Mutation=std::function<void(QJsonObject&)>;
    const std::vector<Mutation> rootMutations{
        [](auto& r){ r["schema_version"]=2; }, [](auto& r){ r["schema_version"]=1.5; },
        [](auto& r){ r.remove("reason"); }, [](auto& r){ r["reason"]=true; },
        [](auto& r){ r["reason"]=QString(16385,'x'); }, [](auto& r){ r["devices"]=false; },
        [](auto& r){ r["devices"]=QJsonArray{42}; },
        [](auto& r){ auto a=r["devices"].toArray(); a.append(a[0]); r["devices"]=a; },
        [](auto& r){ auto a=r["devices"].toArray(); while(a.size()<=1024) { auto d=a[0].toObject(); d["index"]=int(a.size()); a.append(d); } r["devices"]=a; }};
    const std::vector<Mutation> deviceMutations{
        [](auto& d){ d["index"]=-1; }, [](auto& d){ d["index"]=0.5; }, [](auto& d){ d["index"]=qint64(1)<<40; },
        [](auto& d){ d.remove("name"); }, [](auto& d){ d["name"]=42; }, [](auto& d){ d["vendor"]=QString(4097,'x'); },
        [](auto& d){ d["version"]=QString(QChar('\0')); }, [](auto& d){ d["unified_memory"]=1; },
        [](auto& d){ d["memory_bytes"]=-1; }, [](auto& d){ d["memory_bytes"]=1.5; },
        [](auto& d){ d["memory_bytes"]="8589934592"; }, [](auto& d){ d["memory_bytes"]=1e30; },
        [](auto& d){ d["compute_units"]=-1; }, [](auto& d){ d["compute_units"]=qint64(1)<<32; }};
    auto rejected=[](const QByteArray& bytes){
        bool caught=false; try { (void)workbench::parseDeviceInventory(bytes); } catch(const std::exception&) { caught=true; }
        require(caught,"Malformed device schema accepted");
    };
    for(const auto& mutate:rootMutations) { auto report=example(); mutate(report); rejected(QJsonDocument(report).toJson()); }
    for(const auto& mutate:deviceMutations) {
        auto report=example(); auto devices=report["devices"].toArray(); auto device=devices[0].toObject();
        mutate(device); devices[0]=device; report["devices"]=devices; rejected(QJsonDocument(report).toJson());
    }
    for(const auto& bytes:{QByteArray(),QByteArray("[]"),QByteArray("{}")}) rejected(bytes);
    auto oversized=QJsonDocument(example()).toJson(); oversized.append(QByteArray(1024*1024+1-oversized.size(),' ')); rejected(oversized);
    inventory.refresh(fake); finish(inventory); require(inventory.report()==example(),"Device query did not recover after errors");
    drain(inventory);
    QJsonObject evidence{{"schema_version",1},{"query_cases",cases},{"schema_rejections",int(rootMutations.size()+deviceMutations.size()+4)},
        {"supersession",true},{"stale_start_failure",true},{"cancellation",true},{"crash",true},{"failed_start",true},
        {"empty_compiler",true},{"recovery_after_errors",true},{"abandoned_children_terminated",true}};
    QFile log(root.filePath("checks.json")); require(log.open(QIODevice::WriteOnly),"Cannot open test evidence");
    const auto data=QJsonDocument(evidence).toJson(); require(log.write(data)==data.size(),"Cannot save test evidence");
    std::cout << "Real/empty inventories, strict schema, combined output bounds, diagnostics, timeout, cancellation and stale replies passed\n";
}
