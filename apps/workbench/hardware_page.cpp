// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_page.h"
#include <QtWidgets>
#include <QJsonArray>

namespace workbench {
HardwarePage::HardwarePage(QWidget* parent):QWidget(parent),inventory_(this){
    setObjectName("hardwarePage");
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* title=new QLabel("Compute devices"); title->setObjectName("pageTitle"); layout->addWidget(title);
    auto* note=new QLabel("Minimaps can use OpenCL GPUs. Automatic mode chooses CPU for small workloads and falls back if GPU compute is unavailable. Lighting defaults to the CPU job pool; experimental GPU area factors are available through advanced LIGHT arguments.");
    note->setWordWrap(true); note->setObjectName("notice"); layout->addWidget(note);
    compilerName_=new QLabel; compilerName_->setTextFormat(Qt::PlainText); compilerName_->setWordWrap(true);
    compilerName_->setObjectName("hardwareCompiler"); layout->addWidget(compilerName_);
    auto* controls=new QHBoxLayout;
    refresh_=new QPushButton("Refresh device inventory"); refresh_->setObjectName("refreshDevices");
    cancel_=new QPushButton("Cancel query"); cancel_->setObjectName("cancelDevices");
    controls->addWidget(refresh_); controls->addWidget(cancel_); controls->addStretch(); layout->addLayout(controls);
    summary_=new QLabel; summary_->setObjectName("hardwareSummary"); summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true); layout->addWidget(summary_);
    auto* tabs=new QTabWidget; tabs->setObjectName("hardwareTabs");
    auto* devicePage=new QWidget; auto* deviceLayout=new QVBoxLayout(devicePage); deviceLayout->setContentsMargins(10,10,10,10);
    devices_=new QTableWidget(0,5); devices_->setObjectName("computeDevices"); devices_->setAccessibleName("Available OpenCL GPU devices");
    devices_->setStyleSheet("QTableWidget#computeDevices { padding:0; }");
    devices_->setHorizontalHeaderLabels({"Index","Device","Vendor","Memory","Units"});
    for(int column=0;column<devices_->columnCount();++column)
        devices_->horizontalHeaderItem(column)->setTextAlignment((column==0 || column>=3?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter);
    devices_->horizontalHeaderItem(3)->setToolTip("Driver-reported global memory; shared memory is identified in the device details");
    devices_->horizontalHeaderItem(4)->setToolTip("Driver-reported compute units; counts are not comparable across vendors");
    devices_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    devices_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    devices_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Interactive); devices_->setColumnWidth(2,150);
    devices_->verticalHeader()->hide(); devices_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    devices_->setSelectionBehavior(QAbstractItemView::SelectRows); devices_->setSelectionMode(QAbstractItemView::SingleSelection);
    devices_->setWordWrap(false); devices_->setMinimumHeight(140); deviceLayout->addWidget(devices_,1);
    details_=new QPlainTextEdit; details_->setObjectName("deviceDetails"); details_->setReadOnly(true);
    details_->setAccessibleName("Selected device capabilities and compiler messages"); details_->setMinimumHeight(90); details_->setMaximumHeight(140);
    deviceLayout->addWidget(details_);
    tabs->addTab(devicePage,"Devices");
    json_=new QPlainTextEdit; json_->setObjectName("deviceJson"); json_->setReadOnly(true);
    json_->setAccessibleName("Validated device inventory JSON"); json_->setLineWrapMode(QPlainTextEdit::NoWrap);
    json_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); tabs->addTab(json_,"Inventory JSON"); layout->addWidget(tabs,1);
    auto* cpu=new QLabel("CPU workers: automatic, or 1–1024. GPU compute: OpenCL 1.2 or later. No GPU SDK is required.");
    cpu->setWordWrap(true); cpu->setObjectName("muted"); layout->addWidget(cpu);
    connect(refresh_,&QPushButton::clicked,this,[this]{ inventory_.refresh(compiler_); });
    connect(cancel_,&QPushButton::clicked,this,&HardwarePage::cancel);
    connect(&inventory_,&DeviceInventory::changed,this,&HardwarePage::refresh);
    connect(devices_,&QTableWidget::itemSelectionChanged,this,&HardwarePage::displayDevice);
    refresh();
}

void HardwarePage::setCompiler(const QString& compiler){
    if(compiler_==compiler) return;
    compiler_=compiler;
    inventory_.reset();
}

void HardwarePage::refresh(){
    compilerName_->setText(compiler_.isEmpty()?"Choose a compiler in Project settings":"Compiler: "+QFileInfo(compiler_).fileName());
    compilerName_->setToolTip("<qt>"+compiler_.toHtmlEscaped()+"</qt>");
    refresh_->setEnabled(!inventory_.loading() && !compiler_.trimmed().isEmpty()); cancel_->setEnabled(inventory_.loading());
    const auto& report=inventory_.report(); const auto devices=report["devices"].toArray();
    if(inventory_.loading()) summary_->setText("Querying the selected compiler for OpenCL GPUs…");
    else if(!inventory_.error().isEmpty()) summary_->setText(inventory_.error()=="Device query cancelled"?inventory_.error():"Device query could not complete · see the details below.");
    else if(report.isEmpty()) summary_->setText("Refresh to discover the selected compiler's GPU devices.");
    else if(devices.isEmpty()) summary_->setText("No OpenCL GPU available · CPU workflows remain available.");
    else summary_->setText(QString("%1 OpenCL GPU device%2 · select a row for capabilities").arg(devices.size()).arg(devices.size()==1?"":"s"));
    // Driver strings are plain text, including in tooltips and compiler messages.
    const QSignalBlocker blocker(devices_);
    devices_->setRowCount(devices.size());
    for(int row=0;row<devices.size();++row) {
        const auto device=devices[row].toObject();
        const QStringList cells{QString::number(device["index"].toInteger()),device["name"].toString(),device["vendor"].toString(),
            QLocale().formattedDataSize(device["memory_bytes"].toInteger()),QString::number(device["compute_units"].toInteger())};
        for(int column=0;column<cells.size();++column) {
            auto* item=new QTableWidgetItem(cells[column]); item->setToolTip("<qt>"+cells[column].toHtmlEscaped()+"</qt>");
            if(column==0 || column>=3) item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            devices_->setItem(row,column,item);
        }
    }
    devices_->clearSelection(); if(!devices.isEmpty()) devices_->selectRow(0);
    json_->setPlainText(report.isEmpty()?QString():QString::fromUtf8(QJsonDocument(report).toJson()));
    displayDevice();
}

void HardwarePage::displayDevice(){
    const auto& report=inventory_.report(); const auto devices=report["devices"].toArray();
    QStringList text;
    if(!inventory_.error().isEmpty()) text << inventory_.error();
    const int row=devices_->currentRow();
    if(row>=0 && row<devices.size()) {
        const auto device=devices[row].toObject();
        text << QString("Device %1 · %2").arg(device["index"].toInteger()).arg(device["name"].toString());
        text << device["version"].toString();
        text << QString("%1 bytes global memory · %2").arg(QLocale().toString(device["memory_bytes"].toInteger()))
            .arg(device["unified_memory"].toBool()?"shared with the host":"separate from host memory");
        text << "Use this index in Project → Quality & compute to select a minimap GPU.";
    }
    if(!report["reason"].toString().isEmpty()) text << report["reason"].toString();
    if(!inventory_.diagnostics().isEmpty()) text << "Compiler messages:\n"+inventory_.diagnostics();
    details_->setPlainText(text.join('\n'));
}
}
