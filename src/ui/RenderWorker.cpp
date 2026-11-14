#include "RenderWorker.h"

#include "preflight/Preflight.h"

#include <QSizeF>

void RenderWorker::openDocument(const QString &path)
{
    QString error;
    auto doc = engine::Document::open(path, &error);
    if (!doc) {
        emit openFailed(path, error);
        return;
    }
    m_doc = std::move(doc);
    QList<QSizeF> sizes;
    for (int i = 0; i < m_doc->info().pageCount; ++i)
        sizes.append(m_doc->pageSize(i));
    emit documentOpened(m_doc->info(), sizes);
}

void RenderWorker::renderPage(int page, double zoom, int rotation, int requestId)
{
    if (!m_doc || requestId < m_latest.load())
        return;
    emit pageRendered(m_doc->render(page, zoom, rotation), requestId);
}

void RenderWorker::renderThumbnail(int page, int size, int rotation, int generation)
{
    if (!m_doc)
        return;
    const QSizeF pt = m_doc->pageSize(page);
    const double longest = qMax(pt.width(), pt.height());
    emit thumbnailRendered(m_doc->render(page, longest > 0 ? size / longest : 0.1, rotation), generation);
}

void RenderWorker::search(const QString &text, int rotation, int searchId)
{
    QList<engine::SearchHit> hits;
    if (m_doc) {
        for (int page = 0; page < m_doc->info().pageCount; ++page) {
            if (searchId < m_latestSearch.load())
                return; // replaced by a newer search
            hits += m_doc->search(page, text, rotation);
        }
    }
    emit searchFinished(text, hits, searchId);
}

void RenderWorker::inspect(int page, QPointF point, int rotation)
{
    if (m_doc)
        emit objectsFound(page, m_doc->objectsAt(page, point, rotation));
}

void RenderWorker::runPreflight(int standard, int checkId)
{
    if (!m_doc)
        return;
    const preflight::Report report =
        preflight::run(*m_doc, preflight::Standard(standard), {}, [this, checkId](int page, int count) {
            emit preflightProgress(page, count, checkId);
            return checkId >= m_latestPreflight.load();
        });
    emit preflightFinished(report, checkId);
}
