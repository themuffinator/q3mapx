// SPDX-License-Identifier: GPL-3.0-or-later
#include "bsp_inspector.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition,const char* message){
    if(!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static void save(const QString& path,const QByteArray& bytes){
    QFile file(path); require(file.open(QIODevice::WriteOnly),"Cannot create inspection fixture");
    require(file.write(bytes)==bytes.size(),"Cannot write inspection fixture");
}
static QByteArray read(const QString& path){
    QFile file(path); require(file.open(QIODevice::ReadOnly),"Cannot read inspection fixture"); return file.readAll();
}
static void finish(workbench::BspInspector& inspector){
    QEventLoop loop;
    QObject::connect(&inspector,&workbench::BspInspector::changed,&loop,[&]{ if(!inspector.loading()) loop.quit(); });
    QTimer::singleShot(15000,&loop,&QEventLoop::quit);
    if(inspector.loading()) loop.exec();
    require(!inspector.loading(),"Inspection query did not finish");
}
static QByteArray header(const char* ident,int version,int prefix,int lumps){
    QByteArray data(prefix+8*lumps,0); std::memcpy(data.data(),ident,4);
    qToLittleEndian<qint32>(version,data.data()+4); return data;
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("-inspect")) {
        const auto mode=qEnvironmentVariable("Q3MAPX_TEST_INSPECT_MODE");
        if(mode=="slow") { QTimer::singleShot(30000,&app,&QCoreApplication::quit); return app.exec(); }
        if(mode=="large") std::cout << std::string(2*1024*1024,'x');
        else if(mode=="stderr") std::cerr << std::string(2*1024*1024,'x');
        else if(mode=="wrong-exit" || mode=="wrong-file") {
            auto report=QJsonDocument::fromJson(read(qEnvironmentVariable("Q3MAPX_TEST_INSPECT_REPLY"))).object();
            if(mode=="wrong-file") report["file"]="another input.bsp";
            std::cout << QJsonDocument(report).toJson().constData();
            return mode=="wrong-exit"?1:0;
        } else std::cout << "invalid json";
        return 0;
    }
    require(argc==3,"Expected compiler and fixture directory");
    const QDir root(QDir(argv[2]).absolutePath()); require(QDir().mkpath(root.path()),"Cannot create fixture directory");
    const auto file=root.filePath("inspected map.bsp");
    auto bytes=header("IBSP",46,8,17); save(file,bytes);
    workbench::BspInspector inspector;
    inspector.inspect(argv[1],file); finish(inspector);
    require(inspector.error().isEmpty() && inspector.report()["valid"].toBool(),"Real inspector failed");
    require(!inspector.report()["geometry_validated"].toBool() && inspector.report()["ambiguous_game"].toBool(),"Inspection scope/ambiguity lost");
    require(inspector.report()["layouts"].toArray()[0].toObject()["lumps"].toArray().size()==17,"Lump directory omitted");
    const auto original=inspector.report();
    const auto reply=root.filePath("reply.json"); save(reply,QJsonDocument(original).toJson());
    qputenv("Q3MAPX_TEST_INSPECT_REPLY",reply.toUtf8());
    require(read(file)==bytes,"Inspection modified its source");
    inspector.inspect(argv[1],file,"ja"); finish(inspector);
    require(inspector.error().isEmpty() && !inspector.report()["valid"].toBool() && !inspector.report()["errors"].toArray().isEmpty(),"Profile mismatch lost its structured diagnostic");
    bytes=header("IBSP",47,8,18); save(file,bytes);
    inspector.inspect(argv[1],file); finish(inspector);
    require(inspector.error().isEmpty() && inspector.report()["layouts"].toArray().size()==2,"Ambiguous 47 layouts omitted");
    bytes=header("2015",19,12,28); save(file,bytes);
    inspector.inspect(argv[1],file,"mohaa"); finish(inspector);
    require(inspector.error().isEmpty() && inspector.report()["valid"].toBool()
        && inspector.report()["layouts"].toArray()[0].toObject()["lumps"].toArray().size()==28,"Native MOHAA inspection failed");
    bytes=header("IBSP",46,8,17); bytes.append("bad",3);
    qToLittleEndian<qint32>(144,bytes.data()+8+2*8); qToLittleEndian<qint32>(3,bytes.data()+12+2*8); save(file,bytes);
    inspector.inspect(argv[1],file); finish(inspector);
    require(inspector.error().isEmpty() && !inspector.report()["valid"].toBool(),"Malformed directory was not retained as an invalid report");
    require(read(file)==bytes,"Invalid inspection modified its source");
    inspector.inspect(argv[1],root.filePath("missing.bsp")); finish(inspector);
    require(inspector.error().isEmpty() && !inspector.report().isEmpty() && !inspector.report()["valid"].toBool(),"Missing input diagnostic discarded");
    save(file,header("IBSP",46,8,17));
    for(const auto* mode:{"invalid","large","stderr","slow","wrong-exit","wrong-file"}) {
        qputenv("Q3MAPX_TEST_INSPECT_MODE",mode);
        inspector.inspect(QCoreApplication::applicationFilePath(),file,{},QString(mode)=="slow"?100:10000); finish(inspector);
        require(!inspector.error().isEmpty() && inspector.report().isEmpty(),"Invalid compiler response published an inspection");
        if(QString(mode)=="large" || QString(mode)=="stderr") require(inspector.error().contains("1 MiB"),"Output size guard failed");
        if(QString(mode)=="slow") require(inspector.error().contains("timed out"),"Inspection timeout failed");
    }
    qputenv("Q3MAPX_TEST_INSPECT_MODE","slow");
    inspector.inspect(QCoreApplication::applicationFilePath(),file);
    inspector.inspect(argv[1],file); finish(inspector);
    require(inspector.error().isEmpty() && inspector.report()["valid"].toBool(),"Stale query replaced latest inspection");
    inspector.inspect(QCoreApplication::applicationFilePath(),file); inspector.reset("cancelled");
    QEventLoop events; QTimer::singleShot(100,&events,&QEventLoop::quit); events.exec();
    require(!inspector.loading() && inspector.report().isEmpty() && inspector.error()=="cancelled","Cancelled query changed its result");
    inspector.inspect("missing-q3mapx-inspector",file); finish(inspector);
    require(!inspector.error().isEmpty() && inspector.report().isEmpty(),"Startup failure was hidden");
    qunsetenv("Q3MAPX_TEST_INSPECT_MODE"); qunsetenv("Q3MAPX_TEST_INSPECT_REPLY");
    for(int mutation=0;mutation<10;++mutation) {
        auto report=original;
        if(mutation==0) report["schema_version"]=1.5;
        if(mutation==1) report["geometry_validated"]=true;
        if(mutation==2) report["file_bytes"]=-1;
        if(mutation==3) report["valid"]="true";
        if(mutation==4) report["errors"]=QJsonArray{"Unexpected error in a successful report"};
        if(mutation==5) report["ambiguous_game"]=false;
        if(mutation>=6) {
            auto layouts=report["layouts"].toArray(); auto layout=layouts[0].toObject();
            if(mutation==6) layouts.append(layout);
            else if(mutation==8) {
                auto lumps=layout["lumps"].toArray();
                while(lumps.size()<65) lumps.append(lumps[0]);
                layout["lumps"]=lumps; layouts[0]=layout;
            } else {
                auto lumps=layout["lumps"].toArray(); auto lump=lumps[1].toObject(); lump["records"]=12;
                if(mutation==9) { lump=lumps[1].toObject(); lump["offset"]=1.5; }
                lumps[1]=lump; layout["lumps"]=lumps; layouts[0]=layout;
            }
            report["layouts"]=layouts;
        }
        bool rejected=false;
        try { (void)workbench::parseBspInspection(QJsonDocument(report).toJson()); }
        catch(const std::exception&) { rejected=true; }
        require(rejected,"Malformed inspection metadata accepted");
    }
    inspector.inspect(argv[1],file); finish(inspector);
    require(inspector.error().isEmpty() && inspector.report()["valid"].toBool(),"Inspector failed to recover after errors");
    std::cout << "Real formats, ambiguous layouts, invalid inputs, bounded queries, stale replies, cancellation and schema checks passed\n";
}
