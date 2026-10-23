#pragma once
// The window-wide "Room backgrounds" choice: leave them as the .pak files the
// PC release ships, or convert them to WebP (core/BackgroundWebp.h). One row
// for every tab, like the target folder, so the base tree and the overlays of
// one install end up in the same format.

#include "core/BackgroundWebp.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QWidget>

class BackgroundFormatRow : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundFormatRow(QWidget* parent = nullptr) : QWidget(parent) {
        m_format = new QComboBox(this);
        m_format->addItem(tr("PAK (original)"), (int)re1::BackgroundFormat::Pak);
        m_format->addItem(tr("WebP lossless (needs ffmpeg, ~20% smaller)"),
                          (int)re1::BackgroundFormat::WebpLossless);
        m_format->addItem(tr("WebP lossy (needs ffmpeg, ~60% smaller at quality 90)"),
                          (int)re1::BackgroundFormat::WebpLossy);
        m_format->setToolTip(
            tr("The format every room background (Stage*/RC*.pak) is left in "
               "once a migration has run.\n"
               "PAK is what the PC release ships. With a WebP format each .pak "
               "is converted to RC*.webp, checked by reading it back and then "
               "removed; the game reads either.\n"
               "Lossless keeps every pixel. Lossy is smaller and not exact.\n"
               "On the PC Assets tab a WebP format also works with no source "
               "at all: it then converts the tree that is already there.\n"
               "Going back to PAK needs a new migration from the source."));
        m_quality = new QSpinBox(this);
        m_quality->setRange(1, 100);
        m_quality->setValue(90);
        m_quality->setToolTip(tr("libwebp quality for the lossy format. 90 is "
                                 "hard to tell from the original at 320x240."));
        m_qualityLabel = new QLabel(tr("Quality:"), this);

        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Room backgrounds:"), this));
        l->addWidget(m_format);
        l->addWidget(m_qualityLabel);
        l->addWidget(m_quality);
        l->addStretch(1);

        connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this]() {
                    sync();
                    emit changed();
                });
        sync();
    }

    re1::BackgroundFormat format() const {
        return (re1::BackgroundFormat)m_format->currentData().toInt();
    }
    int quality() const { return m_quality->value(); }
    // Every WebP format is encoded by ffmpeg, so the tabs keep their ffmpeg
    // field live while one is selected.
    bool needsFfmpeg() const { return format() != re1::BackgroundFormat::Pak; }

signals:
    void changed();

private:
    void sync() {
        const bool lossy = format() == re1::BackgroundFormat::WebpLossy;
        m_quality->setEnabled(lossy);
        m_qualityLabel->setEnabled(lossy);
    }

    QComboBox* m_format = nullptr;
    QSpinBox* m_quality = nullptr;
    QLabel* m_qualityLabel = nullptr;
};
