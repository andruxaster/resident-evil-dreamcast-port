#pragma once
// The window-wide ffmpeg location. One row for every tab, like the target
// folder, so it is set once; it is remembered between runs.

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

class FfmpegRow : public QWidget {
    Q_OBJECT
public:
    explicit FfmpegRow(QWidget* parent = nullptr) : QWidget(parent) {
        m_path = new QLineEdit(this);
        m_path->setText(QSettings().value(QStringLiteral("ffmpeg"),
                                          QStringLiteral("ffmpeg")).toString());
        m_path->setToolTip(
            tr("Used for the movie conversions, Ogg Vorbis audio, WebP "
               "backgrounds and the HD pack import (which needs libwebp)."));
        auto* browse = new QPushButton(tr("Browse..."), this);
        auto* detect = new QPushButton(tr("Detect"), this);
        m_status = new QLabel(this);

        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("ffmpeg:"), this));
        l->addWidget(m_path, 1);
        l->addWidget(browse);
        l->addWidget(detect);
        l->addWidget(m_status);

        connect(m_path, &QLineEdit::textChanged, this, [this](const QString& t) {
            QSettings().setValue(QStringLiteral("ffmpeg"), t);
            m_status->clear();
        });
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(
                this, tr("Select ffmpeg"), m_path->text(),
                tr("ffmpeg (ffmpeg*.exe);;All files (*)"));
            if (!f.isEmpty()) m_path->setText(f);
        });
        connect(detect, &QPushButton::clicked, this, [this]() {
            const QString f =
                QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
            if (f.isEmpty()) {
                m_status->setText(tr("not found on PATH"));
            } else {
                m_path->setText(f);
                m_status->setText(tr("found"));
            }
        });
    }

    std::string path() const { return m_path->text().toStdString(); }

private:
    QLineEdit* m_path = nullptr;
    QLabel* m_status = nullptr;
};
