// Private helpers shared by the code that talks to MuPDF directly
// (Document.cpp and the preflight checker). Never include this from the UI.
#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

namespace engine::mu {

inline QRectF toQRect(fz_rect r)
{
    return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)).normalized();
}

inline QString pdfText(fz_context *ctx, pdf_obj *obj)
{
    if (!obj)
        return {};
    if (pdf_is_name(ctx, obj))
        return QString::fromUtf8(pdf_to_name(ctx, obj));
    if (pdf_is_string(ctx, obj))
        return QString::fromUtf8(pdf_to_text_string(ctx, obj));
    return {};
}

// Maps page space to the rotated view with its top-left corner at 0, 0.
inline fz_matrix viewMatrix(fz_rect bounds, int rotation)
{
    const fz_matrix m = fz_rotate(float(rotation));
    const fz_rect r = fz_transform_rect(bounds, m);
    return fz_concat(m, fz_translate(-r.x0, -r.y0));
}

inline QString colorSpaceLabel(fz_context *ctx, fz_colorspace *cs)
{
    if (!cs)
        return QStringLiteral("None");
    QString name = QString::fromUtf8(fz_colorspace_name(ctx, cs));
    const auto type = fz_colorspace_type(ctx, cs);
    if (type == FZ_COLORSPACE_SEPARATION) {
        QStringList inks;
        for (int i = 0; i < fz_colorspace_n(ctx, cs); ++i)
            inks << QString::fromUtf8(fz_colorspace_colorant(ctx, cs, i));
        name = QStringLiteral("%1 (%2)")
                   .arg(inks.size() > 1 ? QStringLiteral("DeviceN") : QStringLiteral("Separation"),
                        inks.join(QStringLiteral(", ")));
    }
    return name;
}

} // namespace engine::mu
