// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTimer>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    require(argc == 3, "Expected project and isolated settings directory");
    const auto project = workbench::Project::load(argv[1]);
    QFile source(project.source); require(source.open(QIODevice::ReadOnly), "Cannot read fixture source");
    const auto original = source.readAll(); source.close();
    workbench::Window window(argv[2]);
    window.loadProject(argv[1]);
    auto* queue = window.findChild<workbench::JobQueue*>();
    require(queue, "Window has no build queue");
    bool launched = false;
    QTimer ready;
    QObject::connect(&ready, &QTimer::timeout, &app, [&] {
        if (window.discoveringGames() || launched) return;
        auto* profiles = window.findChild<QComboBox*>("gameProfiles");
        require(profiles && profiles->count() >= 19, "Window did not use the compiler catalog");
        require(profiles->currentText() == project.game, "Catalog replaced the saved project selection");
        auto* button=window.findChild<QPushButton*>("primary");
        require(button, "Run button missing");
        profiles->setCurrentText("alice");
        require(!button->isEnabled(), "Recovery-only profile enabled compilation in the window");
        profiles->setCurrentText(project.game);
        require(button->isEnabled(), "Returning to a writable profile did not enable build");
        QAction* run = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence(Qt::Key_F5)) run = action;
        require(run, "Run workflow action missing");
        launched = true;
        // Invoke the application action directly in an offscreen widget tree.
        // No mouse/keyboard events or operating-system input are synthesized.
        run->trigger();
        require(queue->jobs().size() == 3, "Window failed to enqueue the full pipeline");
    });
    QObject::connect(queue, &workbench::JobQueue::idle, &app, [&] {
        if (!launched) return;
        for (const auto& job : queue->jobs())
            require(job.state == "Succeeded", "A window-launched compiler stage failed");
        require(source.open(QIODevice::ReadOnly) && source.readAll() == original, "Window modified source input");
        require(QFileInfo(queue->jobs().back().outputPath).size() > 100, "No BSP produced from window action");
        std::cout << "Actual window catalog, saved selection and build action passed without input injection\n";
        app.quit();
    });
    QTimer::singleShot(30000, &app, [] { require(false, "Window catalog/build action timed out"); });
    ready.start(25);
    return app.exec();
}
