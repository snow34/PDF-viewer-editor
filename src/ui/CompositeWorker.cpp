#include "CompositeWorker.h"

void CompositeWorker::compose(const std::shared_ptr<engine::SeparatedImage> &image,
                              const engine::CompositeOptions &options, int requestId)
{
    if (!image || requestId < m_latest.load())
        return; // a newer request is already waiting
    bool calibrated = false;
    const QImage result = engine::composite(*image, options, &calibrated);
    emit composed(result, calibrated, requestId);
}
