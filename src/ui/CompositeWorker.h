// Turns rendered ink channels into the pictures shown on screen, on its own
// thread so the window stays responsive.
#pragma once

#include "TileKey.h"
#include "engine/Compositor.h"

#include <QImage>
#include <QObject>

#include <atomic>
#include <memory>

class CompositeWorker : public QObject {
    Q_OBJECT
public:
    // Called from the UI thread when the display settings change: tiles
    // queued with older settings are skipped.
    void setLatestOptions(int id) { m_latest.store(id); }

public slots:
    void composeTile(const TileKey &key, int generation, int optionsId,
                     const std::shared_ptr<engine::SeparatedImage> &image, const QRect &area,
                     const engine::CompositeOptions &options);

signals:
    void tileComposed(const TileKey &key, int generation, int optionsId, const QImage &image, const QRect &area,
                      bool calibrated);

private:
    std::atomic<int> m_latest{ 0 };
};

Q_DECLARE_METATYPE(std::shared_ptr<engine::SeparatedImage>)
Q_DECLARE_METATYPE(engine::CompositeOptions)
