#pragma once
// "Sega Saturn" tab: build the SATURN overlay from a Saturn disc image.

#include <QWidget>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLineEdit;
class RunPanel;
class BackgroundFormatRow;
class FfmpegRow;

class SaturnAssetsTab : public QWidget {
    Q_OBJECT
public:
    // `target` is the window-wide target game folder edit and `backgrounds` the
    // window-wide background format row (both owned by MainWindow),
    // `ffmpeg` the window-wide ffmpeg location.
    explicit SaturnAssetsTab(QLineEdit* target, BackgroundFormatRow* backgrounds, FfmpegRow* ffmpeg,
                             QWidget* parent = nullptr);

private slots:
    void onBrowseImage();

private:
    QLineEdit* m_image = nullptr;
    QLineEdit* m_target = nullptr;  // shared, not owned
    BackgroundFormatRow* m_backgrounds = nullptr;  // shared, not owned
    QCheckBox* m_allModels = nullptr;
    QCheckBox* m_allRooms = nullptr;
    QCheckBox* m_allBgm = nullptr;
    QCheckBox* m_movies = nullptr;
    QComboBox* m_musicFormat = nullptr;
    QSpinBox* m_musicQuality = nullptr;
    FfmpegRow* m_ffmpeg = nullptr;  // shared, not owned
    QCheckBox* m_verify = nullptr;
    RunPanel* m_run = nullptr;
};
