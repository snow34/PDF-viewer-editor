// User-editable look and layout: a Qt style sheet (theme.qss) and a JSON
// description of the toolbar, menus and shortcuts (layout.json), both in the
// customisation folder and reloaded when they change on disk.
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

struct MenuLayout {
    QString title;
    QStringList items;
};

struct Layout {
    QStringList toolbar;
    QList<MenuLayout> menus;
    QHash<QString, QString> shortcuts; // item id -> key sequence
};

class Customization : public QObject {
    Q_OBJECT
public:
    explicit Customization(QObject *parent = nullptr);

    // Folder holding the files. Set PREPRESSPDF_CONFIG_DIR to override it.
    static QString dir();
    static QString themePath();
    static QString layoutPath();
    static QString guidePath();

    static Layout defaultLayout();
    // Reads layout.json. Without a file the default layout is returned. A
    // file that cannot be read gives the default layout and sets `error`.
    static Layout loadLayout(QString *error);
    static QString loadTheme();

    // Writes the default files that do not exist yet, and the guide listing
    // every item id with its description.
    static void writeMissingFiles(const QList<QPair<QString, QString>> &items);
    static bool removeFiles();

    // Emits changed() shortly after a file in the folder is saved.
    void setWatching(bool on);

signals:
    void changed();

private:
    void rewatch();
    QFileSystemWatcher m_watcher;
    bool m_watching = false;
};
