#include "CompositeWorker.h"

void CompositeWorker::composeTile(const TileKey &key, int generation, int optionsId,
                                  const std::shared_ptr<engine::SeparatedImage> &image, const QRect &area,
                                  const engine::CompositeOptions &options)
{
    if (!image || optionsId < m_latest.load())
        return; // the settings changed; the tile is queued again with the new ones
    bool calibrated = false;
    const QImage result = engine::composite(*image, options, &calibrated);
    emit tileComposed(key, generation, optionsId, result, area, calibrated);
}
