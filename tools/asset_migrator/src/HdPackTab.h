#pragma once
// "HD Pack" tab: import a Classic REbirth texture pack (Seamless HD Project,
// RE-ENHANCE) into the port's HD/ folder, and the Seamless HD Project's HD
// movies into the tree's Movie/ folder.

#include <QWidget>

class QComboBox;
class QLineEdit;
class RunPanel;
class FfmpegRow;

class HdPackTab : public QWidget {
    Q_OBJECT
public:
    // `target` is the window-wide target game folder edit (owned by MainWindow).
    explicit HdPackTab(QLineEdit* target, FfmpegRow* ffmpeg, QWidget* parent = nullptr);

private slots:
    void onBrowsePack();
    void onBrowseMovies();

private:
    QComboBox* m_game = nullptr;
    QLineEdit* m_pack = nullptr;
    QLineEdit* m_target = nullptr;  // shared, not owned
    QComboBox* m_tree = nullptr;
    FfmpegRow* m_ffmpeg = nullptr;  // shared, not owned
    QLineEdit* m_movies = nullptr;
    QComboBox* m_capcom = nullptr;
    QComboBox* m_dm7 = nullptr;
    RunPanel* m_run = nullptr;
};
