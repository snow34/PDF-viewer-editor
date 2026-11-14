#include "Types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace preflight {

QList<Standard> allStandards()
{
    return { Standard::X1a_2001, Standard::X1a_2003, Standard::X3_2002, Standard::X3_2003, Standard::X4 };
}

QString standardName(Standard s)
{
    switch (s) {
    case Standard::X1a_2001: return QStringLiteral("PDF/X-1a:2001");
    case Standard::X1a_2003: return QStringLiteral("PDF/X-1a:2003");
    case Standard::X3_2002: return QStringLiteral("PDF/X-3:2002");
    case Standard::X3_2003: return QStringLiteral("PDF/X-3:2003");
    case Standard::X4: return QStringLiteral("PDF/X-4");
    }
    return {};
}

bool standardFromClaim(const QString &claim, Standard *out)
{
    Standard s;
    if (claim.contains(QLatin1String("X-4")))
        s = Standard::X4;
    else if (claim.contains(QLatin1String("X-3")))
        s = claim.contains(QLatin1String("2003")) ? Standard::X3_2003 : Standard::X3_2002;
    else if (claim.contains(QLatin1String("X-1")))
        s = claim.contains(QLatin1String("2003")) ? Standard::X1a_2003 : Standard::X1a_2001;
    else
        return false;
    if (out)
        *out = s;
    return true;
}

int Report::errors() const
{
    int n = 0;
    for (const Finding &f : findings)
        n += f.severity == Severity::Error;
    return n;
}

int Report::warnings() const
{
    int n = 0;
    for (const Finding &f : findings)
        n += f.severity == Severity::Warning;
    return n;
}

static QString verdict(const Report &r)
{
    const QString errors = r.errors() == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(r.errors());
    const QString warnings =
        r.warnings() == 1 ? QStringLiteral("1 warning") : QStringLiteral("%1 warnings").arg(r.warnings());
    if (r.cancelled)
        return QStringLiteral("Check was stopped before it finished");
    if (r.errors() == 0 && r.warnings() == 0)
        return QStringLiteral("Passed: no problems found");
    if (r.errors() == 0)
        return QStringLiteral("Passed with %1").arg(warnings);
    return r.warnings() == 0 ? QStringLiteral("Failed: %1").arg(errors)
                             : QStringLiteral("Failed: %1, %2").arg(errors, warnings);
}

QString toHtml(const Report &r, const QString &fileName)
{
    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><title>Preflight report</title></head><body>"
        "<h2>Preflight report</h2>"
        "<p><b>File:</b> %1<br><b>Checked against:</b> %2<br><b>Pages checked:</b> %3<br>"
        "<b>Result:</b> %4</p>")
                       .arg(fileName.toHtmlEscaped(), standardName(r.standard))
                       .arg(r.pagesChecked)
                       .arg(verdict(r).toHtmlEscaped());
    if (!r.findings.isEmpty()) {
        html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">"
                               "<tr><th>Severity</th><th>Category</th><th>Page</th>"
                               "<th>Problem</th><th>Rule</th></tr>");
        for (const Finding &f : r.findings) {
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                        .arg(f.severity == Severity::Error ? QStringLiteral("Error") : QStringLiteral("Warning"),
                             f.category.toHtmlEscaped(),
                             f.page < 0 ? QStringLiteral("–") : QString::number(f.page + 1),
                             f.message.toHtmlEscaped(), f.ruleId.toHtmlEscaped());
        }
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("<p><small>Checked by Prepress PDF. This is not a certified PDF/X "
                           "validation.</small></p></body></html>");
    return html;
}

QByteArray toJson(const Report &r, const QString &fileName)
{
    QJsonArray findings;
    for (const Finding &f : r.findings) {
        QJsonObject o{
            { QStringLiteral("rule"), f.ruleId },
            { QStringLiteral("category"), f.category },
            { QStringLiteral("severity"),
              f.severity == Severity::Error ? QStringLiteral("error") : QStringLiteral("warning") },
            { QStringLiteral("message"), f.message },
        };
        if (f.page >= 0)
            o.insert(QStringLiteral("page"), f.page + 1);
        if (f.bounds.isValid())
            o.insert(QStringLiteral("boundsPt"), QJsonArray{ f.bounds.x(), f.bounds.y(), f.bounds.width(),
                                                             f.bounds.height() });
        findings.append(o);
    }
    const QJsonObject root{
        { QStringLiteral("file"), fileName },
        { QStringLiteral("standard"), standardName(r.standard) },
        { QStringLiteral("pagesChecked"), r.pagesChecked },
        { QStringLiteral("passed"), r.passed() },
        { QStringLiteral("cancelled"), r.cancelled },
        { QStringLiteral("errors"), r.errors() },
        { QStringLiteral("warnings"), r.warnings() },
        { QStringLiteral("findings"), findings },
    };
    return QJsonDocument(root).toJson();
}

} // namespace preflight
