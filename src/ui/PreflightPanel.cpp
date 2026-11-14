#include "PreflightPanel.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>

using preflight::Finding;
using preflight::Severity;

PreflightPanel::PreflightPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_standard = new QComboBox;
    m_standard->setObjectName(QStringLiteral("preflightStandard"));
    for (preflight::Standard s : preflight::allStandards())
        m_standard->addItem(preflight::standardName(s), int(s));
    m_check = new QPushButton(tr("Check"));
    m_check->setObjectName(QStringLiteral("preflightCheck"));
    top->addWidget(m_standard, 1);
    top->addWidget(m_check);
    layout->addLayout(top);

    m_progress = new QProgressBar;
    m_progress->hide();
    layout->addWidget(m_progress);

    m_result = new QLabel;
    m_result->setObjectName(QStringLiteral("preflightResult"));
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("preflightFindings"));
    m_tree->setHeaderLabels({ tr("Problem"), tr("Page") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);

    // The list shows one line per problem; the full text of the selected one goes here.
    m_detail = new QLabel;
    m_detail->setObjectName(QStringLiteral("preflightDetail"));
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setFrameShape(QFrame::StyledPanel);
    m_detail->setMargin(6);
    m_detail->hide();
    layout->addWidget(m_detail);

    m_save = new QPushButton(tr("Save report…"));
    m_save->setEnabled(false);
    layout->addWidget(m_save);

    connect(m_check, &QPushButton::clicked, this, [this] {
        if (m_running)
            emit stopRequested();
        else
            emit checkRequested();
    });
    connect(m_save, &QPushButton::clicked, this, &PreflightPanel::chooseFileAndSave);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        const int index = item && item->parent() ? item->data(0, Qt::UserRole).toInt() : -1;
        if (index < 0 || index >= m_report.findings.size()) {
            m_detail->hide();
            return;
        }
        const Finding &f = m_report.findings.at(index);
        m_detail->setText(tr("%1<br><small>%2 · rule %3</small>")
                              .arg(f.message.toHtmlEscaped(),
                                   f.page < 0 ? tr("Whole document") : tr("Page %1").arg(f.page + 1),
                                   f.ruleId));
        m_detail->show();
        emit findingActivated(f);
    });
    setDocument({}, {});
}

void PreflightPanel::setDocument(const QString &fileName, const QString &claimedVersion)
{
    m_fileName = fileName;
    m_hasReport = false;
    m_report = {};
    m_tree->clear();
    m_detail->hide();
    m_save->setEnabled(false);
    m_check->setEnabled(!fileName.isEmpty());
    preflight::Standard claimed = preflight::Standard::X4;
    const bool known = preflight::standardFromClaim(claimedVersion, &claimed);
    m_standard->setCurrentIndex(m_standard->findData(int(claimed)));
    if (fileName.isEmpty())
        m_result->setText(tr("Open a PDF to check it."));
    else if (known)
        m_result->setText(tr("The file says it is %1. Press Check to verify that.")
                              .arg(preflight::standardName(claimed)));
    else
        m_result->setText(tr("The file is not marked as PDF/X. Choose a standard and press Check."));
}

preflight::Standard PreflightPanel::standard() const
{
    return preflight::Standard(m_standard->currentData().toInt());
}

void PreflightPanel::setRunning(bool running)
{
    m_running = running;
    m_check->setText(running ? tr("Stop") : tr("Check"));
    m_standard->setEnabled(!running);
    m_progress->setVisible(running);
    if (running) {
        m_progress->setRange(0, 0);
        m_result->setText(tr("Checking…"));
    }
}

void PreflightPanel::setProgress(int page, int pageCount)
{
    m_progress->setRange(0, pageCount);
    m_progress->setValue(page);
}

void PreflightPanel::setReport(const preflight::Report &report)
{
    setRunning(false);
    m_report = report;
    m_hasReport = true;
    m_save->setEnabled(true);
    m_tree->clear();
    m_detail->hide();

    const QString name = preflight::standardName(report.standard);
    auto count = [](int n, const QString &one, const QString &many) {
        return n == 1 ? one : many.arg(n);
    };
    const QString errors = count(report.errors(), tr("1 error"), tr("%1 errors"));
    const QString warnings = count(report.warnings(), tr("1 warning"), tr("%1 warnings"));
    if (report.cancelled)
        m_result->setText(count(report.pagesChecked, tr("Stopped after 1 page. The result is incomplete."),
                                tr("Stopped after %1 pages. The result is incomplete.")));
    else if (report.errors() == 0 && report.warnings() == 0)
        m_result->setText(tr("<b>Passed.</b> No problems found for %1.").arg(name));
    else if (report.errors() == 0)
        m_result->setText(tr("<b>Passed</b> for %1, with %2.").arg(name, warnings));
    else if (report.warnings() == 0)
        m_result->setText(tr("<b>Failed</b> for %1: %2.").arg(name, errors));
    else
        m_result->setText(tr("<b>Failed</b> for %1: %2, %3.").arg(name, errors, warnings));

    const QIcon errorIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    QHash<QString, QTreeWidgetItem *> groups;
    for (int i = 0; i < report.findings.size(); ++i) {
        const Finding &f = report.findings.at(i);
        QTreeWidgetItem *group = groups.value(f.category);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, { f.category });
            group->setFirstColumnSpanned(true);
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            groups.insert(f.category, group);
        }
        auto *item = new QTreeWidgetItem(group, { f.message, f.page < 0 ? QString() : QString::number(f.page + 1) });
        item->setIcon(0, f.severity == Severity::Error ? errorIcon : warningIcon);
        item->setToolTip(0, tr("%1\nRule: %2").arg(f.message, f.ruleId));
        item->setData(0, Qt::UserRole, i);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignTop);
    }
    m_tree->expandAll();
}

bool PreflightPanel::saveReport(const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString name = QFileInfo(m_fileName).fileName();
    if (suffix == QLatin1String("pdf")) {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setTitle(tr("Preflight report for %1").arg(name));
        QTextDocument doc;
        doc.setHtml(preflight::toHtml(m_report, name));
        doc.print(&writer);
        if (QFileInfo(path).size() > 0)
            return true;
    } else {
        const QByteArray data = suffix == QLatin1String("json") ? preflight::toJson(m_report, name)
                                                               : preflight::toHtml(m_report, name).toUtf8();
        QFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size())
            return true;
    }
    if (error)
        *error = tr("Could not write %1").arg(QDir::toNativeSeparators(path));
    return false;
}

void PreflightPanel::chooseFileAndSave()
{
    const QString base = QFileInfo(m_fileName).completeBaseName() + QStringLiteral("-preflight.pdf");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save preflight report"), QFileInfo(m_fileName).absolutePath() + QLatin1Char('/') + base,
        tr("PDF report (*.pdf);;Web page (*.html);;Data for other programs (*.json)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!saveReport(path, &error))
        QMessageBox::warning(this, tr("Save preflight report"), error);
}
