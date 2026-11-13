// Output Preview: simulation profile, separations, ink readout, coverage.
#pragma once

#include "engine/Compositor.h"
#include "engine/ProfileLibrary.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTreeWidget;

class OutputPreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutputPreviewPanel(QWidget *parent = nullptr);

    // Refills the profile list. `intentName` is empty when the document has
    // no CMYK output intent profile.
    void setProfiles(const QList<engine::ProfileInfo> &profiles, const QString &intentName);
    void setInks(const QStringList &inks);
    void setInkValues(const std::vector<double> &values); // empty = cursor outside page
    void setCalibrated(bool calibrated);

    // "" = uncalibrated, "intent" = document output intent, otherwise a file path.
    QString simulationChoice() const;
    engine::DisplayIntent intent() const;
    std::vector<bool> visibleInks() const;
    int sampleSize() const;
    bool showCoverage() const;
    double coverageLimit() const;
    bool showBoxes() const;

signals:
    void displayChanged(); // needs a recomposite
    void boxesToggled(bool on);

private:
    QComboBox *m_profile;
    QComboBox *m_intent;
    QLabel *m_note;
    QTreeWidget *m_inks;
    QLabel *m_total;
    QComboBox *m_sample;
    QCheckBox *m_coverage;
    QDoubleSpinBox *m_limit;
    QCheckBox *m_boxes;
    bool m_filling = false;
};
