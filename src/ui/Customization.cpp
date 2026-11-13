#include "Customization.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

namespace {

const char kDefaultLayout[] = R"({
  "toolbar": [
    "file.open", "-",
    "view.previousPage", "pageNumber", "view.nextPage", "-",
    "view.zoomOut", "view.zoomIn", "view.fitPage", "-",
    "tools.profiles"
  ],
  "menus": [
    { "title": "&File", "items": ["file.open", "file.recent", "file.properties", "-", "file.quit"] },
    { "title": "&View", "items": ["view.zoomIn", "view.zoomOut", "view.actualSize", "view.fitPage", "-",
                                  "view.previousPage", "view.nextPage"] },
    { "title": "&Tools", "items": ["tools.profiles"] },
    { "title": "&Window", "items": ["window.outputPreview"] }
  ],
  "shortcuts": {
  }
}
)";

const char kDefaultTheme[] = R"(/* Prepress PDF theme.

   This is a Qt style sheet. Save the file and the running app updates at
   once (Developer mode must be on). Reference:
   https://doc.qt.io/qt-6/stylesheet-reference.html

   Useful selectors:
     QMainWindow, QMenuBar, QMenu, QToolBar, QToolButton, QStatusBar
     QDockWidget#outputPreview      the Output Preview window
     QTreeWidget#inks               the separations list
     PageView                       the area behind the page

   Everything below is an example and is switched off. Remove the comment
   marks around a block to try it.
*/

/* Example: dark theme
QWidget            { background: #2b2b2b; color: #e6e6e6; font-size: 10pt; }
QToolBar, QMenuBar { background: #333333; border: none; }
QToolButton:hover, QMenuBar::item:selected, QMenu::item:selected { background: #4a6fa5; }
QTreeWidget, QComboBox, QSpinBox, QDoubleSpinBox { background: #1f1f1f; border: 1px solid #555555; }
QHeaderView::section { background: #3a3a3a; border: none; padding: 3px; }
*/

/* Example: darker area behind the page
PageView { background: #505050; }
*/
)";

QString read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeIfMissing(const QString &path, const QByteArray &content)
{
    if (QFile::exists(path))
        return true;
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &v : value.toArray())
        list << v.toString();
    return list;
}

Layout parse(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        if (error) {
            const int line = int(json.left(parseError.offset).count('\n')) + 1;
            *error = QStringLiteral("layout.json, line %1: %2").arg(line).arg(parseError.errorString());
        }
        return {};
    }
    const QJsonObject root = doc.object();
    Layout layout;
    layout.toolbar = toStringList(root.value(QStringLiteral("toolbar")));
    for (const QJsonValue &m : root.value(QStringLiteral("menus")).toArray()) {
        const QJsonObject menu = m.toObject();
        layout.menus.append(
            { menu.value(QStringLiteral("title")).toString(), toStringList(menu.value(QStringLiteral("items"))) });
    }
    const QJsonObject shortcuts = root.value(QStringLiteral("shortcuts")).toObject();
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
        layout.shortcuts.insert(it.key(), it.value().toString());
    return layout;
}

} // namespace

Customization::Customization(QObject *parent) : QObject(parent)
{
    // Editors often save by replacing the file, which sends several
    // notifications and drops the watch; wait briefly, then watch again.
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(150);
    connect(timer, &QTimer::timeout, this, [this] {
        rewatch();
        emit changed();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, timer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, timer, qOverload<>(&QTimer::start));
}

QString Customization::dir()
{
    const QString override = qEnvironmentVariable("PREPRESSPDF_CONFIG_DIR");
    return override.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                     + QStringLiteral("/customise")
               : override;
}

QString Customization::themePath() { return dir() + QStringLiteral("/theme.qss"); }
QString Customization::layoutPath() { return dir() + QStringLiteral("/layout.json"); }
QString Customization::guidePath() { return dir() + QStringLiteral("/HOW-TO.txt"); }

Layout Customization::defaultLayout()
{
    return parse(kDefaultLayout, nullptr);
}

Layout Customization::loadLayout(QString *error)
{
    QFile f(layoutPath());
    if (!f.open(QIODevice::ReadOnly))
        return defaultLayout();
    QString problem;
    const Layout layout = parse(f.readAll(), &problem);
    if (problem.isEmpty() && layout.menus.isEmpty() && layout.toolbar.isEmpty())
        problem = QStringLiteral("layout.json has no toolbar and no menus");
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return defaultLayout();
    }
    return layout;
}

QString Customization::loadTheme()
{
    return read(themePath());
}

void Customization::writeMissingFiles(const QList<QPair<QString, QString>> &items)
{
    QDir().mkpath(dir());
    writeIfMissing(themePath(), kDefaultTheme);
    writeIfMissing(layoutPath(), kDefaultLayout);

    // The guide is rewritten every time so it always matches this version.
    QString guide = QStringLiteral(
        "Customising Prepress PDF\n"
        "========================\n\n"
        "Switch on Customise > Developer mode. While it is on, the app updates\n"
        "as soon as you save one of these files.\n\n"
        "theme.qss    Colours, fonts and spacing (a Qt style sheet). Examples are inside.\n"
        "layout.json  What is in the toolbar and the menus, and the keyboard shortcuts.\n\n"
        "layout.json\n"
        "-----------\n"
        "  \"toolbar\"    list of item ids, left to right. \"-\" is a separator line.\n"
        "  \"menus\"      list of menus, each with a \"title\" and a list of \"items\".\n"
        "               Put & before the letter that Alt should select.\n"
        "  \"shortcuts\"  item id and key, for example  \"file.open\": \"Ctrl+Shift+O\"\n"
        "               An empty text \"\" removes the shortcut.\n\n"
        "The Customise menu is always added by the app, so you cannot lock yourself out.\n"
        "If the app will not start properly any more, start it with --safe-mode or use\n"
        "Customise > Reset to defaults.\n\n"
        "Item ids\n"
        "--------\n");
    for (const auto &item : items)
        guide += QStringLiteral("  %1  %2\n").arg(item.first, -22).arg(item.second);
    QFile f(guidePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(guide.toUtf8());
}

bool Customization::removeFiles()
{
    bool ok = true;
    for (const QString &path : { themePath(), layoutPath() })
        ok = (!QFile::exists(path) || QFile::remove(path)) && ok;
    return ok;
}

void Customization::setWatching(bool on)
{
    m_watching = on;
    rewatch();
}

void Customization::rewatch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!m_watching)
        return;
    if (QDir(dir()).exists())
        m_watcher.addPath(dir());
    for (const QString &path : { themePath(), layoutPath() })
        if (QFile::exists(path))
            m_watcher.addPath(path);
}
