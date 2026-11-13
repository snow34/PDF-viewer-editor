#include "ProfileDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

ProfileDialog::ProfileDialog(engine::ProfileLibrary *library, QWidget *parent)
    : QDialog(parent), m_library(library)
{
    setWindowTitle(tr("Colour profiles"));
    resize(760, 460);
    auto *layout = new QVBoxLayout(this);

    m_list = new QTreeWidget;
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({ tr("Profile"), tr("Space"), tr("Class"), tr("Location") });
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_list);

    auto *buttons = new QHBoxLayout;
    auto *import = new QPushButton(tr("Import…"));
    m_remove = new QPushButton(tr("Remove from app"));
    m_install = new QPushButton(tr("Install for other applications"));
    buttons->addWidget(import);
    buttons->addWidget(m_remove);
    buttons->addWidget(m_install);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *form = new QFormLayout;
    m_monitor = new QComboBox;
    form->addRow(tr("Monitor profile:"), m_monitor);
    layout->addLayout(form);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(import, &QPushButton::clicked, this, &ProfileDialog::importProfiles);
    connect(m_remove, &QPushButton::clicked, this, &ProfileDialog::removeProfile);
    connect(m_install, &QPushButton::clicked, this, &ProfileDialog::installProfile);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        auto *item = m_list->currentItem();
        m_remove->setEnabled(item && item->data(0, Qt::UserRole + 1).toBool());
        m_install->setEnabled(item != nullptr);
    });
    connect(m_monitor, &QComboBox::activated, this, [this] {
        QSettings().setValue(QStringLiteral("monitorProfile"), m_monitor->currentData());
    });
    reload();
}

void ProfileDialog::reload()
{
    const QString monitor = QSettings().value(QStringLiteral("monitorProfile")).toString();
    m_list->clear();
    m_monitor->clear();
    engine::ProfileInfo detected;
    const QString systemProfile = engine::ProfileLibrary::systemMonitorProfile();
    if (!systemProfile.isEmpty() && engine::ProfileLibrary::inspect(systemProfile, &detected, nullptr))
        m_monitor->addItem(tr("Automatic (%1)").arg(detected.description), QString());
    else
        m_monitor->addItem(tr("Automatic (sRGB, no screen profile found)"), QString());
    for (const engine::ProfileInfo &p : m_library->list()) {
        auto *item = new QTreeWidgetItem(
            m_list, { p.description, p.colorSpace, p.deviceClass,
                      p.inAppLibrary ? tr("App library") : QDir::toNativeSeparators(p.path) });
        item->setData(0, Qt::UserRole, p.path);
        item->setData(0, Qt::UserRole + 1, p.inAppLibrary);
        if (p.isRgb())
            m_monitor->addItem(p.description, p.path);
    }
    const int index = m_monitor->findData(monitor);
    m_monitor->setCurrentIndex(index < 0 ? 0 : index);
    m_remove->setEnabled(false);
    m_install->setEnabled(false);
}

QString ProfileDialog::selectedPath() const
{
    auto *item = m_list->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void ProfileDialog::importProfiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import ICC profiles"), QString(), tr("ICC profiles (*.icc *.icm)"));
    QStringList problems;
    for (const QString &file : files) {
        QString error;
        if (m_library->import(file, &error).isEmpty())
            problems << error;
    }
    if (!problems.isEmpty())
        QMessageBox::warning(this, windowTitle(), problems.join(QLatin1Char('\n')));
    reload();
}

void ProfileDialog::removeProfile()
{
    const QString path = selectedPath();
    if (path.isEmpty()
        || QMessageBox::question(this, windowTitle(),
                                 tr("Remove %1 from the app library?")
                                     .arg(m_list->currentItem()->text(0)))
               != QMessageBox::Yes)
        return;
    QString error;
    if (!m_library->remove(path, &error))
        QMessageBox::warning(this, windowTitle(), error);
    reload();
}

void ProfileDialog::installProfile()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    QString error;
    if (engine::ProfileLibrary::installSystemWide(path, &error))
        QMessageBox::information(
            this, windowTitle(),
            tr("The profile is now installed in %1")
                .arg(QDir::toNativeSeparators(engine::ProfileLibrary::userSystemDir())));
    else
        QMessageBox::warning(this, windowTitle(), error);
    reload();
}
