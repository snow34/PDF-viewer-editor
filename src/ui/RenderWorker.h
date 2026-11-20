// Owns the open document and does all MuPDF work on its own thread.
#pragma once

#include "engine/Compositor.h"
#include "engine/Document.h"
#include "TileKey.h"
#include "preflight/Types.h"

#include <QObject>
#include <QPointF>

#include <atomic>

class RenderWorker : public QObject {
    Q_OBJECT
public:
    // Called from the UI thread: a tile batch older than this id stops.
    void setLatestBatch(int id) { m_latestBatch.store(id); }
    void setLatestSearch(int id) { m_latestSearch.store(id); }
    void setLatestPreflight(int id) { m_latestPreflight.store(id); }

public slots:
    void openDocument(const QString &path);
    // Renders tiles in the given order until a newer batch is announced.
    void renderTiles(const QList<TileKey> &tiles, double zoom, int rotation, int generation, int batchId);
    // Small render for the thumbnails panel, fitted into a square of `size` pixels.
    void renderThumbnail(int page, int size, int rotation, const engine::CompositeOptions &options,
                         int generation);
    // Searches every page; a newer search (higher id) cancels this one.
    void search(const QString &text, int rotation, int searchId);
    void inspect(int page, QPointF point, int rotation);
    // `standard` is a preflight::Standard. A higher id set with
    // setLatestPreflight stops a running check.
    void runPreflight(int standard, int checkId);

signals:
    void documentOpened(const engine::DocumentInfo &info, const QList<engine::PageInfo> &pages);
    void openFailed(const QString &path, const QString &error);
    void tileRendered(const TileKey &key, const engine::PageRender &render, int generation);
    void thumbnailRendered(int page, const QImage &image, int generation);
    void searchFinished(const QString &text, const QList<engine::SearchHit> &hits, int searchId);
    void objectsFound(int page, const QList<engine::ObjectInfo> &objects);
    void preflightProgress(int page, int pageCount, int checkId);
    void preflightFinished(const preflight::Report &report, int checkId);

private:
    std::unique_ptr<engine::Document> m_doc;
    std::atomic<int> m_latestBatch{ 0 };
    std::atomic<int> m_latestSearch{ 0 };
    std::atomic<int> m_latestPreflight{ 0 };
};
