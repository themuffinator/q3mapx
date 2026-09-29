// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

int main(int argc,char** argv){
    QApplication app(argc,argv); app.setApplicationName("q3mapx-workbench"); app.setApplicationVersion("0.1.0"); app.setOrganizationName("q3mapx");
    app.setStyle("Fusion");
    QCommandLineParser parser; parser.setApplicationDescription("q3mapx desktop compiler workbench"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"project","Open a saved project","file"});
    parser.addOption({"state-dir","Use a specific settings/history directory","directory"});
    parser.addOption({"render-preview","Render the widget tree to a PNG and exit (use -platform offscreen for tests)","file"});
    parser.process(app);
    const auto state=parser.isSet("state-dir") ? QDir(parser.value("state-dir")).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    try {
        workbench::Window window(state);
        if(parser.isSet("project")) window.loadProject(parser.value("project"));
        window.show();
        if(parser.isSet("render-preview")) QTimer::singleShot(100,&app,[&]{ app.exit(window.renderPreview(parser.value("render-preview")) ? 0 : 1); });
        return app.exec();
    } catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
