#include "SaturnAssetsTab.h"

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
#include <QSpinBox>
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

SaturnAssetsTab::SaturnAssetsTab(QLineEdit* target, BackgroundFormatRow* backgrounds,
                                 FfmpegRow* ffmpeg, QWidget* parent)
    : QWidget(parent), m_target(target), m_backgrounds(backgrounds),
      m_ffmpeg(ffmpeg) {
    m_image = new QLineEdit(this);
    auto* browseImage = new QPushButton(tr("Browse..."), this);


    m_allModels = new QCheckBox(tr("All enemy and character models"), this);
    m_allModels->setChecked(false);
    m_allModels->setToolTip(
        tr("Converts every other model on the disc (ENEMY/EM10xx, EM11xx and "
           "CHAR10-13: the enemies, the NPCs and the four player models) to PC "
           ".EMDs in SATURN/Enemy under the PC's own file names, so Saturn mode "
           "draws the Saturn's meshes and textures for everyone. The animations "
           "are the PS1 data each Saturn model carries.\n"
           "63 models, ~19 MB. A converted model is 1.5-2.5x the PC file "
           "(its textures are unpacked into several pages). Off, only the "
           "Saturn-only models listed above are written and the base tree "
           "supplies the rest."));
    m_allRooms = new QCheckBox(tr("All rooms and backgrounds"), this);
    m_allRooms->setChecked(false);
    m_allRooms->setToolTip(
        tr("Decodes every campaign room (STAGE1-7 ROOM*.RDT) and converts every "
           "background (ROOM*.BSS -> RC*.pak) into SATURN/Stage1..7, so Saturn "
           "mode runs the Saturn's room data and art everywhere.\n"
           "About 350 rooms and 640 backgrounds: ~450 MB and a couple of "
           "minutes. Off, only the rooms the Saturn changed are written (the "
           "Laboratory Tyrant room and the wardrobe) and the base tree supplies "
           "the rest.\n"
           "Note: 141 of the Saturn's 348 rooms are not byte-identical to the "
           "PC's - the PC release patched their scripts, typically by 1-2 bytes "
           "and up to a few hundred. With this on, Saturn mode runs the "
           "unpatched Saturn scripts in those rooms."));
    m_allBgm = new QCheckBox(tr("All room BGM (rendered from the Saturn's sequences)"), this);
    m_allBgm->setChecked(false);
    m_allBgm->setToolTip(
        tr("Renders every campaign BGM the game names from the Saturn's "
           "BGM<group>.CDP packs into SATURN/Sound under the PC's own file "
           "names, so Saturn mode plays the Saturn's arrangement in every room.\n"
           "The Saturn plays its music as sequences on its sound chip; they are "
           "played here through a model of Sega's sound driver and the SCSP, "
           "reverb included. Off, the base tree's music plays."));
    m_movies = new QCheckBox(tr("FMV: convert MOVIE.CPK to MP4 (needs ffmpeg)"), this);
    m_movies->setChecked(false);
    m_movies->setToolTip(
        tr("Converts the Saturn's 28 movies (Sega FILM / Cinepak, 320x224) to "
           "H.264 MP4 in SATURN/Movie under the PC's movie names, so Saturn mode "
           "plays the Saturn's FMV.\n"
           "The Saturn keeps the prologue as three files; they are joined into "
           "one and the game skips the Chris-only part for Jill at the Saturn's "
           "own cut points. The Japanese disc's prologue has its subtitles in the "
           "picture, and the staff rolls their credits, so there is no separate "
           "subtitle or credits data to migrate. The PC's fourth staff roll "
           "(staf_r) has no Saturn version and stays the base tree's.\n"
           "About 250 MB of source; takes a few minutes. Off, the base tree's "
           "movies play."));
    m_musicFormat = new QComboBox(this);
    m_musicFormat->addItem(tr("WAV (lossless)"), false);
    m_musicFormat->addItem(tr("Ogg Vorbis (needs ffmpeg)"), true);
    m_musicQuality = new QSpinBox(this);
    m_musicQuality->setRange(0, 10);
    m_musicQuality->setValue(6);
    m_musicQuality->setToolTip(tr("Vorbis quality (-q:a)."));
    auto* musicRow = new QWidget(this);
    {
        auto* l = new QHBoxLayout(musicRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_musicFormat);
        l->addWidget(new QLabel(tr("Quality:"), musicRow));
        l->addWidget(m_musicQuality);
        l->addStretch(1);
    }
    // The format applies to every rendered sound (the Battle Game's music, the
    // Tick's effects and, when asked for, the campaign BGM), so it is always live.
    auto syncMusic = [this]() {
        m_musicQuality->setEnabled(m_musicFormat->currentData().toBool());
    };
    connect(m_musicFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncMusic);
    syncMusic();
    m_verify = new QCheckBox(tr("Verify the overlay after building"), this);
    m_verify->setChecked(true);
    auto* form = new QFormLayout();
    form->addRow(tr("Disc image:"), pathRow(m_image, browseImage));
    form->addRow(tr("Optional:"), m_allModels);
    form->addRow(QString(), m_allRooms);
    form->addRow(QString(), m_allBgm);
    form->addRow(QString(), m_movies);
    form->addRow(tr("Sound format:"), musicRow);
    form->addRow(QString(), m_verify);

    m_run = new RunPanel(this);

    auto* note = new QLabel(
            tr("Builds the Saturn overlay (<target>/SATURN) from a Resident Evil "
               "Sega Saturn disc image: the track-1 .bin, its .cue or a 2048-byte "
               ".iso. The Japanese \"Bio Hazard\" disc is detected and goes to "
               "<target>/SATURN_JPN instead: Japanese text, item labels and the "
               "results screens' BIO HAZARD logo, read by the game with [Assets] "
               "Version=JPN. Only the content the Saturn release adds is written; "
               "everything else falls back to the base tree, which is never "
               "written to.\n\n"
               "Always migrated: the Saturn-only creatures (the Tick, Zombie "
               "Wesker), the Saturn-only alternate outfits, the Battle Game "
               "arenas, the Laboratory Tyrant room (its re-drawn backgrounds and "
               "room data), the wardrobe room (patched so one of the two wardrobes "
               "offer the Saturn-only outfit) and the exclusive sound (Battle "
               "Game music, Tick "
               "sound effects). The optional migrations below are large.\n\n"
               ""),
        this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(m_run, 1);

    connect(browseImage, &QPushButton::clicked, this, &SaturnAssetsTab::onBrowseImage);

    m_run->setTaskFactory([this]() -> Worker* {
        re1::SaturnMigrationOptions opts;
        opts.imagePath = m_image->text().toStdString();
        opts.targetRoot = m_target->text().toStdString();
        opts.allModels = m_allModels->isChecked();
        opts.allRooms = m_allRooms->isChecked();
        opts.allBgm = m_allBgm->isChecked();
        opts.movies = m_movies->isChecked();
        opts.musicFormat = m_musicFormat->currentData().toBool() ? re1::AudioFormat::Ogg
                                                                 : re1::AudioFormat::Wav;
        opts.oggQuality = m_musicQuality->value();
        opts.ffmpegPath = m_ffmpeg->path();
        opts.verify = m_verify->isChecked();
        opts.backgroundFormat = m_backgrounds->format();
        opts.webpQuality = m_backgrounds->quality();
        return new Worker(
            [opts](const re1::Progress& p, QString& error) {
                std::string err;
                const bool ok = re1::migrateSaturnAssets(opts, p, &err);
                if (!ok) error = QString::fromStdString(err);
                return ok;
            });
    });
}

void SaturnAssetsTab::onBrowseImage() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Select the Saturn disc image (track 1)"), m_image->text(),
        tr("Disc images (*.bin *.cue *.iso);;All files (*)"));
    if (!f.isEmpty()) m_image->setText(f);
}
