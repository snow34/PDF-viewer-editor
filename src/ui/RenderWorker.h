// Owns the open document and does all MuPDF work on its own thread.
#pragma once

#include "engine/Document.h"

#include <QObject>

#include <atomic>

class RenderWorker : public QObject {
    Q_OBJECT
public:
    // Called from the UI thread: renders older than this id are skipped.
    void setLatestRequest(int id) { m_latest.store(id); }

public slots:
    void openDocument(const QString &path);
    void renderPage(int page, double zoom, int requestId);

signals:
    void documentOpened(const engine::DocumentInfo &info, const QList<QSizeF> &pageSizes);
    void openFailed(const QString &path, const QString &error);
    void pageRendered(const engine::PageRender &render, int requestId);

private:
    std::unique_ptr<engine::Document> m_doc;
    std::atomic<int> m_latest{ 0 };
};
