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
    PageRender render(int page, double zoom);

    // Page size in points, display orientation.
    QSizeF pageSize(int page);

private:
    Document() = default;
    void readInfo(const QString &path);

    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    DocumentInfo m_info;
};

} // namespace engine
