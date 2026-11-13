// Lists ICC profiles; imports, removes and installs them; picks the monitor profile.
#pragma once

#include "engine/ProfileLibrary.h"

#include <QDialog>

class QComboBox;
class QPushButton;
class QTreeWidget;

class ProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileDialog(engine::ProfileLibrary *library, QWidget *parent = nullptr);

private:
    void reload();
    QString selectedPath() const;
    void importProfiles();
    void removeProfile();
    void installProfile();

    engine::ProfileLibrary *m_library;
    QTreeWidget *m_list;
    QComboBox *m_monitor;
    QPushButton *m_remove;
    QPushButton *m_install;
};
