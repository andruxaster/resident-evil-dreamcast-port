#include "DcAssetsTab.h"

#include "BackgroundFormatRow.h"
#include "FfmpegRow.h"
#include "RunPanel.h"
#include "Worker.h"
#include "core/Migrate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QWidget* pathRow(QLineEdit* edit, QPushButton* browse) {
    auto* w = new QWidget();
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit, 1);
    l->addWidget(browse);
    return w;
}

}  // namespace

DcAssetsTab::DcAssetsTab(QLineEdit* target, BackgroundFormatRow* backgrounds,
                         FfmpegRow* ffmpeg, QWidget* parent)
    : QWidget(parent), m_target(target), m_backgrounds(backgrounds),
      m_ffmpeg(ffmpeg) {
    m_image = new QLineEdit(this);
    auto* browseImage = new QPushButton(tr("Browse..."), this);


    m_base = new QComboBox(this);
    m_base->addItem(tr("USA (North American / GOG)"), (int)re1::AssetVersion::USA);
    m_base->addItem(tr("Japanese (Biohazard)"), (int)re1::AssetVersion::JPN);

    m_convert = new QCheckBox(tr("Convert movies (STR -> MP4)"), this);
    m_convert->setChecked(false);
    m_subs = new QCheckBox(
        tr("Prologue FMV subtitles (JIMAKU*.RGB -> JPN/data/jimaku*.png)"),
        this);
    m_subs->setChecked(false);
    m_subs->setToolTip(
        tr("The Japanese PS1 prologue subtitles ([Game] Ps1FmvSubtitles=1).\n"
           "They are written into the JPN tree's Data folder, not the DC "
           "overlay, because the loader builds its path from the compile-time "
           "JPN asset root and calls LoadFile directly - it never looks in an "
           "overlay.\n"
           "A Japanese disc has them; a USA disc is skipped."));
    m_verify = new QCheckBox(tr("Verify the overlay after building"), this);
    m_verify->setChecked(true);
    m_verify->setToolTip(
        tr("After writing DC/, run the read-only completeness check:\n"
           "\u2022 every file the base tree has is present in the overlay "
           "(except the folders the DC legitimately falls back for), and\n"
           "\u2022 every DC room resolves to a camera-0 background pak.\n"
           "A half-populated overlay fails quietly at runtime by mixing DC and "
           "base content, so this reports it as an error instead. It writes "
           "nothing; untick it to skip the check."));

    auto* form = new QFormLayout();
    form->addRow(tr("Disc image:"), pathRow(m_image, browseImage));
    form->addRow(tr("Base tree:"), m_base);
    form->addRow(QString(), m_convert);
    form->addRow(QString(), m_subs);
    form->addRow(QString(), m_verify);

    m_run = new RunPanel(this);

    auto* note = new QLabel(
        tr("Builds the DC overlay (<target>/DC) from a Director's Cut disc "
           "image. Every DC room and all backgrounds (STAGE1-7 and STAGE8-E) "
           "are migrated. A raw 2352-byte .bin/.cue image is required so the "
           "movies keep their CD-XA audio; an .iso is accepted but its audio is "
           "degraded. The base tree is never written to.\n\n"
           "Only DC-exclusive files go into the overlay. Rooms, models and "
           "backgrounds the DC left as the 1996 originals are recognised from "
           "a baked-in fingerprint of that release and fall back to the base "
           "tree, whose copies carry the PC release's own fixes.\n\n"
           "The one exception is the prologue FMV subtitles, which are added to "
           "<target>/JPN/Data - the loader reads them from the JPN tree's own "
           "Data folder, so there is nowhere else they would work from."),
        this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(m_run, 1);

    connect(browseImage, &QPushButton::clicked, this, &DcAssetsTab::onBrowseImage);

    m_run->setTaskFactory([this]() -> Worker* {
        re1::DcMigrationOptions opts;
        opts.imagePath = m_image->text().toStdString();
        opts.targetRoot = m_target->text().toStdString();
        opts.base = (re1::AssetVersion)m_base->currentData().toInt();
        opts.convertMovies = m_convert->isChecked();
        opts.fmvSubtitles = m_subs->isChecked();
        opts.verify = m_verify->isChecked();
        opts.ffmpegPath = m_ffmpeg->path();
        opts.backgroundFormat = m_backgrounds->format();
        opts.webpQuality = m_backgrounds->quality();
        return new Worker(
            [opts](const re1::Progress& p, QString& error) {
                std::string err;
                const bool ok = re1::migrateDcAssets(opts, p, &err);
                if (!ok) error = QString::fromStdString(err);
                return ok;
            });
    });
}

void DcAssetsTab::onBrowseImage() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Select the Director's Cut disc image"), m_image->text(),
        tr("Disc images (*.bin *.cue *.iso);;All files (*)"));
    if (!f.isEmpty()) m_image->setText(f);
}
