#pragma once
// "Director's Cut" tab: build the DC overlay from a PS1 disc image.

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class RunPanel;
class BackgroundFormatRow;
class FfmpegRow;

class DcAssetsTab : public QWidget {
    Q_OBJECT
public:
    // `target` is the window-wide target game folder edit and `backgrounds` the
    // window-wide background format row (both owned by MainWindow),
    // `ffmpeg` the window-wide ffmpeg location.
    explicit DcAssetsTab(QLineEdit* target, BackgroundFormatRow* backgrounds, FfmpegRow* ffmpeg,
                         QWidget* parent = nullptr);

private slots:
    void onBrowseImage();

private:
    QLineEdit* m_image = nullptr;
    QLineEdit* m_target = nullptr;  // shared, not owned
    BackgroundFormatRow* m_backgrounds = nullptr;  // shared, not owned
    QComboBox* m_base = nullptr;
    QCheckBox* m_convert = nullptr;
    QCheckBox* m_subs = nullptr;
    QCheckBox* m_verify = nullptr;
    FfmpegRow* m_ffmpeg = nullptr;  // shared, not owned
    RunPanel* m_run = nullptr;
};
