#include "RenderWorker.h"

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

void RenderWorker::renderPage(int page, double zoom, int requestId)
{
    if (!m_doc || requestId < m_latest.load())
        return;
    emit pageRendered(m_doc->render(page, zoom), requestId);
}
