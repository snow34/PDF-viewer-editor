// A small reader for XMP metadata as found in PDF files. Properties are
// looked up by namespace URI, so it does not matter which prefix a file uses;
// the prefixes themselves are kept because PDF/X-4 prescribes them.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

namespace preflight {

namespace xmpns {
inline const QString dc = QStringLiteral("http://purl.org/dc/elements/1.1/");
inline const QString xmp = QStringLiteral("http://ns.adobe.com/xap/1.0/");
inline const QString xmpMM = QStringLiteral("http://ns.adobe.com/xap/1.0/mm/");
inline const QString pdf = QStringLiteral("http://ns.adobe.com/pdf/1.3/");
inline const QString pdfxid = QStringLiteral("http://www.npes.org/pdfx/ns/id/");
inline const QString rdf = QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
} // namespace xmpns

struct Xmp {
    bool present = false;   // the file has an XMP stream with some content
    bool readable = true;   // it is well-formed XML
    QString problem;        // why it is not readable
    // Simple properties and the first (or "x-default") entry of lists and
    // alternatives, keyed by namespace URI + "|" + local name.
    QHash<QString, QString> values;
    // Every prefix declared in the packet: (prefix, namespace URI).
    QList<QPair<QString, QString>> prefixes;

    QString value(const QString &ns, const QString &name) const { return values.value(ns + QLatin1Char('|') + name); }
};

Xmp parseXmp(const QString &packet);

// Dates as written in the PDF document information ("D:20240101120000+01'00'")
// and in XMP ("2024-01-01T12:00:00+01:00"). Invalid when not understood.
QDateTime parsePdfDate(const QString &text);
QDateTime parseXmpDate(const QString &text);

} // namespace preflight
