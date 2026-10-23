#include "MainWindow.h"

#include "BackgroundFormatRow.h"

#include "DcAssetsTab.h"
#include "FfmpegRow.h"
#include "HdPackTab.h"
#include "PcAssetsTab.h"
#include "SaturnAssetsTab.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("RE1 Asset Migrator"));
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    // One target folder for every tab, so a migration never lands somewhere
    // other than where the previous one did.
    auto* target = new QLineEdit(central);
    target->setText(QCoreApplication::applicationDirPath());
    auto* browse = new QPushButton(tr("Browse..."), central);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Target game folder:"), central));
    row->addWidget(target, 1);
    row->addWidget(browse);
    layout->addLayout(row);
    connect(browse, &QPushButton::clicked, this, [this, target]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, tr("Select the game folder"), target->text());
        if (!d.isEmpty()) target->setText(d);
    });

    // And one background format, for the same reason: the game takes a .webp
    // over a .pak wherever it finds one, so a tree and its overlays are best
    // kept alike.
    auto* backgrounds = new BackgroundFormatRow(central);
    layout->addWidget(backgrounds);

    // And one ffmpeg, set once for every tab.
    auto* ffmpeg = new FfmpegRow(central);
    layout->addWidget(ffmpeg);

    auto* tabs = new QTabWidget(central);
    tabs->addTab(new PcAssetsTab(target, backgrounds, ffmpeg, tabs), tr("PC Assets"));
    tabs->addTab(new DcAssetsTab(target, backgrounds, ffmpeg, tabs), tr("Director's Cut"));
    tabs->addTab(new SaturnAssetsTab(target, backgrounds, ffmpeg, tabs), tr("Sega Saturn"));
    tabs->addTab(new HdPackTab(target, ffmpeg, tabs), tr("HD Pack"));
    layout->addWidget(tabs, 1);
    setCentralWidget(central);
    resize(760, 620);
}
