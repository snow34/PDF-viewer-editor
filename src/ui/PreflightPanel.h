// Preflight: choose a PDF/X standard, run the check, browse the findings.
#pragma once

#include "preflight/Types.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class PreflightPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreflightPanel(QWidget *parent = nullptr);

    // Called when a document is opened: clears the results and preselects
    // the standard the file claims (PDF/X-4 when it claims none).
    void setDocument(const QString &fileName, const QString &claimedVersion);
    preflight::Standard standard() const;
    void setRunning(bool running);
    void setProgress(int page, int pageCount);
    void setReport(const preflight::Report &report);
    const preflight::Report &report() const { return m_report; }
    bool hasReport() const { return m_hasReport; }

    // Writes the report as .pdf, .html or .json, chosen by the file extension.
    bool saveReport(const QString &path, QString *error = nullptr);

signals:
    void checkRequested();
    void stopRequested();
    void findingActivated(const preflight::Finding &finding);

private:
    void chooseFileAndSave();

    QComboBox *m_standard;
    QPushButton *m_check;
    QPushButton *m_save;
    QProgressBar *m_progress;
    QLabel *m_result;
    QLabel *m_detail;
    QTreeWidget *m_tree;
    preflight::Report m_report;
    bool m_hasReport = false;
    bool m_running = false;
    QString m_fileName;
};
