// Results of checking a document against a PDF/X standard.
#pragma once

#include <QList>
#include <QMetaType>
#include <QRectF>
#include <QString>

namespace preflight {

enum class Standard { X1a_2001, X1a_2003, X3_2002, X3_2003, X4 };

QList<Standard> allStandards();
QString standardName(Standard s); // e.g. "PDF/X-1a:2001"
// The standard a claimed version text refers to, e.g. "PDF/X-3:2003".
bool standardFromClaim(const QString &claim, Standard *out);

enum class Severity { Error, Warning };

struct Finding {
    QString ruleId;   // stable identifier, e.g. "intent.missing"
    QString category; // e.g. "Output intent"
    Severity severity = Severity::Error;
    QString message;
    int page = -1;    // -1: concerns the whole document
    QRectF bounds;    // view points at rotation 0; invalid when there is no location
};

struct Report {
    Standard standard = Standard::X4;
    QList<Finding> findings;
    int pagesChecked = 0;
    bool cancelled = false;

    int errors() const;
    int warnings() const;
    bool passed() const { return !cancelled && errors() == 0; }
};

struct Options {
    // Images below this resolution get a warning. 0 switches the check off.
    double minImagePpi = 150.0;
};

// Report as a self-contained HTML page and as JSON. `fileName` is shown in the heading.
QString toHtml(const Report &report, const QString &fileName);
QByteArray toJson(const Report &report, const QString &fileName);

} // namespace preflight

Q_DECLARE_METATYPE(preflight::Report)
Q_DECLARE_METATYPE(preflight::Standard)
