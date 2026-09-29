// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QElapsedTimer>
#include <iostream>
#include <cstring>

int main(int argc,char** argv){
    // Qt may show a modal version dialog for a Windows GUI process without
    // redirected output, even on the offscreen platform. Version queries need
    // no GUI initialization and must terminate in scripts as well as terminals.
    for(int i=1;i<argc;++i) if(std::strcmp(argv[i],"--version")==0 || std::strcmp(argv[i],"-v")==0) {
        std::cout << "q3mapx-workbench " Q3MAPX_VERSION "\n"; return 0;
    }
    QApplication app(argc,argv); app.setApplicationName("q3mapx-workbench"); app.setApplicationVersion(Q3MAPX_VERSION); app.setOrganizationName("q3mapx");
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
        QTimer previewTimer; QElapsedTimer previewElapsed;
        if(parser.isSet("render-preview")) {
            previewElapsed.start();
            QObject::connect(&previewTimer,&QTimer::timeout,&app,[&]{
                if(window.discoveringGames() && previewElapsed.elapsed()<12000) return;
                previewTimer.stop(); app.exit(window.renderPreview(parser.value("render-preview")) ? 0 : 1);
            });
            previewTimer.start(50);
        }
        return app.exec();
    } catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
