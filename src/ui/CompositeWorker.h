// Turns rendered ink channels into the image shown on screen, on its own
// thread so the window stays responsive while a large page is converted.
#pragma once

#include "engine/Compositor.h"

#include <QImage>
#include <QObject>

#include <atomic>
#include <memory>

class CompositeWorker : public QObject {
    Q_OBJECT
public:
    // Called from the UI thread: requests older than this id are skipped.
    void setLatestRequest(int id) { m_latest.store(id); }

public slots:
    void compose(const std::shared_ptr<engine::SeparatedImage> &image, const engine::CompositeOptions &options,
                 int requestId);

signals:
    void composed(const QImage &image, bool calibrated, int requestId);

private:
    std::atomic<int> m_latest{ 0 };
};

Q_DECLARE_METATYPE(std::shared_ptr<engine::SeparatedImage>)
Q_DECLARE_METATYPE(engine::CompositeOptions)
