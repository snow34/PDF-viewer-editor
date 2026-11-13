#include "OutputPreviewPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

OutputPreviewPanel::OutputPreviewPanel(QWidget *parent) : QWidget(parent)
{
    QSettings settings;
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_profile = new QComboBox;
    m_profile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_profile->setMinimumContentsLength(18);
    form->addRow(tr("Simulation profile:"), m_profile);

    m_intent = new QComboBox;
    m_intent->addItem(tr("Relative colorimetric"), int(engine::DisplayIntent::RelativeColorimetric));
    m_intent->addItem(tr("Absolute (simulate paper white)"),
                      int(engine::DisplayIntent::AbsoluteColorimetric));
    form->addRow(tr("Display:"), m_intent);
    layout->addLayout(form);

    m_note = new QLabel;
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    m_inks = new QTreeWidget;
    m_inks->setObjectName(QStringLiteral("inks"));
    m_inks->setRootIsDecorated(false);
    m_inks->setHeaderLabels({ tr("Separation"), tr("Ink") });
    m_inks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_inks->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_inks->header()->setStretchLastSection(false);
    layout->addWidget(m_inks, 1);

    m_total = new QLabel;
    m_total->setObjectName(QStringLiteral("total"));
    layout->addWidget(m_total);

    auto *form2 = new QFormLayout;
    m_sample = new QComboBox;
    m_sample->addItem(tr("Point sample"), 1);
    m_sample->addItem(tr("3 × 3 average"), 3);
    m_sample->addItem(tr("5 × 5 average"), 5);
    form2->addRow(tr("Sample size:"), m_sample);

    m_coverage = new QCheckBox(tr("Highlight total area coverage above"));
    m_coverage->setObjectName(QStringLiteral("coverage"));
    m_limit = new QDoubleSpinBox;
    m_limit->setRange(1, 1000);
    m_limit->setDecimals(0);
    m_limit->setSuffix(QStringLiteral(" %"));
    m_limit->setValue(settings.value(QStringLiteral("coverageLimit"), 300).toDouble());
    form2->addRow(m_coverage, m_limit);
    layout->addLayout(form2);

    m_boxes = new QCheckBox(tr("Show page boxes (trim green, bleed blue, art red)"));
    m_boxes->setChecked(settings.value(QStringLiteral("showBoxes"), true).toBool());
    layout->addWidget(m_boxes);

    setInkValues({});

    connect(m_profile, &QComboBox::currentIndexChanged, this, [this] {
        if (m_filling)
            return;
        QSettings().setValue(QStringLiteral("simulationProfile"), simulationChoice());
        emit displayChanged();
    });
    connect(m_intent, &QComboBox::currentIndexChanged, this, &OutputPreviewPanel::displayChanged);
    connect(m_inks, &QTreeWidget::itemChanged, this, [this] {
        if (!m_filling)
            emit displayChanged();
    });
    connect(m_coverage, &QCheckBox::toggled, this, &OutputPreviewPanel::displayChanged);
    connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QSettings().setValue(QStringLiteral("coverageLimit"), v);
        if (m_coverage->isChecked())
            emit displayChanged();
    });
    connect(m_boxes, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("showBoxes"), on);
        emit boxesToggled(on);
    });
}

void OutputPreviewPanel::setProfiles(const QList<engine::ProfileInfo> &profiles,
                                     const QString &intentName)
{
    QSettings settings;
    const bool remembered = settings.contains(QStringLiteral("simulationProfile"));
    const QString wanted = settings.value(QStringLiteral("simulationProfile")).toString();
    m_filling = true;
    m_profile->clear();
    if (!intentName.isEmpty())
        m_profile->addItem(tr("Output intent: %1").arg(intentName), QStringLiteral("intent"));
    for (const engine::ProfileInfo &p : profiles)
        if (p.isCmyk())
            m_profile->addItem(p.description, p.path);
    m_profile->addItem(tr("None (uncalibrated)"), QString());
    // A document's own output intent wins; otherwise the last profile used.
    // With neither, the first CMYK profile found.
    const int index = intentName.isEmpty() && remembered ? m_profile->findData(wanted) : 0;
    m_profile->setCurrentIndex(index < 0 ? 0 : index);
    m_filling = false;
}

void OutputPreviewPanel::setInks(const QStringList &inks)
{
    QStringList current;
    for (int i = 0; i < m_inks->topLevelItemCount(); ++i)
        current << m_inks->topLevelItem(i)->text(0);
    if (current == inks)
        return; // keep the user's plate selection when the ink set is unchanged
    m_filling = true;
    m_inks->clear();
    for (const QString &ink : inks) {
        auto *item = new QTreeWidgetItem(m_inks, { ink, QString() });
        item->setCheckState(0, Qt::Checked);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_filling = false;
    setInkValues({});
}

void OutputPreviewPanel::setInkValues(const std::vector<double> &values)
{
    double total = 0;
    for (int i = 0; i < m_inks->topLevelItemCount(); ++i) {
        const bool have = size_t(i) < values.size();
        m_inks->topLevelItem(i)->setText(
            1, have ? QStringLiteral("%1 %").arg(qRound(values[size_t(i)])) : QStringLiteral("–"));
        if (have)
            total += values[size_t(i)];
    }
    m_total->setText(values.empty() ? tr("Total area coverage: –")
                                    : tr("Total area coverage: %1 %").arg(qRound(total)));
}

void OutputPreviewPanel::setCalibrated(bool calibrated)
{
    m_note->setText(calibrated ? QString()
                               : tr("Colours on screen are approximate: no CMYK profile is "
                                    "selected. Ink values are not affected."));
    m_note->setVisible(!calibrated);
}

QString OutputPreviewPanel::simulationChoice() const
{
    return m_profile->currentData().toString();
}

engine::DisplayIntent OutputPreviewPanel::intent() const
{
    return engine::DisplayIntent(m_intent->currentData().toInt());
}

std::vector<bool> OutputPreviewPanel::visibleInks() const
{
    std::vector<bool> v;
    for (int i = 0; i < m_inks->topLevelItemCount(); ++i)
        v.push_back(m_inks->topLevelItem(i)->checkState(0) == Qt::Checked);
    return v;
}

int OutputPreviewPanel::sampleSize() const { return m_sample->currentData().toInt(); }
bool OutputPreviewPanel::showCoverage() const { return m_coverage->isChecked(); }
double OutputPreviewPanel::coverageLimit() const { return m_limit->value(); }
bool OutputPreviewPanel::showBoxes() const { return m_boxes->isChecked(); }
