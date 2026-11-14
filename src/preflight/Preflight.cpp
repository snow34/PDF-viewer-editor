#include "Preflight.h"

#include "engine/Document.h"
#include "engine/MuPdfUtil.h"
#include "engine/ProfileLibrary.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <cstring>
#include <set>

using namespace engine::mu;

namespace preflight {

namespace {

const int kMaxPerRuleAndPage = 10; // further identical findings on a page are counted, not listed

bool isX1a(Standard s) { return s == Standard::X1a_2001 || s == Standard::X1a_2003; }
bool isX4(Standard s) { return s == Standard::X4; }
// Highest PDF version the standard is based on, as 13, 14 or 16.
int baseVersion(Standard s)
{
    switch (s) {
    case Standard::X1a_2001:
    case Standard::X3_2002: return 13;
    case Standard::X1a_2003:
    case Standard::X3_2003: return 14;
    case Standard::X4: return 16;
    }
    return 16;
}

enum class ColorKind { None, DeviceGray, DeviceRgb, DeviceCmyk, IccGray, IccRgb, IccCmyk, Lab, Spot, Other };

struct Checker {
    fz_context *ctx = nullptr;
    pdf_document *pdf = nullptr;
    fz_document *doc = nullptr;
    Standard standard = Standard::X4;
    Options options;
    Report report;
    QString intentSpace; // "CMYK", "RGB", "Gray", or empty when unknown
    QHash<QString, int> perPage; // ruleId -> findings on the current page

    void add(const char *rule, const char *category, Severity severity, const QString &message, int page = -1,
             const QRectF &bounds = {})
    {
        const QString id = QString::fromLatin1(rule);
        if (page >= 0 && ++perPage[id] > kMaxPerRuleAndPage)
            return;
        Finding f;
        f.ruleId = id;
        f.category = QString::fromLatin1(category);
        f.severity = severity;
        f.message = message;
        f.page = page;
        f.bounds = bounds;
        report.findings.append(f);
    }

    // Called when a page is finished: tells the user how many findings were left out.
    void closePage(int page)
    {
        for (auto it = perPage.constBegin(); it != perPage.constEnd(); ++it) {
            if (it.value() <= kMaxPerRuleAndPage)
                continue;
            for (int i = report.findings.size() - 1; i >= 0; --i) {
                Finding &f = report.findings[i];
                if (f.page == page && f.ruleId == it.key()) {
                    f.message += QStringLiteral(" (and %1 more like this on the page)")
                                     .arg(it.value() - kMaxPerRuleAndPage);
                    break;
                }
            }
        }
        perPage.clear();
    }
};

// Names are compared as text so the code does not depend on which names a
// particular MuPDF version has in its built-in table.
bool isName(fz_context *ctx, pdf_obj *obj, const char *name)
{
    return pdf_is_name(ctx, obj) && std::strcmp(pdf_to_name(ctx, obj), name) == 0;
}

ColorKind classify(fz_context *ctx, fz_colorspace *cs)
{
    if (!cs)
        return ColorKind::None;
    if (fz_colorspace_is_indexed(ctx, cs))
        return classify(ctx, fz_base_colorspace(ctx, cs));
    const bool device = fz_colorspace_is_device(ctx, cs);
    switch (fz_colorspace_type(ctx, cs)) {
    case FZ_COLORSPACE_GRAY: return device ? ColorKind::DeviceGray : ColorKind::IccGray;
    case FZ_COLORSPACE_RGB: return device ? ColorKind::DeviceRgb : ColorKind::IccRgb;
    case FZ_COLORSPACE_CMYK: return device ? ColorKind::DeviceCmyk : ColorKind::IccCmyk;
    case FZ_COLORSPACE_LAB: return ColorKind::Lab;
    case FZ_COLORSPACE_SEPARATION: return ColorKind::Spot;
    default: return ColorKind::Other;
    }
}

// ---------------------------------------------------------------- page device

struct PageDevice {
    fz_device super;
    Checker *c;
    int page;
    fz_matrix view;
    const QSet<QString> *unembedded; // font names without an embedded font program
    QSet<QString> *located;          // those of them seen in text on the page
};

QRectF viewRect(PageDevice *d, fz_rect r)
{
    return toQRect(fz_transform_rect(r, d->view));
}

void checkColor(fz_context *ctx, PageDevice *d, fz_colorspace *cs, fz_rect bounds, const char *what)
{
    Checker *c = d->c;
    const ColorKind kind = classify(ctx, cs);
    if (kind == ColorKind::None || kind == ColorKind::Other)
        return;
    const QString label = colorSpaceLabel(ctx, cs);
    const QString object = QString::fromLatin1(what);

    if (isX1a(c->standard)) {
        switch (kind) {
        case ColorKind::DeviceRgb:
        case ColorKind::IccRgb:
            c->add("color.rgb", "Colour", Severity::Error,
                   QStringLiteral("%1 uses RGB colour (%2). PDF/X-1a allows only CMYK, grey and spot colours.")
                       .arg(object, label),
                   d->page, viewRect(d, bounds));
            break;
        case ColorKind::Lab:
            c->add("color.lab", "Colour", Severity::Error,
                   QStringLiteral("%1 uses Lab colour. PDF/X-1a allows only CMYK, grey and spot colours.").arg(object),
                   d->page, viewRect(d, bounds));
            break;
        case ColorKind::IccCmyk:
        case ColorKind::IccGray:
            c->add("color.icc", "Colour", Severity::Error,
                   QStringLiteral("%1 uses ICC-based colour (%2). PDF/X-1a allows only device CMYK, "
                                  "device grey and spot colours.")
                       .arg(object, label),
                   d->page, viewRect(d, bounds));
            break;
        case ColorKind::Spot: {
            const ColorKind alt = classify(ctx, cs->u.separation.base);
            if (alt != ColorKind::DeviceCmyk && alt != ColorKind::DeviceGray && alt != ColorKind::None)
                c->add("color.spotAlternate", "Colour", Severity::Warning,
                       QStringLiteral("%1: the spot colour %2 has an alternate colour that is not device "
                                      "CMYK or grey.")
                           .arg(object, label),
                       d->page, viewRect(d, bounds));
            break;
        }
        default:
            break;
        }
        return;
    }

    // PDF/X-3 and PDF/X-4: device colour has to match the printing condition.
    // An unknown printing condition (named, without profile) is taken as CMYK.
    const QString intent = c->intentSpace.isEmpty() ? QStringLiteral("CMYK") : c->intentSpace;
    if (kind == ColorKind::DeviceRgb && intent != QLatin1String("RGB"))
        c->add("color.deviceRgb", "Colour", Severity::Error,
               QStringLiteral("%1 uses device RGB, but the output intent is %2. RGB must be ICC-based "
                              "(colour-managed) or converted.")
                   .arg(object, intent),
               d->page, viewRect(d, bounds));
    else if (kind == ColorKind::DeviceCmyk && intent != QLatin1String("CMYK"))
        c->add("color.deviceCmyk", "Colour", Severity::Error,
               QStringLiteral("%1 uses device CMYK, but the output intent is %2.").arg(object, intent), d->page,
               viewRect(d, bounds));
}

void checkOpacity(PageDevice *d, float alpha, fz_rect bounds, const char *what)
{
    if (isX4(d->c->standard) || alpha >= 0.999f)
        return;
    d->c->add("transparency.opacity", "Transparency", Severity::Error,
              QStringLiteral("%1 is %2 % opaque. %3 does not allow transparency.")
                  .arg(QString::fromLatin1(what))
                  .arg(qRound(alpha * 100))
                  .arg(standardName(d->c->standard)),
              d->page, viewRect(d, bounds));
}

void pfFillPath(fz_context *ctx, fz_device *dev, const fz_path *path, int, fz_matrix ctm, fz_colorspace *cs,
                const float *, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_bound_path(ctx, path, nullptr, ctm);
    checkColor(ctx, d, cs, r, "A filled shape");
    checkOpacity(d, alpha, r, "A filled shape");
}

void pfStrokePath(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke,
                  fz_matrix ctm, fz_colorspace *cs, const float *, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_bound_path(ctx, path, stroke, ctm);
    checkColor(ctx, d, cs, r, "A line");
    checkOpacity(d, alpha, r, "A line");
}

void checkFonts(fz_context *ctx, PageDevice *d, const fz_text *text, fz_rect bounds)
{
    for (const fz_text_span *span = text->head; span; span = span->next) {
        const QString name = QString::fromUtf8(fz_font_name(ctx, span->font));
        const bool listed = d->unembedded->contains(name);
        if (!listed && !fz_font_flags(span->font)->ft_substitute)
            continue;
        if (listed)
            d->located->insert(name);
        d->c->add("font.notEmbedded", "Fonts", Severity::Error,
                  QStringLiteral("Text uses the font %1, which is not embedded in the file.").arg(name), d->page,
                  viewRect(d, bounds));
        return; // one finding per text object
    }
}

void pfFillText(fz_context *ctx, fz_device *dev, const fz_text *text, fz_matrix ctm, fz_colorspace *cs,
                const float *, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_bound_text(ctx, text, nullptr, ctm);
    checkColor(ctx, d, cs, r, "Text");
    checkOpacity(d, alpha, r, "Text");
    checkFonts(ctx, d, text, r);
}

void pfStrokeText(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_stroke_state *stroke,
                  fz_matrix ctm, fz_colorspace *cs, const float *, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_bound_text(ctx, text, stroke, ctm);
    checkColor(ctx, d, cs, r, "Outlined text");
    checkOpacity(d, alpha, r, "Outlined text");
    checkFonts(ctx, d, text, r);
}

void pfIgnoreText(fz_context *ctx, fz_device *dev, const fz_text *text, fz_matrix ctm)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    checkFonts(ctx, d, text, fz_bound_text(ctx, text, nullptr, ctm)); // invisible text still needs its font
}

void pfImage(fz_context *ctx, fz_device *dev, fz_image *image, fz_matrix ctm, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    checkColor(ctx, d, image->colorspace, r, "An image");
    checkOpacity(d, alpha, r, "An image");

    const double limit = d->c->options.minImagePpi;
    const double widthIn = std::hypot(ctm.a, ctm.b) / 72.0;
    const double heightIn = std::hypot(ctm.c, ctm.d) / 72.0;
    if (limit > 0 && widthIn > 0 && heightIn > 0 && image->w > 16 && image->h > 16) {
        const double ppi = qMin(image->w / widthIn, image->h / heightIn);
        if (ppi < limit)
            d->c->add("image.lowResolution", "Image resolution", Severity::Warning,
                      QStringLiteral("An image has only %1 ppi at the size it is placed (%2 × %3 pixels). "
                                     "Below %4 ppi it may look blurred in print.")
                          .arg(qRound(ppi))
                          .arg(image->w)
                          .arg(image->h)
                          .arg(qRound(limit)),
                      d->page, viewRect(d, r));
    }
}

void pfImageMask(fz_context *ctx, fz_device *dev, fz_image *, fz_matrix ctm, fz_colorspace *cs, const float *,
                 float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    checkColor(ctx, d, cs, r, "A one-colour image");
    checkOpacity(d, alpha, r, "A one-colour image");
}

void pfShade(fz_context *ctx, fz_device *dev, fz_shade *shade, fz_matrix ctm, float alpha, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    const fz_rect r = fz_bound_shade(ctx, shade, ctm);
    checkColor(ctx, d, shade->colorspace, r, "A gradient");
    checkOpacity(d, alpha, r, "A gradient");
}

void pfBeginMask(fz_context *, fz_device *dev, fz_rect area, int, fz_colorspace *, const float *, fz_color_params)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    if (!isX4(d->c->standard))
        d->c->add("transparency.softMask", "Transparency", Severity::Error,
                  QStringLiteral("A soft mask is used. %1 does not allow transparency.")
                      .arg(standardName(d->c->standard)),
                  d->page, viewRect(d, area));
}

void pfBeginGroup(fz_context *, fz_device *dev, fz_rect area, fz_colorspace *, int, int, int blendmode,
                  float alpha)
{
    auto *d = reinterpret_cast<PageDevice *>(dev);
    if (isX4(d->c->standard))
        return;
    if (blendmode != 0)
        d->c->add("transparency.blendMode", "Transparency", Severity::Error,
                  QStringLiteral("A blend mode other than Normal is used. %1 does not allow transparency.")
                      .arg(standardName(d->c->standard)),
                  d->page, viewRect(d, area));
    else if (alpha < 0.999f)
        checkOpacity(d, alpha, area, "A group of objects");
    else
        d->c->add("transparency.group", "Transparency", Severity::Error,
                  QStringLiteral("A transparency group is used. %1 does not allow transparency.")
                      .arg(standardName(d->c->standard)),
                  d->page, viewRect(d, area));
}

// --------------------------------------------------------- structural checks

QString xmpValue(const QString &xmp, const QString &name)
{
    const QRegularExpression re(QRegularExpression::escape(name)
                                + QStringLiteral("\\s*(?:=\\s*[\"']([^\"']*)[\"']|>\\s*([^<]*?)\\s*<)"));
    const auto m = re.match(xmp);
    if (!m.hasMatch())
        return {};
    return m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
}

QString loadXmp(fz_context *ctx, pdf_obj *root)
{
    QString xmp;
    fz_buffer *buf = nullptr;
    fz_var(buf);
    fz_try(ctx)
    {
        pdf_obj *metadata = pdf_dict_gets(ctx, root, "Metadata");
        if (pdf_is_stream(ctx, metadata)) {
            buf = pdf_load_stream(ctx, metadata);
            unsigned char *bytes = nullptr;
            const size_t len = fz_buffer_storage(ctx, buf, &bytes);
            xmp = QString::fromUtf8(reinterpret_cast<const char *>(bytes), qsizetype(len));
        }
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx)
    {
    }
    return xmp;
}

void checkIdentification(Checker &c, pdf_obj *info, const QString &xmp)
{
    fz_context *ctx = c.ctx;
    const QString infoVersion = pdfText(ctx, pdf_dict_gets(ctx, info, "GTS_PDFXVersion"));
    const QString conformance = pdfText(ctx, pdf_dict_gets(ctx, info, "GTS_PDFXConformance"));
    const QString xmpVersion = xmpValue(xmp, QStringLiteral("GTS_PDFXVersion"));
    const QString wanted = standardName(c.standard);

    QString claimed;
    QString expectedVersion;
    switch (c.standard) {
    case Standard::X1a_2001:
        claimed = infoVersion;
        expectedVersion = QStringLiteral("PDF/X-1:2001");
        break;
    case Standard::X1a_2003:
    case Standard::X3_2002:
    case Standard::X3_2003:
        claimed = infoVersion;
        expectedVersion = wanted;
        break;
    case Standard::X4:
        claimed = xmpVersion;
        expectedVersion = wanted;
        break;
    }

    if (claimed.isEmpty()) {
        const QString other = isX4(c.standard) ? infoVersion : xmpVersion;
        c.add("id.missing", "Identification", Severity::Error,
              other.isEmpty()
                  ? QStringLiteral("The file is not marked as %1: the version key is missing.").arg(wanted)
                  : QStringLiteral("The file is marked as %1, not as %2.").arg(other, wanted));
        return;
    }
    if (!claimed.startsWith(expectedVersion)) {
        c.add("id.mismatch", "Identification", Severity::Error,
              QStringLiteral("The file is marked as %1, not as %2.")
                  .arg(conformance.isEmpty() ? claimed : conformance, wanted));
        return;
    }
    if (isX1a(c.standard) && !conformance.startsWith(wanted))
        c.add("id.conformance", "Identification", Severity::Error,
              conformance.isEmpty()
                  ? QStringLiteral("The conformance key (GTS_PDFXConformance) is missing; it must say %1.").arg(wanted)
                  : QStringLiteral("The conformance key says %1, not %2.").arg(conformance, wanted));
}

void checkFile(Checker &c, pdf_obj *trailer)
{
    fz_context *ctx = c.ctx;
    const int limit = baseVersion(c.standard);
    if (c.pdf->version > limit)
        c.add("file.version", "File", Severity::Warning,
              QStringLiteral("The file is PDF %1.%2; %3 is based on PDF %4.%5. Some printers reject newer versions.")
                  .arg(c.pdf->version / 10)
                  .arg(c.pdf->version % 10)
                  .arg(standardName(c.standard))
                  .arg(limit / 10)
                  .arg(limit % 10));
    if (pdf_dict_gets(ctx, trailer, "Encrypt"))
        c.add("file.encrypted", "File", Severity::Error,
              QStringLiteral("The file is encrypted (has security settings). PDF/X files must not be."));
}

void checkOutputIntent(Checker &c, pdf_obj *root)
{
    fz_context *ctx = c.ctx;
    pdf_obj *list = pdf_dict_gets(ctx, root, "OutputIntents");
    pdf_obj *intent = nullptr;
    int count = 0;
    for (int i = 0; i < pdf_array_len(ctx, list); ++i) {
        pdf_obj *oi = pdf_array_get(ctx, list, i);
        if (pdfText(ctx, pdf_dict_gets(ctx, oi, "S")) == QLatin1String("GTS_PDFX")) {
            ++count;
            if (!intent)
                intent = oi;
        }
    }
    if (count == 0) {
        c.add("intent.missing", "Output intent", Severity::Error,
              QStringLiteral("There is no PDF/X output intent, so the file does not say which printing "
                             "condition it was made for."));
        return;
    }
    if (count > 1)
        c.add("intent.multiple", "Output intent", Severity::Error,
              QStringLiteral("There are %1 PDF/X output intents; exactly one is required.").arg(count));

    const QString identifier = pdfText(ctx, pdf_dict_gets(ctx, intent, "OutputConditionIdentifier"));
    if (identifier.isEmpty())
        c.add("intent.identifier", "Output intent", Severity::Error,
              QStringLiteral("The output intent has no name for the printing condition "
                             "(OutputConditionIdentifier)."));

    pdf_obj *dest = pdf_dict_gets(ctx, intent, "DestOutputProfile");
    if (!pdf_is_stream(ctx, dest)) {
        const QString registry = pdfText(ctx, pdf_dict_gets(ctx, intent, "RegistryName"));
        if (isX4(c.standard))
            c.add("intent.profile", "Output intent", Severity::Error,
                  QStringLiteral("The output intent has no embedded ICC profile. PDF/X-4 requires one."));
        else if (registry.isEmpty())
            c.add("intent.profile", "Output intent", Severity::Error,
                  QStringLiteral("The output intent has neither an embedded ICC profile nor a registry name, "
                                 "so the printing condition cannot be identified."));
        return;
    }

    QByteArray data;
    fz_buffer *buf = nullptr;
    fz_var(buf);
    fz_try(ctx)
    {
        buf = pdf_load_stream(ctx, dest);
        unsigned char *bytes = nullptr;
        const size_t len = fz_buffer_storage(ctx, buf, &bytes);
        data = QByteArray(reinterpret_cast<const char *>(bytes), qsizetype(len));
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx)
    {
    }

    engine::ProfileInfo profile;
    if (!engine::ProfileLibrary::inspectData(data, &profile)) {
        c.add("intent.profileInvalid", "Output intent", Severity::Error,
              QStringLiteral("The ICC profile in the output intent is damaged and cannot be read."));
        return;
    }
    c.intentSpace = profile.colorSpace;
    if (profile.deviceClass != QLatin1String("Output"))
        c.add("intent.profileClass", "Output intent", Severity::Error,
              QStringLiteral("The output intent profile \"%1\" is a %2 profile; it must be a printer "
                             "(output) profile.")
                  .arg(profile.description, profile.deviceClass.toLower()));
    if (isX1a(c.standard) && profile.colorSpace != QLatin1String("CMYK"))
        c.add("intent.profileSpace", "Output intent", Severity::Error,
              QStringLiteral("The output intent profile is %1; PDF/X-1a requires a CMYK printing condition.")
                  .arg(profile.colorSpace));
}

void checkMetadata(Checker &c, pdf_obj *root, pdf_obj *info, const QString &xmp)
{
    fz_context *ctx = c.ctx;
    if (isX4(c.standard) && xmp.isEmpty())
        c.add("meta.xmp", "Metadata", Severity::Error,
              QStringLiteral("The file has no XMP metadata. PDF/X-4 requires it."));
    Q_UNUSED(root);

    static const QRegularExpression titleRe(QStringLiteral("<dc:title>.*?<rdf:li[^>]*>\\s*([^<]+?)\\s*<"),
                                            QRegularExpression::DotMatchesEverythingOption);
    const bool hasTitle = !pdfText(ctx, pdf_dict_gets(ctx, info, "Title")).trimmed().isEmpty()
                          || titleRe.match(xmp).hasMatch();
    if (!hasTitle)
        c.add("meta.title", "Metadata", Severity::Error, QStringLiteral("The document has no title."));

    const bool hasCreated = pdf_dict_gets(ctx, info, "CreationDate")
                            || !xmpValue(xmp, QStringLiteral("xmp:CreateDate")).isEmpty();
    if (!hasCreated)
        c.add("meta.creationDate", "Metadata", Severity::Error,
              QStringLiteral("The document has no creation date."));
    const bool hasModified = pdf_dict_gets(ctx, info, "ModDate")
                             || !xmpValue(xmp, QStringLiteral("xmp:ModifyDate")).isEmpty();
    if (!hasModified)
        c.add("meta.modDate", "Metadata", Severity::Error,
              QStringLiteral("The document has no modification date."));

    QString trapped = pdfText(ctx, pdf_dict_gets(ctx, info, "Trapped"));
    if (trapped.isEmpty())
        trapped = xmpValue(xmp, QStringLiteral("pdf:Trapped"));
    if (trapped != QLatin1String("True") && trapped != QLatin1String("False"))
        c.add("meta.trapped", "Metadata", Severity::Error,
              trapped.isEmpty()
                  ? QStringLiteral("The file does not say whether it has been trapped (Trapped must be "
                                   "True or False).")
                  : QStringLiteral("Trapped is \"%1\"; it must be True or False.").arg(trapped));
}

void checkCatalog(Checker &c, pdf_obj *root)
{
    fz_context *ctx = c.ctx;
    pdf_obj *names = pdf_dict_gets(ctx, root, "Names");
    if (pdf_dict_gets(ctx, names, "JavaScript"))
        c.add("forbidden.javascript", "Forbidden features", Severity::Error,
              QStringLiteral("The file contains JavaScript."));
    if (pdf_dict_gets(ctx, root, "OpenAction") || pdf_dict_gets(ctx, root, "AA")) {
        pdf_obj *action = pdf_dict_gets(ctx, root, "OpenAction");
        const bool script = pdfText(ctx, pdf_dict_gets(ctx, action, "S")) == QLatin1String("JavaScript");
        if (script)
            c.add("forbidden.javascript", "Forbidden features", Severity::Error,
                  QStringLiteral("The file runs JavaScript when it is opened."));
        else
            c.add("forbidden.action", "Forbidden features", Severity::Warning,
                  QStringLiteral("The file has an action that runs when it is opened."));
    }
    if (!isX4(c.standard) && pdf_dict_gets(ctx, root, "OCProperties"))
        c.add("forbidden.layers", "Forbidden features", Severity::Error,
              QStringLiteral("The file contains layers (optional content). %1 does not allow them.")
                  .arg(standardName(c.standard)));
    if (!isX4(c.standard) && pdf_dict_gets(ctx, names, "EmbeddedFiles"))
        c.add("forbidden.embeddedFiles", "Forbidden features", Severity::Warning,
              QStringLiteral("The file contains attached files."));
}

// Looks at every object in the file once, for things that are forbidden
// wherever they occur.
void checkAllObjects(Checker &c)
{
    fz_context *ctx = c.ctx;
    int lzw = 0, jpx = 0, jbig2 = 0, postscript = 0, opi = 0, alternates = 0, softMaskImages = 0, transfer = 0;
    const int count = pdf_xref_len(ctx, c.pdf);
    for (int num = 1; num < count; ++num) {
        pdf_obj *obj = nullptr;
        fz_var(obj);
        fz_try(ctx)
        {
            obj = pdf_load_object(ctx, c.pdf, num);
            if (pdf_is_dict(ctx, obj)) {
                if (pdf_obj_num_is_stream(ctx, c.pdf, num)) {
                    pdf_obj *filter = pdf_dict_gets(ctx, obj, "Filter");
                    const int n = pdf_is_array(ctx, filter) ? pdf_array_len(ctx, filter) : 1;
                    for (int i = 0; i < n; ++i) {
                        pdf_obj *f = pdf_is_array(ctx, filter) ? pdf_array_get(ctx, filter, i) : filter;
                        lzw += isName(ctx, f, "LZWDecode");
                        jpx += isName(ctx, f, "JPXDecode");
                        jbig2 += isName(ctx, f, "JBIG2Decode");
                    }
                }
                pdf_obj *subtype = pdf_dict_gets(ctx, obj, "Subtype");
                const bool xobject = isName(ctx, pdf_dict_gets(ctx, obj, "Type"), "XObject")
                                     || isName(ctx, subtype, "Image")
                                     || isName(ctx, subtype, "Form");
                if (isName(ctx, subtype, "PS") || pdf_dict_gets(ctx, obj, "Subtype2"))
                    ++postscript;
                if (xobject && pdf_dict_gets(ctx, obj, "OPI"))
                    ++opi;
                if (xobject && pdf_dict_gets(ctx, obj, "Alternates"))
                    ++alternates;
                if (isName(ctx, subtype, "Image")) {
                    pdf_obj *smask = pdf_dict_gets(ctx, obj, "SMask");
                    if (smask && !isName(ctx, smask, "None"))
                        ++softMaskImages;
                }
                if (isName(ctx, pdf_dict_gets(ctx, obj, "Type"), "ExtGState")) {
                    pdf_obj *tr2 = pdf_dict_gets(ctx, obj, "TR2");
                    if (pdf_dict_gets(ctx, obj, "TR") || (tr2 && !isName(ctx, tr2, "Default")))
                        ++transfer;
                }
            }
        }
        fz_always(ctx)
        {
            pdf_drop_obj(ctx, obj);
        }
        fz_catch(ctx)
        {
            // An unreadable object is not a PDF/X question; skip it.
        }
    }

    const QString name = standardName(c.standard);
    if (postscript)
        c.add("forbidden.postscript", "Forbidden features", Severity::Error,
              QStringLiteral("The file contains embedded PostScript (%1 object(s)).").arg(postscript));
    if (opi)
        c.add("forbidden.opi", "Forbidden features", Severity::Error,
              QStringLiteral("The file contains OPI references to external images (%1).").arg(opi));
    if (alternates)
        c.add("forbidden.alternates", "Forbidden features", Severity::Warning,
              QStringLiteral("%1 image(s) have alternate versions.").arg(alternates));
    if (transfer)
        c.add("forbidden.transferFunction", "Forbidden features", Severity::Error,
              QStringLiteral("The file contains transfer functions (%1), which change tone values at output.")
                  .arg(transfer));
    if (softMaskImages && !isX4(c.standard))
        c.add("transparency.softMaskImage", "Transparency", Severity::Error,
              QStringLiteral("%1 image(s) have a soft mask (transparency). %2 does not allow transparency.")
                  .arg(softMaskImages)
                  .arg(name));
    if (jpx && !isX4(c.standard))
        c.add("forbidden.jpeg2000", "Forbidden features", Severity::Error,
              QStringLiteral("%1 object(s) use JPEG 2000 compression, which %2 does not allow.").arg(jpx).arg(name));
    if (lzw)
        c.add("forbidden.lzw", "Forbidden features", Severity::Warning,
              QStringLiteral("%1 object(s) use LZW compression.").arg(lzw));
    if (jbig2 && baseVersion(c.standard) < 14)
        c.add("forbidden.jbig2", "Forbidden features", Severity::Warning,
              QStringLiteral("%1 object(s) use JBIG2 compression, which is newer than PDF 1.3.").arg(jbig2));
}

bool inside(fz_rect inner, fz_rect outer)
{
    const float t = 0.01f;
    return inner.x0 >= outer.x0 - t && inner.y0 >= outer.y0 - t && inner.x1 <= outer.x1 + t
           && inner.y1 <= outer.y1 + t;
}

struct Boxes {
    fz_rect media = fz_empty_rect, crop = fz_empty_rect, bleed = fz_empty_rect, trim = fz_empty_rect,
            art = fz_empty_rect;
    bool hasMedia = false, hasCrop = false, hasBleed = false, hasTrim = false, hasArt = false;
};

Boxes readBoxes(fz_context *ctx, pdf_obj *page)
{
    Boxes b;
    auto read = [&](pdf_obj *key, bool inherit, fz_rect *out) {
        pdf_obj *o = inherit ? pdf_dict_get_inheritable(ctx, page, key) : pdf_dict_get(ctx, page, key);
        if (!pdf_is_array(ctx, o) || pdf_array_len(ctx, o) != 4)
            return false;
        fz_rect r = pdf_to_rect(ctx, o);
        *out = fz_make_rect(fz_min(r.x0, r.x1), fz_min(r.y0, r.y1), fz_max(r.x0, r.x1), fz_max(r.y0, r.y1));
        return true;
    };
    b.hasMedia = read(PDF_NAME(MediaBox), true, &b.media);
    b.hasCrop = read(PDF_NAME(CropBox), true, &b.crop);
    b.hasBleed = read(PDF_NAME(BleedBox), false, &b.bleed);
    b.hasTrim = read(PDF_NAME(TrimBox), false, &b.trim);
    b.hasArt = read(PDF_NAME(ArtBox), false, &b.art);
    return b;
}

void checkBoxes(Checker &c, int page, const Boxes &b, fz_matrix toView)
{
    auto view = [&](fz_rect r) { return toQRect(fz_transform_rect(r, toView)); };
    if (!b.hasMedia) {
        c.add("box.mediaMissing", "Page boxes", Severity::Error, QStringLiteral("The page has no MediaBox."), page);
        return;
    }
    if (!b.hasTrim && !b.hasArt)
        c.add("box.trimMissing", "Page boxes", Severity::Error,
              QStringLiteral("The page has no TrimBox (or ArtBox), so the finished size is not defined."), page);
    if (b.hasTrim && b.hasArt)
        c.add("box.trimAndArt", "Page boxes", Severity::Error,
              QStringLiteral("The page has both a TrimBox and an ArtBox; only one of them is allowed."), page,
              view(b.art));

    struct Rule {
        bool present;
        fz_rect inner;
        const char *innerName;
        bool outerPresent;
        fz_rect outer;
        const char *outerName;
    };
    const Rule rules[] = {
        { b.hasTrim, b.trim, "TrimBox", b.hasBleed, b.bleed, "BleedBox" },
        { b.hasArt, b.art, "ArtBox", b.hasBleed, b.bleed, "BleedBox" },
        { b.hasTrim, b.trim, "TrimBox", b.hasCrop, b.crop, "CropBox" },
        { b.hasArt, b.art, "ArtBox", b.hasCrop, b.crop, "CropBox" },
        { b.hasBleed, b.bleed, "BleedBox", b.hasCrop, b.crop, "CropBox" },
        { b.hasTrim, b.trim, "TrimBox", true, b.media, "MediaBox" },
        { b.hasArt, b.art, "ArtBox", true, b.media, "MediaBox" },
        { b.hasBleed, b.bleed, "BleedBox", true, b.media, "MediaBox" },
        { b.hasCrop, b.crop, "CropBox", true, b.media, "MediaBox" },
    };
    for (const Rule &r : rules) {
        if (r.present && r.outerPresent && !inside(r.inner, r.outer))
            c.add("box.nesting", "Page boxes", Severity::Error,
                  QStringLiteral("The %1 extends beyond the %2.")
                      .arg(QLatin1String(r.innerName), QLatin1String(r.outerName)),
                  page, view(r.inner));
    }
}

void checkAnnotations(Checker &c, int page, pdf_obj *pageObj, const Boxes &b, fz_matrix toView)
{
    fz_context *ctx = c.ctx;
    if (!b.hasMedia)
        return;
    const fz_rect printed = b.hasBleed ? b.bleed : (b.hasTrim ? b.trim : (b.hasArt ? b.art : b.media));
    pdf_obj *annots = pdf_dict_gets(ctx, pageObj, "Annots");
    for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
        pdf_obj *annot = pdf_array_get(ctx, annots, i);
        const QString subtype = pdfText(ctx, pdf_dict_gets(ctx, annot, "Subtype"));
        if (subtype == QLatin1String("TrapNet") || subtype == QLatin1String("PrinterMark"))
            continue;
        const fz_rect rect = pdf_to_rect(ctx, pdf_dict_gets(ctx, annot, "Rect"));
        const fz_rect r = fz_make_rect(fz_min(rect.x0, rect.x1), fz_min(rect.y0, rect.y1),
                                       fz_max(rect.x0, rect.x1), fz_max(rect.y0, rect.y1));
        const fz_rect overlap = fz_intersect_rect(r, printed);
        if (fz_is_empty_rect(overlap) && !fz_is_empty_rect(r))
            continue; // lies outside the printed area: allowed
        c.add("annot.inside", "Annotations", Severity::Error,
              QStringLiteral("A %1 annotation (comment, link or form field) lies inside the printed area.")
                  .arg(subtype.isEmpty() ? QStringLiteral("nameless") : subtype),
              page, toQRect(fz_transform_rect(r, toView)));
    }
    if (pdf_dict_gets(ctx, pageObj, "AA"))
        c.add("forbidden.action", "Forbidden features", Severity::Warning,
              QStringLiteral("The page has an action attached to it."), page);
}

// Collects the names of fonts that have no embedded font program, following
// form XObjects and patterns. `visited` stops loops.
void collectUnembeddedFonts(fz_context *ctx, pdf_obj *resources, QSet<QString> &names, std::set<int> &visited,
                            int depth = 0)
{
    if (!pdf_is_dict(ctx, resources) || depth > 12)
        return;
    pdf_obj *fonts = pdf_dict_gets(ctx, resources, "Font");
    for (int i = 0; i < pdf_dict_len(ctx, fonts); ++i) {
        pdf_obj *font = pdf_dict_get_val(ctx, fonts, i);
        const int num = pdf_to_num(ctx, font);
        if (num > 0 && !visited.insert(num).second)
            continue;
        pdf_obj *subtype = pdf_dict_gets(ctx, font, "Subtype");
        if (isName(ctx, subtype, "Type3")) {
            collectUnembeddedFonts(ctx, pdf_dict_gets(ctx, font, "Resources"), names, visited, depth + 1);
            continue;
        }
        pdf_obj *described = font;
        if (isName(ctx, subtype, "Type0"))
            described = pdf_array_get(ctx, pdf_dict_gets(ctx, font, "DescendantFonts"), 0);
        pdf_obj *descriptor = pdf_dict_gets(ctx, described, "FontDescriptor");
        const bool embedded = pdf_dict_gets(ctx, descriptor, "FontFile")
                              || pdf_dict_gets(ctx, descriptor, "FontFile2")
                              || pdf_dict_gets(ctx, descriptor, "FontFile3");
        if (!embedded) {
            QString name = pdfText(ctx, pdf_dict_gets(ctx, font, "BaseFont"));
            names.insert(name.isEmpty() ? QStringLiteral("(unnamed font)") : name);
        }
    }
    for (const char *key : { "XObject", "Pattern" }) {
        pdf_obj *dict = pdf_dict_gets(ctx, resources, key);
        for (int i = 0; i < pdf_dict_len(ctx, dict); ++i) {
            pdf_obj *child = pdf_dict_get_val(ctx, dict, i);
            const int num = pdf_to_num(ctx, child);
            if (num > 0 && !visited.insert(num).second)
                continue;
            collectUnembeddedFonts(ctx, pdf_dict_gets(ctx, child, "Resources"), names, visited, depth + 1);
        }
    }
}

void checkPage(Checker &c, int pageNo)
{
    fz_context *ctx = c.ctx;
    fz_page *page = nullptr;
    fz_device *dev = nullptr;
    QSet<QString> unembedded, located;
    fz_var(page);
    fz_var(dev);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, c.doc, pageNo);
        pdf_page *pp = pdf_page_from_fz_page(ctx, page);
        const fz_matrix view = viewMatrix(fz_bound_page(ctx, page), 0);
        fz_rect cropped;
        fz_matrix pm;
        pdf_page_transform(ctx, pp, &cropped, &pm);
        const fz_matrix rawToView = fz_concat(pm, view);

        const Boxes boxes = readBoxes(ctx, pp->obj);
        checkBoxes(c, pageNo, boxes, rawToView);
        checkAnnotations(c, pageNo, pp->obj, boxes, rawToView);

        std::set<int> visited;
        collectUnembeddedFonts(ctx, pdf_dict_get_inheritable(ctx, pp->obj, PDF_NAME(Resources)), unembedded,
                               visited);

        PageDevice *d = fz_new_derived_device(ctx, PageDevice);
        dev = &d->super;
        d->c = &c;
        d->page = pageNo;
        d->view = view;
        d->unembedded = &unembedded;
        d->located = &located;
        d->super.fill_path = pfFillPath;
        d->super.stroke_path = pfStrokePath;
        d->super.fill_text = pfFillText;
        d->super.stroke_text = pfStrokeText;
        d->super.ignore_text = pfIgnoreText;
        d->super.fill_image = pfImage;
        d->super.fill_image_mask = pfImageMask;
        d->super.fill_shade = pfShade;
        d->super.begin_mask = pfBeginMask;
        d->super.begin_group = pfBeginGroup;
        fz_run_page_contents(ctx, page, dev, fz_identity, nullptr);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        c.add("file.damagedPage", "File", Severity::Error,
              QStringLiteral("The page could not be read completely: %1")
                  .arg(QString::fromUtf8(fz_caught_message(ctx))),
              pageNo);
    }

    // Fonts that are listed for the page but were not seen in any text object.
    QStringList rest;
    for (const QString &name : unembedded)
        if (!located.contains(name))
            rest << name;
    rest.sort();
    for (const QString &name : rest)
        c.add("font.notEmbedded", "Fonts", Severity::Error,
              QStringLiteral("The font %1 is not embedded in the file.").arg(name), pageNo);
    c.closePage(pageNo);
}

} // namespace

Report run(engine::Document &document, Standard standard, const Options &options,
           const std::function<bool(int, int)> &progress)
{
    Checker c;
    c.ctx = document.context();
    c.standard = standard;
    c.options = options;
    c.report.standard = standard;
    c.doc = document.handle();
    c.pdf = pdf_specifics(c.ctx, c.doc);
    if (!c.pdf) {
        c.add("file.notPdf", "File", Severity::Error, QStringLiteral("This is not a PDF file."));
        return c.report;
    }

    fz_context *ctx = c.ctx;
    QString xmp;
    fz_try(ctx)
    {
        pdf_obj *trailer = pdf_trailer(ctx, c.pdf);
        pdf_obj *root = pdf_dict_gets(ctx, trailer, "Root");
        pdf_obj *info = pdf_dict_gets(ctx, trailer, "Info");
        xmp = loadXmp(ctx, root);
        checkIdentification(c, info, xmp);
        checkFile(c, trailer);
        checkOutputIntent(c, root);
        checkMetadata(c, root, info, xmp);
        checkCatalog(c, root);
    }
    fz_catch(ctx)
    {
        c.add("file.damaged", "File", Severity::Error,
              QStringLiteral("The document structure could not be read: %1")
                  .arg(QString::fromUtf8(fz_caught_message(ctx))));
    }
    checkAllObjects(c);

    const int pages = document.info().pageCount;
    for (int page = 0; page < pages; ++page) {
        if (progress && !progress(page, pages)) {
            c.report.cancelled = true;
            break;
        }
        checkPage(c, page);
        c.report.pagesChecked = page + 1;
    }
    return c.report;
}

} // namespace preflight
