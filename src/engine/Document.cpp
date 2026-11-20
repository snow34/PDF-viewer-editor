#include "Document.h"

#include <QFile>
#include <QRegularExpression>
#include <QSizeF>

#include "MuPdfUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine {

using namespace mu;

namespace {

// A device that paints nothing and records what lies under one point.
struct InspectDevice {
    fz_device super;
    fz_point point;       // page space
    fz_matrix view;       // page space -> view
    QList<ObjectInfo> *found;
};

QStringList colorValues(fz_context *ctx, fz_colorspace *cs, const float *color)
{
    QStringList values;
    if (!cs || !color)
        return values;
    const auto type = fz_colorspace_type(ctx, cs);
    const int n = fz_colorspace_n(ctx, cs);
    static const char *const rgb[] = { "Red", "Green", "Blue" };
    static const char *const cmyk[] = { "Cyan", "Magenta", "Yellow", "Black" };
    static const char *const lab[] = { "L", "a", "b" };
    for (int i = 0; i < n; ++i) {
        const char *colorant = fz_colorspace_colorant(ctx, cs, i);
        QString name = colorant ? QString::fromUtf8(colorant) : QString();
        if (type == FZ_COLORSPACE_RGB && n == 3) {
            values << QStringLiteral("%1 %2").arg(QLatin1String(rgb[i])).arg(qRound(color[i] * 255));
        } else if (type == FZ_COLORSPACE_LAB && n == 3) {
            values << QStringLiteral("%1 %2").arg(QLatin1String(lab[i])).arg(double(color[i]), 0, 'f', 1);
        } else {
            if (type == FZ_COLORSPACE_CMYK && n == 4)
                name = QLatin1String(cmyk[i]);
            else if (type == FZ_COLORSPACE_GRAY)
                name = QStringLiteral("Gray");
            else if (name.isEmpty())
                name = QStringLiteral("Channel %1").arg(i + 1);
            values << QStringLiteral("%1 %2 %").arg(name).arg(qRound(color[i] * 100));
        }
    }
    return values;
}

bool hit(InspectDevice *d, fz_rect r)
{
    return d->point.x >= r.x0 && d->point.x <= r.x1 && d->point.y >= r.y0 && d->point.y <= r.y1;
}

ObjectInfo describe(fz_context *ctx, InspectDevice *d, const char *kind, fz_rect bounds,
                    fz_colorspace *cs, const float *color, float alpha, fz_color_params params)
{
    ObjectInfo o;
    o.kind = QString::fromLatin1(kind);
    o.colorSpace = colorSpaceLabel(ctx, cs);
    o.values = colorValues(ctx, cs, color);
    o.opacity = alpha;
    o.overprint = params.op != 0;
    o.overprintMode = params.opm;
    o.bounds = toQRect(fz_transform_rect(bounds, d->view));
    return o;
}

void inspectFillPath(fz_context *ctx, fz_device *dev, const fz_path *path, int, fz_matrix ctm,
                     fz_colorspace *cs, const float *color, float alpha, fz_color_params params)
{
    auto *d = reinterpret_cast<InspectDevice *>(dev);
    const fz_rect r = fz_bound_path(ctx, path, nullptr, ctm);
    if (hit(d, r))
        d->found->prepend(describe(ctx, d, "Filled path", r, cs, color, alpha, params));
}

void inspectStrokePath(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke,
                       fz_matrix ctm, fz_colorspace *cs, const float *color, float alpha,
                       fz_color_params params)
{
    auto *d = reinterpret_cast<InspectDevice *>(dev);
    const fz_rect r = fz_bound_path(ctx, path, stroke, ctm);
    if (hit(d, r))
        d->found->prepend(describe(ctx, d, "Stroked path", r, cs, color, alpha, params));
}

void inspectText(fz_context *ctx, InspectDevice *d, const char *kind, const fz_text *text,
                 const fz_stroke_state *stroke, fz_matrix ctm, fz_colorspace *cs, const float *color,
                 float alpha, fz_color_params params)
{
    const fz_rect r = fz_bound_text(ctx, text, stroke, ctm);
    if (!hit(d, r))
        return;
    ObjectInfo o = describe(ctx, d, kind, r, cs, color, alpha, params);
    if (const fz_text_span *span = text->head) {
        o.font = QString::fromUtf8(fz_font_name(ctx, span->font));
        o.fontSize = fz_matrix_expansion(fz_concat(span->trm, ctm));
        o.fontSubstituted = fz_font_flags(span->font)->ft_substitute;
    }
    d->found->prepend(o);
}

void inspectFillText(fz_context *ctx, fz_device *dev, const fz_text *text, fz_matrix ctm,
                     fz_colorspace *cs, const float *color, float alpha, fz_color_params params)
{
    inspectText(ctx, reinterpret_cast<InspectDevice *>(dev), "Text", text, nullptr, ctm, cs, color, alpha, params);
}

void inspectStrokeText(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_stroke_state *stroke,
                       fz_matrix ctm, fz_colorspace *cs, const float *color, float alpha,
                       fz_color_params params)
{
    inspectText(ctx, reinterpret_cast<InspectDevice *>(dev), "Outlined text", text, stroke, ctm, cs, color,
                alpha, params);
}

void inspectImageCommon(fz_context *ctx, InspectDevice *d, const char *kind, fz_image *image, fz_matrix ctm,
                        fz_colorspace *cs, const float *color, float alpha, fz_color_params params)
{
    const fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    if (!hit(d, r))
        return;
    ObjectInfo o = describe(ctx, d, kind, r, cs, color, alpha, params);
    o.imageWidth = image->w;
    o.imageHeight = image->h;
    o.bitsPerComponent = image->bpc;
    // Size on the page in inches along the image's own axes.
    const double widthIn = std::hypot(ctm.a, ctm.b) / 72.0;
    const double heightIn = std::hypot(ctm.c, ctm.d) / 72.0;
    o.ppiX = widthIn > 0 ? image->w / widthIn : 0;
    o.ppiY = heightIn > 0 ? image->h / heightIn : 0;
    d->found->prepend(o);
}

void inspectImage(fz_context *ctx, fz_device *dev, fz_image *image, fz_matrix ctm, float alpha,
                  fz_color_params params)
{
    inspectImageCommon(ctx, reinterpret_cast<InspectDevice *>(dev), "Image", image, ctm, image->colorspace,
                       nullptr, alpha, params);
}

void inspectImageMask(fz_context *ctx, fz_device *dev, fz_image *image, fz_matrix ctm, fz_colorspace *cs,
                      const float *color, float alpha, fz_color_params params)
{
    inspectImageCommon(ctx, reinterpret_cast<InspectDevice *>(dev), "Image mask", image, ctm, cs, color,
                       alpha, params);
}

void inspectShade(fz_context *ctx, fz_device *dev, fz_shade *shade, fz_matrix ctm, float alpha,
                  fz_color_params params)
{
    auto *d = reinterpret_cast<InspectDevice *>(dev);
    const fz_rect r = fz_bound_shade(ctx, shade, ctm);
    if (hit(d, r))
        d->found->prepend(describe(ctx, d, "Gradient", r, shade->colorspace, nullptr, alpha, params));
}

} // namespace

std::unique_ptr<Document> Document::open(const QString &path, QString *error)
{
    std::unique_ptr<Document> d(new Document);
    d->m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!d->m_ctx) {
        if (error)
            *error = QStringLiteral("Could not initialise the PDF engine.");
        return nullptr;
    }
    fz_context *ctx = d->m_ctx;
#ifdef Q_OS_WIN
    const QByteArray file = path.toUtf8(); // MuPDF takes UTF-8 paths on Windows
#else
    const QByteArray file = QFile::encodeName(path);
#endif
    char message[256] = "";
    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        d->m_doc = fz_open_document(ctx, file.constData());
        if (fz_needs_password(ctx, d->m_doc))
            fz_throw(ctx, FZ_ERROR_GENERIC, "The document is password protected.");
        d->m_info.pageCount = fz_count_pages(ctx, d->m_doc);
    }
    fz_catch(ctx)
    {
        std::strncpy(message, fz_caught_message(ctx), sizeof message - 1);
    }
    if (message[0] || !d->m_doc) {
        if (error)
            *error = QString::fromUtf8(message);
        return nullptr;
    }
    d->readInfo(path);
    return d;
}

Document::~Document()
{
    for (Prepared &p : m_prepared)
        dropPrepared(p);
    if (m_ctx) {
        fz_drop_document(m_ctx, m_doc);
        fz_drop_context(m_ctx);
    }
}

void Document::readInfo(const QString &path)
{
    fz_context *ctx = m_ctx;
    m_info.path = path;

    char buf[512];
    if (fz_lookup_metadata(ctx, m_doc, FZ_META_INFO_TITLE, buf, sizeof buf) > 0)
        m_info.title = QString::fromUtf8(buf);
    if (fz_lookup_metadata(ctx, m_doc, FZ_META_FORMAT, buf, sizeof buf) > 0)
        m_info.pdfVersion = QString::fromUtf8(buf);

    pdf_document *pdf = pdf_specifics(ctx, m_doc);
    m_info.isPdf = pdf != nullptr;
    if (!pdf)
        return;

    QString infoVersion, xmp;
    QList<OutputIntentInfo> intents;
    QByteArray profile;
    fz_buffer *stream = nullptr;
    fz_var(stream);
    fz_try(ctx)
    {
        pdf_obj *trailer = pdf_trailer(ctx, pdf);
        pdf_obj *root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        pdf_obj *docInfo = pdf_dict_get(ctx, trailer, PDF_NAME(Info));

        // PDF/X-1a and PDF/X-3 identify themselves in the Info dictionary.
        infoVersion = pdfText(ctx, pdf_dict_gets(ctx, docInfo, "GTS_PDFXVersion"));
        // PDF/X-1a:2001 files say "PDF/X-1:2001" there and name the level separately.
        const QString conformance = pdfText(ctx, pdf_dict_gets(ctx, docInfo, "GTS_PDFXConformance"));
        if (!conformance.isEmpty())
            infoVersion = conformance;

        // PDF/X-4 and later identify themselves in XMP metadata only.
        pdf_obj *metadata = pdf_dict_get(ctx, root, PDF_NAME(Metadata));
        if (pdf_is_stream(ctx, metadata)) {
            stream = pdf_load_stream(ctx, metadata);
            unsigned char *bytes = nullptr;
            size_t len = fz_buffer_storage(ctx, stream, &bytes);
            xmp = QString::fromUtf8(reinterpret_cast<const char *>(bytes), qsizetype(len));
            fz_drop_buffer(ctx, stream);
            stream = nullptr;
        }

        pdf_obj *list = pdf_dict_get(ctx, root, PDF_NAME(OutputIntents));
        int n = pdf_array_len(ctx, list);
        for (int i = 0; i < n; ++i) {
            pdf_obj *oi = pdf_array_get(ctx, list, i);
            OutputIntentInfo e;
            e.subtype = pdfText(ctx, pdf_dict_get(ctx, oi, PDF_NAME(S)));
            e.conditionIdentifier = pdfText(ctx, pdf_dict_gets(ctx, oi, "OutputConditionIdentifier"));
            e.info = pdfText(ctx, pdf_dict_get(ctx, oi, PDF_NAME(Info)));
            e.registry = pdfText(ctx, pdf_dict_gets(ctx, oi, "RegistryName"));
            pdf_obj *dest = pdf_dict_gets(ctx, oi, "DestOutputProfile");
            if (pdf_is_stream(ctx, dest)) {
                e.hasProfile = true;
                e.profileComponents = pdf_to_int(ctx, pdf_dict_get(ctx, dest, PDF_NAME(N)));
                if (profile.isEmpty() && e.profileComponents == 4) {
                    stream = pdf_load_stream(ctx, dest);
                    unsigned char *bytes = nullptr;
                    size_t len = fz_buffer_storage(ctx, stream, &bytes);
                    profile = QByteArray(reinterpret_cast<const char *>(bytes), qsizetype(len));
                    fz_drop_buffer(ctx, stream);
                    stream = nullptr;
                }
            }
            intents.append(e);
        }
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, stream);
    }
    fz_catch(ctx)
    {
        // Damaged metadata must not stop the file from opening.
    }

    m_info.outputIntents = intents;
    m_info.outputIntentProfile = profile;
    m_info.pdfxVersion = infoVersion;
    if (m_info.pdfxVersion.isEmpty() && !xmp.isEmpty()) {
        static const QRegularExpression re(
            QStringLiteral("GTS_PDFXVersion\\s*(?:=\\s*[\"']([^\"']+)[\"']|>\\s*([^<]+?)\\s*<)"));
        const auto m = re.match(xmp);
        if (m.hasMatch())
            m_info.pdfxVersion = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
    }
}

QSizeF Document::pageSize(int pageNo)
{
    fz_context *ctx = m_ctx;
    fz_page *page = nullptr;
    fz_rect r = fz_empty_rect;
    fz_var(page);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, m_doc, pageNo);
        r = fz_bound_page(ctx, page);
    }
    fz_always(ctx)
    {
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        return {};
    }
    return QSizeF(r.x1 - r.x0, r.y1 - r.y0);
}

void Document::dropPrepared(Prepared &p)
{
    fz_drop_separations(m_ctx, p.seps);
    fz_drop_display_list(m_ctx, p.list);
    fz_drop_page(m_ctx, p.fzPage);
    p = Prepared();
}

Document::Prepared *Document::prepare(int pageNo)
{
    for (size_t i = 0; i < m_prepared.size(); ++i) {
        if (m_prepared[i].page == pageNo) {
            std::rotate(m_prepared.begin(), m_prepared.begin() + ptrdiff_t(i), m_prepared.begin() + ptrdiff_t(i) + 1);
            return &m_prepared.front();
        }
    }
    constexpr size_t kKeep = 6;
    fz_context *ctx = m_ctx;
    Prepared p;
    p.page = pageNo;
    fz_try(ctx)
    {
        p.fzPage = fz_load_page(ctx, m_doc, pageNo);
        p.list = fz_new_display_list_from_page(ctx, p.fzPage);
        p.seps = fz_page_separations(ctx, p.fzPage);
        const int spots = p.seps ? fz_count_separations(ctx, p.seps) : 0;
        for (int i = 0; i < spots; ++i)
            fz_set_separation_behavior(ctx, p.seps, i, FZ_SEPARATION_SPOT);

        // Page boxes in the page's own top-left coordinate system (rotation 0).
        const fz_matrix view = viewMatrix(fz_bound_page(ctx, p.fzPage), 0);
        if (pdf_page *pp = pdf_page_from_fz_page(ctx, p.fzPage)) {
            fz_rect cropped;
            fz_matrix pm;
            pdf_page_transform(ctx, pp, &cropped, &pm);
            auto box = [&](pdf_obj *key, bool inherit, bool *present) {
                pdf_obj *o = inherit ? pdf_dict_get_inheritable(ctx, pp->obj, key)
                                     : pdf_dict_get(ctx, pp->obj, key);
                const bool ok = pdf_is_array(ctx, o) && pdf_array_len(ctx, o) == 4;
                if (present)
                    *present = ok;
                return ok ? toQRect(fz_transform_rect(pdf_to_rect(ctx, o), fz_concat(pm, view)))
                          : QRectF();
            };
            p.boxes.media = box(PDF_NAME(MediaBox), true, nullptr);
            p.boxes.crop = box(PDF_NAME(CropBox), true, &p.boxes.hasCrop);
            p.boxes.bleed = box(PDF_NAME(BleedBox), false, &p.boxes.hasBleed);
            p.boxes.trim = box(PDF_NAME(TrimBox), false, &p.boxes.hasTrim);
            p.boxes.art = box(PDF_NAME(ArtBox), false, &p.boxes.hasArt);
        } else {
            p.boxes.media = toQRect(fz_transform_rect(fz_bound_page(ctx, p.fzPage), view));
        }
    }
    fz_catch(ctx)
    {
        dropPrepared(p);
        fz_rethrow(ctx);
    }
    if (m_prepared.size() >= kKeep) {
        dropPrepared(m_prepared.back());
        m_prepared.pop_back();
    }
    m_prepared.insert(m_prepared.begin(), p);
    return &m_prepared.front();
}

PageInfo Document::pageInfo(int pageNo)
{
    PageInfo info;
    fz_context *ctx = m_ctx;
    fz_try(ctx)
    {
        Prepared *p = prepare(pageNo);
        const fz_rect bounds = fz_bound_page(ctx, p->fzPage);
        info.size = QSizeF(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
        info.boxes = p->boxes;
        const int spots = p->seps ? fz_count_separations(ctx, p->seps) : 0;
        for (int i = 0; i < spots; ++i)
            info.spots << QString::fromUtf8(fz_separation_name(ctx, p->seps, i));
    }
    fz_catch(ctx)
    {
        // A damaged page shows as an empty page of size 0; rendering it reports the error.
    }
    return info;
}

PageRender Document::render(int pageNo, double zoom, int rotation)
{
    return renderTile(pageNo, zoom, rotation, QRect(0, 0, 1 << 30, 1 << 30));
}

PageRender Document::renderTile(int pageNo, double zoom, int rotation, const QRect &area)
{
    PageRender out;
    out.page = pageNo;
    out.zoom = zoom;
    out.rotation = rotation;
    auto image = std::make_shared<SeparatedImage>();
    image->inks = { QStringLiteral("Cyan"), QStringLiteral("Magenta"), QStringLiteral("Yellow"),
                    QStringLiteral("Black") };

    fz_context *ctx = m_ctx;
    fz_pixmap *pix = nullptr;
    fz_device *dev = nullptr;
    char message[256] = "";
    fz_var(pix);
    fz_var(dev);

    fz_try(ctx)
    {
        Prepared *p = prepare(pageNo);
        const fz_rect bounds = fz_bound_page(ctx, p->fzPage);
        const fz_matrix view = viewMatrix(bounds, rotation);
        const fz_matrix ctm = fz_concat(view, fz_scale(float(zoom), float(zoom)));
        fz_irect bbox = fz_round_rect(fz_transform_rect(bounds, ctm));
        bbox = fz_intersect_irect(bbox, fz_make_irect(area.left(), area.top(), area.left() + area.width(),
                                                      area.top() + area.height()));
        if (fz_is_empty_irect(bbox))
            fz_throw(ctx, FZ_ERROR_GENERIC, "The requested area lies outside the page.");

        // Render into the document's own CMYK space so DeviceCMYK values in
        // the file arrive unchanged in the ink channels.
        fz_colorspace *cs = fz_document_output_intent(ctx, m_doc);
        if (!cs || fz_colorspace_n(ctx, cs) != 4)
            cs = fz_device_cmyk(ctx);
        const int spots = p->seps ? fz_count_separations(ctx, p->seps) : 0;

        pix = fz_new_pixmap_with_bbox(ctx, cs, bbox, p->seps, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);
        // The transform goes to the display list, so the clip rectangle can be
        // given in pixels and only the objects inside it are drawn.
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, p->list, dev, ctm, fz_rect_from_irect(bbox), nullptr);
        fz_close_device(ctx, dev);

        for (int i = 0; i < spots; ++i) {
            float eq[4] = { 0, 0, 0, 0 };
            fz_separation_equivalent(ctx, p->seps, i, cs, eq, nullptr, fz_default_color_params);
            image->spotCmyk.push_back({ eq[0], eq[1], eq[2], eq[3] });
            image->inks.append(QString::fromUtf8(fz_separation_name(ctx, p->seps, i)));
        }

        const int w = fz_pixmap_width(ctx, pix);
        const int h = fz_pixmap_height(ctx, pix);
        const int n = fz_pixmap_components(ctx, pix);
        if (n != 4 + spots)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Unexpected channel count from the renderer.");
        image->width = w;
        image->height = h;
        image->data.resize(size_t(w) * size_t(h) * size_t(n));
        const unsigned char *src = fz_pixmap_samples(ctx, pix);
        const ptrdiff_t stride = fz_pixmap_stride(ctx, pix);
        for (int y = 0; y < h; ++y)
            std::memcpy(image->data.data() + size_t(y) * size_t(w) * size_t(n), src + y * stride,
                        size_t(w) * size_t(n));
        out.area = QRect(bbox.x0, bbox.y0, w, h);

        // Page boxes in the rotated view.
        out.boxes = p->boxes;
        if (rotation != 0) {
            const fz_matrix turn = fz_concat(fz_invert_matrix(viewMatrix(bounds, 0)), view);
            auto turned = [&](const QRectF &r) {
                return r.isNull() ? r
                                  : toQRect(fz_transform_rect(fz_make_rect(float(r.left()), float(r.top()),
                                                                           float(r.right()), float(r.bottom())),
                                                              turn));
            };
            out.boxes.media = turned(out.boxes.media);
            out.boxes.crop = turned(out.boxes.crop);
            out.boxes.bleed = turned(out.boxes.bleed);
            out.boxes.trim = turned(out.boxes.trim);
            out.boxes.art = turned(out.boxes.art);
        }
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_pixmap(ctx, pix);
    }
    fz_catch(ctx)
    {
        std::strncpy(message, fz_caught_message(ctx), sizeof message - 1);
    }

    if (message[0])
        out.error = QString::fromUtf8(message);
    else
        out.image = image;
    return out;
}

QList<SearchHit> Document::search(int pageNo, const QString &text, int rotation)
{
    QList<SearchHit> hits;
    const QByteArray needle = text.toUtf8();
    if (needle.isEmpty())
        return hits;
    constexpr int maxQuads = 2000;
    std::vector<fz_quad> quads(maxQuads);
    std::vector<int> marks(maxQuads);
    fz_context *ctx = m_ctx;
    fz_page *page = nullptr;
    int count = 0;
    fz_matrix view = fz_identity;
    fz_var(page);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, m_doc, pageNo);
        view = viewMatrix(fz_bound_page(ctx, page), rotation);
        count = fz_search_page(ctx, page, needle.constData(), marks.data(), quads.data(), maxQuads);
    }
    fz_always(ctx)
    {
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        return hits;
    }
    for (int i = 0; i < count; ++i) {
        if (marks[size_t(i)] || hits.isEmpty()) {
            SearchHit h;
            h.page = pageNo;
            hits.append(h);
        }
        hits.last().rects.append(toQRect(fz_transform_rect(fz_rect_from_quad(quads[size_t(i)]), view)));
    }
    return hits;
}

QList<ObjectInfo> Document::objectsAt(int pageNo, QPointF point, int rotation)
{
    QList<ObjectInfo> found;
    fz_context *ctx = m_ctx;
    fz_page *page = nullptr;
    fz_device *dev = nullptr;
    fz_var(page);
    fz_var(dev);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, m_doc, pageNo);
        const fz_matrix view = viewMatrix(fz_bound_page(ctx, page), rotation);
        InspectDevice *d = fz_new_derived_device(ctx, InspectDevice);
        dev = &d->super;
        d->view = view;
        d->point = fz_transform_point(fz_make_point(float(point.x()), float(point.y())),
                                      fz_invert_matrix(view));
        d->found = &found;
        d->super.fill_path = inspectFillPath;
        d->super.stroke_path = inspectStrokePath;
        d->super.fill_text = inspectFillText;
        d->super.stroke_text = inspectStrokeText;
        d->super.fill_image = inspectImage;
        d->super.fill_image_mask = inspectImageMask;
        d->super.fill_shade = inspectShade;
        fz_run_page(ctx, page, dev, fz_identity, nullptr);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        // Report what was found before the error.
    }
    return found;
}

} // namespace engine
