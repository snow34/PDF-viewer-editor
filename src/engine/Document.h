// Wraps a MuPDF document. Not thread-safe: use one Document from one thread.
#pragma once

#include "Types.h"

#include <memory>

struct fz_context;
struct fz_document;

namespace engine {

class Document {
public:
    static std::unique_ptr<Document> open(const QString &path, QString *error);
    ~Document();

    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    const DocumentInfo &info() const { return m_info; }

    // Renders a page to ink channels with overprint simulated. The ink values
    // are the file's own values: content is rendered in the document's CMYK
    // output intent when it has one, in DeviceCMYK otherwise.
    // `rotation` turns the view clockwise by 0, 90, 180 or 270 degrees.
    PageRender render(int page, double zoom, int rotation = 0);

    // Page size in points, display orientation, before view rotation.
    QSizeF pageSize(int page);

    // Places on one page where `text` occurs (case-insensitive).
    QList<SearchHit> search(int page, const QString &text, int rotation = 0);

    // Objects whose bounding box contains `point` (view coordinates in
    // points), topmost first. Clipping is not taken into account.
    QList<ObjectInfo> objectsAt(int page, QPointF point, int rotation = 0);

private:
    Document() = default;
    void readInfo(const QString &path);

    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    DocumentInfo m_info;
};

} // namespace engine
