#include "HdPackTab.h"

#include "FfmpegRow.h"
#include "RunPanel.h"
#include "Worker.h"
#include "core/HdPack.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

HdPackTab::HdPackTab(QLineEdit* target, FfmpegRow* ffmpeg, QWidget* parent)
    : QWidget(parent), m_target(target), m_ffmpeg(ffmpeg) {
    m_pack = new QLineEdit(this);
    auto* browse = new QPushButton(tr("Browse..."), this);
    auto* packRow = new QWidget(this);
    {
        auto* l = new QHBoxLayout(packRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_pack, 1);
        l->addWidget(browse);
    }

    m_game = new QComboBox(this);
    m_game->addItem(tr("Original game (the pack's hires folder)"),
                    (int)re1::HdPackGame::Original);
    m_game->addItem(tr("Director's Cut (Seamless HD Project: bio1dc\\hires)"),
                    (int)re1::HdPackGame::DirectorsCut);
    m_game->setToolTip(
        tr("Which game the selected hires folder is for. The Seamless HD Project "
           "ships two: hires for the original game and bio1dc\\hires for the "
           "Director's Cut camera angles - import each one with its own choice."));

    m_tree = new QComboBox(this);
    m_tree->addItem(tr("USA (North American / GOG)"), (int)re1::AssetVersion::USA);
    m_tree->addItem(tr("Japanese (Biohazard)"), (int)re1::AssetVersion::JPN);
    m_tree->setToolTip(
        tr("The migrated tree the files are named after, and the HD/ folder they "
           "go to (HD/USA or HD/JPN). Pick the one that is installed."));

    m_movies = new QLineEdit(this);
    m_movies->setPlaceholderText(tr("optional, e.g. RE_SHDP_1.1\\movie"));
    auto* browseMovies = new QPushButton(tr("Browse..."), this);
    auto* moviesRow = new QWidget(this);
    {
        auto* l = new QHBoxLayout(moviesRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_movies, 1);
        l->addWidget(browseMovies);
    }
    m_capcom = new QComboBox(this);
    m_capcom->addItem(tr("Pack's choice (Original)"), (int)re1::HdCapcomIntro::PackDefault);
    m_capcom->addItem(tr("Cubes (1997)"), (int)re1::HdCapcomIntro::Cubes);
    m_capcom->addItem(tr("Original (backlight projection)"), (int)re1::HdCapcomIntro::Original);
    m_dm7 = new QComboBox(this);
    m_dm7->addItem(tr("Pack's choice (Original)"), (int)re1::HdDm7Clip::PackDefault);
    m_dm7->addItem(tr("With the cut Jill scene"), (int)re1::HdDm7Clip::JillScene);
    m_dm7->addItem(tr("Original"), (int)re1::HdDm7Clip::Original);
    m_dm7->setToolTip(tr("DM7, the shark approach. The pack also has a version "
                         "with a Jill scene cut from the release."));

    auto* form = new QFormLayout();
    form->addRow(tr("Pack is for:"), m_game);
    form->addRow(tr("hires folder:"), packRow);
    form->addRow(tr("Names from tree:"), m_tree);
    form->addRow(tr("HD movie folder:"), moviesRow);
    form->addRow(tr("Capcom logo movie:"), m_capcom);
    form->addRow(tr("Shark movie (DM7):"), m_dm7);

    m_run = new RunPanel(this);

    auto* note = new QLabel(
        tr("Imports one hires folder of a Classic REbirth HD pack (Seamless HD "
           "Project, RE-ENHANCE) into <target>/HD, where the game picks the "
           "files up ([Textures] HdTextures=1, the default). Select a hires "
           "folder: it holds one or more of bgd, mask, misc, skin and skin2.\n\n"
           "\u2022 Original game: room backgrounds, masks, character and enemy "
           "skins, and the Data, Item_m1 (doors), Item_m2 (maps, item views) "
           "and Effspr textures are written under the game file's own path, "
           "HD/<USA|JPN>/Stage1/RC1060.webp, .../Enemy/EM1010.webp, "
           ".../Item_m1/DOOR00.webp ... Migrate the PC assets first. What the "
           "table does not cover is matched against the game's texture dump "
           "(set [Textures] DumpTextures=1, play, run this again) and goes by "
           "hash to HD/<hash>.webp.\n"
           "\u2022 Director's Cut: the rooms of the Director's Cut camera angles, "
           "written as HD/DC/Stage8/RC8000.webp and the room masks as "
           "HD/DC/Stage8/ROOM8000_MASK3.webp. Migrate the Director's Cut "
           "first.\n\n"
           "Only confident matches are imported; the rest is counted in the log.\n\n"
           "HD movie folder (optional): the Seamless HD Project's movie folder. "
           "Its .mp4 files are copied into <target>/HD/<USA|JPN>/Movie under the "
           "tree's FMV names (OJ -> OU, Ed4 -> EU4, Stfc_B -> STFC_R ... for USA); "
           "the game plays them in place of the tree's own movies. Leave the "
           "hires folder empty to import only the movies."),
        this);
    note->setWordWrap(true);

    // The tree only matters for the original game and the movies; the DC set
    // names come from the DC folder.
    auto syncTree = [this]() {
        const bool movies = !m_movies->text().trimmed().isEmpty();
        m_tree->setEnabled(movies ||
                           m_game->currentData().toInt() == (int)re1::HdPackGame::Original);
        m_capcom->setEnabled(movies);
        m_dm7->setEnabled(movies);
    };
    connect(m_game, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [syncTree](int) { syncTree(); });
    connect(m_movies, &QLineEdit::textChanged, this, [syncTree](const QString&) { syncTree(); });
    syncTree();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(m_run, 1);

    connect(browse, &QPushButton::clicked, this, &HdPackTab::onBrowsePack);
    connect(browseMovies, &QPushButton::clicked, this, &HdPackTab::onBrowseMovies);

    m_run->setTaskFactory([this]() -> Worker* {
        re1::HdPackOptions opts;
        opts.packDir = m_pack->text().toStdString();
        opts.game = (re1::HdPackGame)m_game->currentData().toInt();
        opts.targetRoot = m_target->text().toStdString();
        opts.tree = (re1::AssetVersion)m_tree->currentData().toInt();
        opts.ffmpegPath = m_ffmpeg->path();
        re1::HdMovieOptions movies;
        movies.movieDir = m_movies->text().trimmed().toStdString();
        movies.targetRoot = opts.targetRoot;
        movies.tree = opts.tree;
        movies.capcom = (re1::HdCapcomIntro)m_capcom->currentData().toInt();
        movies.dm7 = (re1::HdDm7Clip)m_dm7->currentData().toInt();
        const bool textures = !m_pack->text().trimmed().isEmpty();
        return new Worker(
            [opts, movies, textures](const re1::Progress& p, QString& error) {
                std::string err;
                if (!textures && movies.movieDir.empty()) {
                    error = QStringLiteral("select a hires folder, a movie folder, or both");
                    return false;
                }
                if (textures && !re1::importHdPack(opts, p, &err)) {
                    error = QString::fromStdString(err);
                    return false;
                }
                if (!movies.movieDir.empty() && !re1::importHdMovies(movies, p, &err)) {
                    error = QString::fromStdString(err);
                    return false;
                }
                return true;
            });
    });
}

void HdPackTab::onBrowsePack() {
    const QString d = QFileDialog::getExistingDirectory(
        this, tr("Select the HD pack's hires folder"), m_pack->text());
    if (!d.isEmpty()) m_pack->setText(d);
}

void HdPackTab::onBrowseMovies() {
    const QString d = QFileDialog::getExistingDirectory(
        this, tr("Select the HD pack's movie folder"), m_movies->text());
    if (!d.isEmpty()) m_movies->setText(d);
}
