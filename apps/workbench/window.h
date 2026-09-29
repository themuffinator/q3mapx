// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "job_queue.h"
#include <QJsonArray>
#include <QMainWindow>
#include <QMap>
class QCheckBox; class QComboBox; class QLabel; class QLineEdit; class QListWidget; class QPlainTextEdit;
class QPushButton; class QProgressBar; class QSpinBox; class QStackedWidget; class QTableWidget; class QTreeWidget;

namespace workbench {
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(const QString& stateDirectory);
    void loadProject(const QString& path);
    bool renderPreview(const QString& path);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    QString stateDirectory_, projectPath_, theme_="dark";
    JobQueue queue_;
    QJsonArray history_;
    QStringList recordedGroups_;
    QMap<int,QString> logs_;
    QLineEdit *name_, *source_, *gameRoot_, *outputRoot_, *compiler_, *mod_, *search_;
    QComboBox *game_, *quality_, *backend_, *format_, *workflow_, *reports_;
    QSpinBox *workers_, *gpu_, *size_, *samples_;
    QCheckBox* reproducibleVis_;
    QPlainTextEdit *bspOptions_, *visOptions_, *lightOptions_, *preview_, *logView_, *reportView_, *hardware_;
    QTableWidget *jobs_, *historyView_;
    QTreeWidget* diagnostics_;
    QLabel *title_, *status_;
    QPushButton *run_, *cancel_;
    QProgressBar* progress_;
    QListWidget* navigation_;
    QStackedWidget* pages_;
    bool dirty_=false, populating_=false;
    Project project() const;
    void setProject(const Project& project);
    QWidget* configuration();
    QWidget* queuePage();
    QWidget* historyPage();
    QWidget* hardwarePage();
    QWidget* pathField(QLineEdit*& edit,const QString& placeholder,int kind);
    void updatePreview();
    void saveProject(bool saveAs);
    void enqueue(bool start);
    void refreshQueue();
    void selectJob();
    void appendOutput(int index,const QString& text);
    void recordHistory();
    void refreshHistory();
    void discoverHardware();
    void applyTheme();
    void showError(const QString& message);
    bool confirmDiscard();
};
}
