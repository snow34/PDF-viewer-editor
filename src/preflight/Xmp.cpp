#include "Xmp.h"

#include <QRegularExpression>
#include <QTimeZone>
#include <QXmlStreamReader>

namespace preflight {

Xmp parseXmp(const QString &packet)
{
    Xmp x;
    x.present = !packet.trimmed().isEmpty();
    if (!x.present)
        return x;

    QXmlStreamReader r(packet);
    int depth = 0;
    int descriptionDepth = -1; // depth of the rdf:Description being read
    int propertyDepth = -1;    // depth of the property element being read
    QString property, text;
    bool inItem = false, hasItem = false;
    QString itemText, itemLang, firstItem, defaultItem;

    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            ++depth;
            for (const QXmlStreamNamespaceDeclaration &d : r.namespaceDeclarations())
                x.prefixes.append({ d.prefix().toString(), d.namespaceUri().toString() });
            const QString ns = r.namespaceUri().toString();
            const QString name = r.name().toString();
            if (ns == xmpns::rdf && name == QLatin1String("Description")) {
                descriptionDepth = depth;
                // Short form: properties written as attributes.
                for (const QXmlStreamAttribute &a : r.attributes()) {
                    const QString ans = a.namespaceUri().toString();
                    if (ans.isEmpty() || ans == xmpns::rdf || ans == QLatin1String("http://www.w3.org/XML/1998/namespace"))
                        continue;
                    x.values.insert(ans + QLatin1Char('|') + a.name().toString(), a.value().toString().trimmed());
                }
            } else if (descriptionDepth >= 0 && depth == descriptionDepth + 1) {
                property = ns + QLatin1Char('|') + name;
                propertyDepth = depth;
                text.clear();
                hasItem = false;
                firstItem.clear();
                defaultItem.clear();
            } else if (propertyDepth >= 0 && ns == xmpns::rdf && name == QLatin1String("li")) {
                inItem = true;
                itemText.clear();
                itemLang = r.attributes().value(QLatin1String("http://www.w3.org/XML/1998/namespace"), QLatin1String("lang")).toString();
            }
        } else if (r.isCharacters()) {
            if (inItem)
                itemText += r.text();
            else if (propertyDepth >= 0 && depth == propertyDepth)
                text += r.text();
        } else if (r.isEndElement()) {
            if (inItem && r.namespaceUri() == xmpns::rdf && r.name() == QLatin1String("li")) {
                inItem = false;
                const QString item = itemText.trimmed();
                if (!hasItem)
                    firstItem = item;
                hasItem = true;
                if (itemLang == QLatin1String("x-default"))
                    defaultItem = item;
            }
            if (depth == propertyDepth) {
                const QString value = hasItem ? (defaultItem.isEmpty() ? firstItem : defaultItem) : text.trimmed();
                if (!x.values.contains(property))
                    x.values.insert(property, value);
                propertyDepth = -1;
            }
            if (depth == descriptionDepth)
                descriptionDepth = -1;
            --depth;
        }
    }
    if (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
        x.readable = false;
        x.problem = QStringLiteral("line %1: %2").arg(r.lineNumber()).arg(r.errorString());
    }
    return x;
}

QDateTime parsePdfDate(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(
        "^D?:?(\\d{4})(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?\\s*(?:([Zz])|([+-])(\\d{2})'?(\\d{2})?'?)?"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    auto part = [&](int i, int fallback) { return m.captured(i).isEmpty() ? fallback : m.captured(i).toInt(); };
    const QDate date(part(1, 0), part(2, 1), part(3, 1));
    const QTime time(part(4, 0), part(5, 0), part(6, 0));
    if (!date.isValid() || !time.isValid())
        return {};
    int offset = 0;
    if (!m.captured(8).isEmpty())
        offset = (m.captured(8) == QLatin1String("-") ? -1 : 1) * (part(9, 0) * 3600 + part(10, 0) * 60);
    return QDateTime(date, time, QTimeZone(offset)).toUTC();
}

QDateTime parseXmpDate(const QString &text)
{
    const QString t = text.trimmed();
    QDateTime d = QDateTime::fromString(t, Qt::ISODateWithMs);
    if (!d.isValid())
        d = QDateTime::fromString(t, Qt::ISODate);
    if (!d.isValid()) {
        const QDate day = QDate::fromString(t, Qt::ISODate);
        if (day.isValid())
            d = QDateTime(day, QTime(0, 0), QTimeZone::utc());
    }
    return d.isValid() ? d.toUTC() : QDateTime();
}

} // namespace preflight
