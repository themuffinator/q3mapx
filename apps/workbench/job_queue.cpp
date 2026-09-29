// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_queue.h"
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <algorithm>

namespace workbench {
JobQueue::JobQueue(QObject* parent):QObject(parent){}
JobQueue::~JobQueue(){
    if (process_) { process_->disconnect(this); process_->kill(); process_->waitForFinished(2000); }
}
void JobQueue::enqueue(const QVector<Job>& jobs){ jobs_+=jobs; emit changed(); }
void JobQueue::start(){ if (running()) return; stopped_=false; next(); }
void JobQueue::next(){
    if (stopped_ || running()) return;
    int index=-1;
    for (int i=0;i<jobs_.size();++i) if (jobs_[i].state=="Queued") { index=i; break; }
    if (index<0) { stopped_=true; emit idle(); return; }
    active_=index; cancelled_=false; pendingLine_.clear(); decoder_.resetState();
    auto& job=jobs_[active_]; job.state="Running";
    log_.setFileName(job.logPath);
    timer_.start();
    if (!log_.open(QIODevice::WriteOnly|QIODevice::Truncate)) { finish(-1,QProcess::CrashExit,"Cannot open build log: "+log_.errorString()); return; }
    process_=new QProcess(this);
    process_->setWorkingDirectory(job.directory);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_,&QProcess::readyReadStandardOutput,this,&JobQueue::read);
    connect(process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){ finish(code,status); });
    connect(process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if (process_ && error==QProcess::FailedToStart) finish(-1,QProcess::CrashExit,process_->errorString());
    });
    const QString heading="> "+displayCommand(job)+"\n";
    log_.write(heading.toUtf8());
    emit output(active_,heading); emit activity(job.label+" · starting"); emit changed();
    process_->start(job.program,job.arguments,QIODevice::ReadOnly);
}
void JobQueue::read(){
    if (!process_ || active_<0) return;
    const auto bytes=process_->readAllStandardOutput();
    if (bytes.isEmpty()) return;
    if (log_.write(bytes)!=bytes.size()) {
        jobs_[active_].error="Build log write failed: "+log_.errorString();
        process_->kill();
    }
    emit output(active_,decoder_(bytes));
    pendingLine_+=bytes;
    // Bound display parsing even if a child emits a very long line; the full log stays on disk.
    if (pendingLine_.size()>65536) pendingLine_=pendingLine_.right(65536);
    qsizetype newline;
    while ((newline=pendingLine_.indexOf('\n'))>=0) {
        const QString line=QString::fromUtf8(pendingLine_.left(newline)).trimmed();
        pendingLine_.remove(0,newline+1);
        if (line.startsWith("--- ")) emit activity(jobs_[active_].label+" · "+line.mid(4).trimmed());
    }
}
void JobQueue::finish(int code,QProcess::ExitStatus status,const QString& error){
    if (active_<0) return;
    read();
    const int index=active_;
    auto& job=jobs_[index];
    if (!error.isEmpty()) job.error=error;
    if (log_.isOpen() && !log_.flush() && job.error.isEmpty()) job.error="Could not flush the build log: "+log_.errorString();
    job.elapsedMs=timer_.elapsed(); job.exitCode=code;
    job.state=cancelled_ ? "Cancelled" : code==0 && status==QProcess::NormalExit && job.error.isEmpty() ? "Succeeded" : "Failed";
    if (job.state=="Failed" && job.error.isEmpty()) job.error="Compiler exited with code "+QString::number(code);
    if (!job.error.isEmpty()) {
        const auto message="\nERROR: "+job.error+"\n";
        if (log_.isOpen()) log_.write(message.toUtf8());
        emit output(index,message);
    }
    if (job.state!="Succeeded") {
        for (int i=index+1;i<jobs_.size();++i) if (jobs_[i].group==job.group && jobs_[i].state=="Queued") {
            jobs_[i].state="Skipped"; jobs_[i].error="A required earlier stage did not succeed.";
        }
    }
    log_.close();
    if (process_) { process_->disconnect(this); process_->deleteLater(); process_=nullptr; }
    active_=-1;
    emit completed(index); emit changed();
    if (stopped_) emit idle(); else QTimer::singleShot(0,this,&JobQueue::next);
}
void JobQueue::cancel(){
    stopped_=true;
    if (!process_ || active_<0) return;
    cancelled_=true; emit activity("Cancelling "+jobs_[active_].label+"…");
    process_->terminate();
    const QPointer<QProcess> target(process_);
    QTimer::singleShot(1200,this,[target]{ if (target && target->state()!=QProcess::NotRunning) target->kill(); });
}
void JobQueue::clearFinished(){
    if (running()) return;
    jobs_.removeIf([](const Job& job){ return job.state!="Queued"; }); emit changed();
}
QJsonObject JobQueue::report() const {
    QJsonArray records; for (const auto& job:jobs_) records.append(job.toJson());
    return {{"schema_version",1},{"jobs",records}};
}
}
