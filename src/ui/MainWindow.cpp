#include "MainWindow.h"

#include "OutputPreviewPanel.h"
#include "PageView.h"
#include "ProfileDialog.h"
#include "RenderWorker.h"
#include "engine/Compositor.h"

#include <QApplication>

#include <tuple>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMenu>
#include <QUrl>
#include <QWidgetAction>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QTreeWidget>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {
const double kMinZoom = 0.05;
const double kMaxZoom = 64.0;
const double kMaxPixels = 60e6; // keeps a rendered page within a sensible amount of memory
const double kMmPerPoint = 25.4 / 72.0;
const int kThumbnailSize = 120;
} // namespace

MainWindow::MainWindow(bool safeMode) : m_safeMode(safeMode)
{
    qRegisterMetaType<engine::PageRender>();
    qRegisterMetaType<engine::DocumentInfo>();
    qRegisterMetaType<QList<QSizeF>>();
    qRegisterMetaType<QList<engine::SearchHit>>();
    qRegisterMetaType<QList<engine::ObjectInfo>>();

    setWindowTitle(QApplication::applicationDisplayName());
    resize(1200, 820);

    m_view = new PageView;
    setCentralWidget(m_view);

    m_panel = new OutputPreviewPanel;
    // Output Preview is its own window that floats above the main window and
    // can be dragged anywhere, including onto another screen.
    m_previewWindow = new QDockWidget(tr("Output Preview"), this);
    m_previewWindow->setObjectName(QStringLiteral("outputPreview"));
    m_previewWindow->setWidget(m_panel);
    m_previewWindow->setAllowedAreas(Qt::NoDockWidgetArea);
    m_previewWindow->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_previewWindow);
    m_previewWindow->setFloating(true);

    // Thumbnails and the object inspector are ordinary panels: docked by
    // default, and they can be dragged out into their own windows.
    m_thumbnails = new QListWidget;
    m_thumbnails->setObjectName(QStringLiteral("thumbnails"));
    m_thumbnails->setViewMode(QListView::IconMode);
    m_thumbnails->setIconSize(QSize(kThumbnailSize, kThumbnailSize));
    m_thumbnails->setFlow(QListView::TopToBottom);
    m_thumbnails->setWrapping(false);
    m_thumbnails->setMovement(QListView::Static);
    m_thumbnails->setSpacing(6);
    m_thumbnails->setMinimumWidth(kThumbnailSize + 40);
    m_thumbnails->setMaximumWidth(kThumbnailSize + 60);
    m_thumbnailDock = new QDockWidget(tr("Pages"), this);
    m_thumbnailDock->setObjectName(QStringLiteral("thumbnailPanel"));
    m_thumbnailDock->setWidget(m_thumbnails);
    addDockWidget(Qt::LeftDockWidgetArea, m_thumbnailDock);
    connect(m_thumbnails, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            goToPage(row);
    });

    m_inspector = new QTreeWidget;
    m_inspector->setObjectName(QStringLiteral("inspector"));
    m_inspector->setHeaderLabels({ tr("Property"), tr("Value") });
    m_inspector->setMinimumWidth(260);
    m_inspectorDock = new QDockWidget(tr("Object Inspector"), this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorPanel"));
    m_inspectorDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    new QTreeWidgetItem(m_inspector, { tr("Click on the page to inspect what is there.") });
    m_inspector->setFirstColumnSpanned(0, QModelIndex(), true);

    m_searchBar = new QToolBar(tr("Search"), this);
    m_searchBar->setObjectName(QStringLiteral("searchBar"));
    m_searchBar->setMovable(false);
    m_searchField = new QLineEdit;
    m_searchField->setObjectName(QStringLiteral("searchField"));
    m_searchField->setPlaceholderText(tr("Find text in the document"));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setMaximumWidth(320);
    m_searchStatus = new QLabel;
    m_searchStatus->setObjectName(QStringLiteral("searchStatus"));
    m_searchBar->addWidget(new QLabel(tr(" Find: ")));
    m_searchBar->addWidget(m_searchField);
    addToolBar(Qt::BottomToolBarArea, m_searchBar);
    m_searchBar->hide();
    connect(m_searchField, &QLineEdit::returnPressed, this, [this] {
        if (m_searchField->text() == m_searchText && !m_hits.isEmpty())
            stepSearch(1);
        else
            startSearch();
    });

    m_banner = new QLabel;
    m_sizeLabel = new QLabel;
    m_zoomLabel = new QLabel;
    statusBar()->addWidget(m_banner, 1);
    statusBar()->addPermanentWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    buildActions();
    connect(&m_customization, &Customization::changed, this, &MainWindow::applyCustomization);
    m_customization.setWatching(m_devModeAction->isChecked());
    applyCustomization();

    m_worker = new RenderWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &MainWindow::requestOpen, m_worker, &RenderWorker::openDocument);
    connect(this, &MainWindow::requestRender, m_worker, &RenderWorker::renderPage);
    connect(m_worker, &RenderWorker::documentOpened, this, &MainWindow::onOpened);
    connect(m_worker, &RenderWorker::pageRendered, this, &MainWindow::onRendered);
    connect(this, &MainWindow::requestThumbnail, m_worker, &RenderWorker::renderThumbnail);
    connect(this, &MainWindow::requestSearch, m_worker, &RenderWorker::search);
    connect(this, &MainWindow::requestInspect, m_worker, &RenderWorker::inspect);
    connect(m_worker, &RenderWorker::thumbnailRendered, this, &MainWindow::onThumbnail);
    connect(m_worker, &RenderWorker::searchFinished, this, &MainWindow::onSearchFinished);
    connect(m_worker, &RenderWorker::objectsFound, this, &MainWindow::onObjectsFound);
    connect(m_worker, &RenderWorker::openFailed, this, [this](const QString &path, const QString &error) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1\n\n%2").arg(QDir::toNativeSeparators(path), error));
    });
    m_thread.start();

    connect(m_view, &PageView::zoomRequested, this, &MainWindow::zoomBy);
    connect(m_view, &PageView::fileDropped, this, &MainWindow::openFile);
    connect(m_view, &PageView::hovered, this, [this](int x, int y) {
        if (m_render.image)
            m_panel->setInkValues(engine::inkValues(*m_render.image, x, y, m_panel->sampleSize()));
    });
    connect(m_view, &PageView::clicked, this, [this](QPointF point) {
        if (m_info.pageCount > 0)
            emit requestInspect(m_page, point, m_rotation);
    });
    connect(m_view, &PageView::hoverLeft, this, [this] { m_panel->setInkValues({}); });
    connect(m_panel, &OutputPreviewPanel::displayChanged, this, &MainWindow::recomposite);
    connect(m_panel, &OutputPreviewPanel::boxesToggled, m_view, &PageView::setShowBoxes);
    m_view->setShowBoxes(m_panel->showBoxes());

    refreshProfiles();
    updateBanner();
}

MainWindow::~MainWindow()
{
    m_thread.quit();
    m_thread.wait();
}

QAction *MainWindow::addItem(const QString &id, const QString &text, const QString &help,
                             const QKeySequence &shortcut)
{
    auto *action = new QAction(text, this);
    action->setShortcut(shortcut);
    addAction(action); // keeps the shortcut working when the item is in no menu
    m_items.insert(id, action);
    m_defaultShortcuts.insert(id, shortcut);
    m_itemHelp.append({ id, help });
    return action;
}

void MainWindow::buildActions()
{
    // Every item gets an id. layout.json decides where the items appear.
    connect(addItem(QStringLiteral("file.open"), tr("&Open…"), tr("Open a PDF file"), QKeySequence::Open),
            &QAction::triggered, this, &MainWindow::chooseFile);
    connect(addItem(QStringLiteral("file.properties"), tr("&Properties…"), tr("Document properties"),
                    QKeySequence(tr("Ctrl+D"))),
            &QAction::triggered, this, &MainWindow::showProperties);
    connect(addItem(QStringLiteral("file.quit"), tr("&Quit"), tr("Close the app"), QKeySequence::Quit),
            &QAction::triggered, qApp, &QApplication::quit);

    connect(addItem(QStringLiteral("view.zoomIn"), tr("Zoom &in"), tr("Zoom in"), QKeySequence::ZoomIn),
            &QAction::triggered, this, [this] { zoomBy(1); });
    connect(addItem(QStringLiteral("view.zoomOut"), tr("Zoom &out"), tr("Zoom out"), QKeySequence::ZoomOut),
            &QAction::triggered, this, [this] { zoomBy(-1); });
    connect(addItem(QStringLiteral("view.actualSize"), tr("&Actual size"), tr("Show the page at 100 %"),
                    QKeySequence(tr("Ctrl+1"))),
            &QAction::triggered, this, [this] {
                m_fit = false;
                m_fitAction->setChecked(false);
                m_zoom = logicalDpiX() / 72.0;
                render();
            });
    m_fitAction = addItem(QStringLiteral("view.fitPage"), tr("&Fit page"), tr("Fit the whole page in the window"),
                          QKeySequence(tr("Ctrl+0")));
    m_fitAction->setCheckable(true);
    m_fitAction->setChecked(true);
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::setFit);
    connect(addItem(QStringLiteral("view.previousPage"), tr("&Previous page"), tr("Go to the previous page"),
                    QKeySequence(Qt::Key_PageUp)),
            &QAction::triggered, this, [this] { goToPage(m_page - 1); });
    connect(addItem(QStringLiteral("view.nextPage"), tr("&Next page"), tr("Go to the next page"),
                    QKeySequence(Qt::Key_PageDown)),
            &QAction::triggered, this, [this] { goToPage(m_page + 1); });

    connect(addItem(QStringLiteral("view.rotateLeft"), tr("Rotate &left"), tr("Turn the view 90° anticlockwise"),
                    QKeySequence(tr("Ctrl+Shift+-"))),
            &QAction::triggered, this, [this] { rotateBy(-90); });
    connect(addItem(QStringLiteral("view.rotateRight"), tr("Rotate &right"), tr("Turn the view 90° clockwise"),
                    QKeySequence(tr("Ctrl+Shift++"))),
            &QAction::triggered, this, [this] { rotateBy(90); });

    connect(addItem(QStringLiteral("edit.find"), tr("&Find…"), tr("Search for text"), QKeySequence::Find),
            &QAction::triggered, this, &MainWindow::showSearchBar);
    connect(addItem(QStringLiteral("edit.findNext"), tr("Find &next"), tr("Go to the next search result"),
                    QKeySequence::FindNext),
            &QAction::triggered, this, [this] { stepSearch(1); });
    connect(addItem(QStringLiteral("edit.findPrevious"), tr("Find &previous"),
                    tr("Go to the previous search result"), QKeySequence::FindPrevious),
            &QAction::triggered, this, [this] { stepSearch(-1); });
    m_searchBar->addAction(m_items.value(QStringLiteral("edit.findPrevious")));
    m_searchBar->addAction(m_items.value(QStringLiteral("edit.findNext")));
    m_searchBar->addWidget(m_searchStatus);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_searchBar->addWidget(spacer);
    m_searchBar->addAction(tr("Close"), this, [this] {
        m_searchBar->hide();
        m_hits.clear();
        m_currentHit = -1;
        m_searchText.clear();
        m_searchStatus->clear();
        m_view->setSearchHits({}, {});
    });

    for (const auto &panel : { std::make_tuple(QStringLiteral("window.thumbnails"), m_thumbnailDock,
                                               tr("Show or hide the page thumbnails")),
                               std::make_tuple(QStringLiteral("window.inspector"), m_inspectorDock,
                                               tr("Show or hide the Object Inspector")) }) {
        QAction *toggle = std::get<1>(panel)->toggleViewAction();
        m_items.insert(std::get<0>(panel), toggle);
        m_itemHelp.append({ std::get<0>(panel), std::get<2>(panel) });
    }

    connect(addItem(QStringLiteral("help.about"), tr("&About Prepress PDF"), tr("Version and licences")),
            &QAction::triggered, this, [this] {
                QMessageBox::about(
                    this, tr("About Prepress PDF"),
                    tr("<h3>Prepress PDF %1</h3>"
                       "<p>A PDF viewer for print production.</p>"
                       "<p>Free software under the GNU Affero General Public License v3. "
                       "Source code: <a href=\"https://github.com/snow34/PDF-viewer-editor\">"
                       "github.com/snow34/PDF-viewer-editor</a></p>"
                       "<p>Built with MuPDF (AGPL), Qt %2 (LGPL) and Little CMS (MIT). "
                       "See THIRD-PARTY-NOTICES.txt for details.</p>")
                        .arg(QStringLiteral(APP_VERSION), QString::fromLatin1(qVersion())));
            });

    connect(addItem(QStringLiteral("tools.profiles"), tr("&Colour profiles…"), tr("Manage ICC profiles")),
            &QAction::triggered, this, &MainWindow::showProfiles);

    QAction *togglePreview = m_previewWindow->toggleViewAction();
    togglePreview->setShortcut(QKeySequence(tr("Ctrl+Shift+Y")));
    addAction(togglePreview);
    m_items.insert(QStringLiteral("window.outputPreview"), togglePreview);
    m_defaultShortcuts.insert(QStringLiteral("window.outputPreview"), togglePreview->shortcut());
    m_itemHelp.append({ QStringLiteral("window.outputPreview"), tr("Show or hide the Output Preview window") });

    // Two items that are not plain commands.
    m_recent = new QMenu(tr("Open &recent"), this);
    m_items.insert(QStringLiteral("file.recent"), m_recent->menuAction());
    m_itemHelp.append({ QStringLiteral("file.recent"), tr("Submenu with recently opened files (menus only)") });

    auto *pageBox = new QWidget;
    auto *pageLayout = new QHBoxLayout(pageBox);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageSpin = new QSpinBox;
    m_pageSpin->setRange(0, 0);
    m_pageSpin->setKeyboardTracking(false);
    m_pageCount = new QLabel;
    pageLayout->addWidget(m_pageSpin);
    pageLayout->addWidget(m_pageCount);
    auto *pageAction = new QWidgetAction(this);
    pageAction->setDefaultWidget(pageBox);
    m_items.insert(QStringLiteral("pageNumber"), pageAction);
    m_itemHelp.append({ QStringLiteral("pageNumber"), tr("Page number box (toolbar only)") });
    connect(m_pageSpin, &QSpinBox::valueChanged, this, [this](int v) { goToPage(v - 1); });

    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // The Customise menu is always added by the app and cannot be removed
    // through layout.json, so there is always a way back.
    m_customiseMenu = new QMenu(tr("&Customise"), this);
    m_devModeAction = m_customiseMenu->addAction(tr("&Developer mode"));
    m_devModeAction->setObjectName(QStringLiteral("devMode"));
    m_devModeAction->setCheckable(true);
    m_devModeAction->setChecked(!m_safeMode && QSettings().value(QStringLiteral("devMode"), false).toBool());
    connect(m_devModeAction, &QAction::toggled, this, &MainWindow::setDevMode);
    m_customiseMenu->addSeparator();
    auto open = [](const QString &path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); };
    m_devActions << m_customiseMenu->addAction(tr("Open customisation &folder"),
                                               this, [open] { open(Customization::dir()); })
                 << m_customiseMenu->addAction(tr("Edit &theme (theme.qss)"),
                                               this, [open] { open(Customization::themePath()); })
                 << m_customiseMenu->addAction(tr("Edit &layout (layout.json)"),
                                               this, [open] { open(Customization::layoutPath()); })
                 << m_customiseMenu->addAction(tr("&Reload now"), this, &MainWindow::applyCustomization)
                 << m_customiseMenu->addAction(tr("&Available items…"), this, &MainWindow::showItemList)
                 << m_customiseMenu->addAction(tr("Reset to &defaults…"), this, &MainWindow::resetCustomization);
    for (QAction *action : m_devActions)
        action->setEnabled(m_devModeAction->isChecked());
    if (m_devModeAction->isChecked())
        Customization::writeMissingFiles(m_itemHelp);

    updateRecentMenu();
}

void MainWindow::setDevMode(bool on)
{
    QSettings().setValue(QStringLiteral("devMode"), on);
    for (QAction *action : m_devActions)
        action->setEnabled(on);
    if (on)
        Customization::writeMissingFiles(m_itemHelp);
    m_customization.setWatching(on);
    applyCustomization();
    if (on)
        statusBar()->showMessage(tr("Developer mode is on. Files are in %1")
                                     .arg(QDir::toNativeSeparators(Customization::dir())), 10000);
}

void MainWindow::applyCustomization()
{
    QStringList problems;
    QString error;
    const Layout layout = m_safeMode ? Customization::defaultLayout() : Customization::loadLayout(&error);
    if (!error.isEmpty())
        problems << error;
    applyLayout(layout, &problems);
    qApp->setStyleSheet(m_safeMode ? QString() : Customization::loadTheme());
    if (!problems.isEmpty())
        statusBar()->showMessage(tr("Customisation: %1").arg(problems.join(QStringLiteral("; "))), 15000);
    else if (m_devModeAction->isChecked())
        statusBar()->showMessage(tr("Theme and layout loaded."), 3000);
}

void MainWindow::applyLayout(const Layout &layout, QStringList *problems)
{
    // Separators belong to one layout and are thrown away with it.
    m_toolBar->clear();
    menuBar()->clear();
    delete m_layoutObjects;
    m_layoutObjects = new QObject(this);
    auto fill = [&](QWidget *target, const QStringList &ids, bool isToolBar) {
        for (const QString &id : ids) {
            if (id == QLatin1String("-")) {
                auto *separator = new QAction(m_layoutObjects);
                separator->setSeparator(true);
                target->addAction(separator);
                continue;
            }
            QAction *action = m_items.value(id);
            const bool wrongPlace = (isToolBar && id == QLatin1String("file.recent"))
                                    || (!isToolBar && id == QLatin1String("pageNumber"));
            if (!action || wrongPlace) {
                problems->append(action ? tr("\"%1\" cannot be used there").arg(id)
                                        : tr("unknown item \"%1\"").arg(id));
                continue;
            }
            target->addAction(action);
        }
    };

    fill(m_toolBar, layout.toolbar, true);
    m_toolBar->setVisible(!layout.toolbar.isEmpty());

    // Menus made for the previous layout are deleted; the items themselves
    // belong to the window and are reused.
    const auto oldMenus = menuBar()->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
    for (QMenu *menu : oldMenus)
        menu->deleteLater();
    for (const MenuLayout &m : layout.menus) {
        auto *menu = new QMenu(m.title, menuBar());
        fill(menu, m.items, false);
        menuBar()->addMenu(menu);
    }
    menuBar()->addMenu(m_customiseMenu);

    for (auto it = m_defaultShortcuts.begin(); it != m_defaultShortcuts.end(); ++it)
        m_items.value(it.key())->setShortcut(it.value());
    for (auto it = layout.shortcuts.begin(); it != layout.shortcuts.end(); ++it) {
        QAction *action = m_defaultShortcuts.contains(it.key()) ? m_items.value(it.key()) : nullptr;
        if (!action) {
            problems->append(tr("shortcut for unknown item \"%1\"").arg(it.key()));
            continue;
        }
        const QKeySequence keys(it.value());
        if (!it.value().isEmpty() && keys.isEmpty())
            problems->append(tr("\"%1\" is not a valid shortcut").arg(it.value()));
        else
            action->setShortcut(keys);
    }
}

void MainWindow::showItemList()
{
    QStringList lines;
    for (const auto &item : m_itemHelp)
        lines << QStringLiteral("%1 — %2").arg(item.first, item.second);
    lines << QString() << tr("Use \"-\" for a separator. The same list is in HOW-TO.txt in the customisation folder.");
    QMessageBox::information(this, tr("Available items"), lines.join(QLatin1Char('\n')));
}

void MainWindow::resetCustomization()
{
    if (QMessageBox::question(this, tr("Reset to defaults"),
                              tr("Delete your theme.qss and layout.json and go back to the standard "
                                 "look and layout? This cannot be undone."))
        != QMessageBox::Yes)
        return;
    if (!Customization::removeFiles())
        QMessageBox::warning(this, tr("Reset to defaults"), tr("The files could not be deleted."));
    if (m_devModeAction->isChecked())
        Customization::writeMissingFiles(m_itemHelp);
    applyCustomization();
}

void MainWindow::chooseFile()
{
    const QString dir = QFileInfo(m_info.path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open PDF"), dir, tr("PDF files (*.pdf)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openFile(const QString &path)
{
    emit requestOpen(QFileInfo(path).absoluteFilePath());
}

void MainWindow::onOpened(const engine::DocumentInfo &info, const QList<QSizeF> &sizes)
{
    m_info = info;
    m_sizes = sizes;
    m_page = 0;
    m_rotation = 0;
    m_render = {};
    m_hits.clear();
    m_currentHit = -1;
    m_searchText.clear();
    m_searchStatus->clear();
    m_view->setSearchHits({}, {});
    m_view->setSelection({});
    setWindowTitle(QStringLiteral("%1 – %2").arg(QFileInfo(info.path).fileName(),
                                                 QApplication::applicationDisplayName()));

    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(info.path);
    recent.prepend(info.path);
    settings.setValue(QStringLiteral("recentFiles"), recent.mid(0, 10));
    updateRecentMenu();

    {
        QSignalBlocker block(m_pageSpin);
        m_pageSpin->setRange(1, qMax(1, info.pageCount));
        m_pageSpin->setValue(1);
    }
    m_pageCount->setText(tr(" of %1 ").arg(info.pageCount));
    refreshProfiles();
    updateBanner();
    restartThumbnails();
    render();
}

QSizeF MainWindow::viewSize(int page) const
{
    const QSizeF size = m_sizes.value(page);
    return m_rotation % 180 == 0 ? size : size.transposed();
}

void MainWindow::rotateBy(int degrees)
{
    if (m_info.pageCount == 0)
        return;
    m_rotation = ((m_rotation + degrees) % 360 + 360) % 360;
    m_view->setSelection({});
    restartThumbnails();
    render();
    if (!m_searchText.isEmpty())
        startSearch(); // result positions depend on the rotation
}

void MainWindow::restartThumbnails()
{
    ++m_thumbGeneration;
    m_nextThumbnail = 0;
    QSignalBlocker block(m_thumbnails);
    if (m_thumbnails->count() != m_info.pageCount) {
        m_thumbnails->clear();
        QPixmap blank(kThumbnailSize, kThumbnailSize);
        blank.fill(Qt::transparent);
        for (int i = 0; i < m_info.pageCount; ++i) {
            auto *item = new QListWidgetItem(QIcon(blank), QString::number(i + 1), m_thumbnails);
            item->setTextAlignment(Qt::AlignHCenter);
        }
    }
    m_thumbnails->setCurrentRow(m_page);
    // One at a time, so thumbnails never hold up the page being viewed.
    if (m_info.pageCount > 0)
        emit requestThumbnail(0, kThumbnailSize, m_rotation, m_thumbGeneration);
}

void MainWindow::onThumbnail(const engine::PageRender &render, int generation)
{
    if (generation != m_thumbGeneration)
        return;
    if (render.image && render.page < m_thumbnails->count()) {
        engine::CompositeOptions o = compositeOptions();
        o.visible.clear();
        o.showCoverage = false;
        const QImage image = engine::composite(*render.image, o);
        QPixmap framed(kThumbnailSize, kThumbnailSize);
        framed.fill(Qt::transparent);
        QPainter p(&framed);
        const QPoint at((kThumbnailSize - image.width()) / 2, (kThumbnailSize - image.height()) / 2);
        p.drawImage(at, image);
        p.setPen(QColor(120, 120, 120));
        p.drawRect(QRect(at, image.size()).adjusted(0, 0, -1, -1));
        p.end();
        QIcon icon(framed);
        icon.addPixmap(framed, QIcon::Selected); // no colour wash over the selected page
        m_thumbnails->item(render.page)->setIcon(icon);
    }
    m_nextThumbnail = render.page + 1;
    if (m_nextThumbnail < m_info.pageCount)
        emit requestThumbnail(m_nextThumbnail, kThumbnailSize, m_rotation, m_thumbGeneration);
}

void MainWindow::showSearchBar()
{
    m_searchBar->show();
    m_searchField->setFocus();
    m_searchField->selectAll();
}

void MainWindow::startSearch()
{
    m_searchText = m_searchField->text();
    m_hits.clear();
    m_currentHit = -1;
    m_view->setSearchHits({}, {});
    if (m_searchText.isEmpty() || m_info.pageCount == 0) {
        m_searchStatus->clear();
        return;
    }
    m_searchStatus->setText(tr(" Searching…"));
    m_worker->setLatestSearch(++m_searchId);
    emit requestSearch(m_searchText, m_rotation, m_searchId);
}

void MainWindow::onSearchFinished(const QString &text, const QList<engine::SearchHit> &hits, int searchId)
{
    if (searchId != m_searchId)
        return;
    m_hits = hits;
    m_currentHit = -1;
    if (hits.isEmpty()) {
        m_searchStatus->setText(tr(" \"%1\" was not found").arg(text));
        m_view->setSearchHits({}, {});
        return;
    }
    // Start with the first result on or after the page being viewed.
    m_currentHit = 0;
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i).page >= m_page) {
            m_currentHit = i;
            break;
        }
    }
    showCurrentHit();
}

void MainWindow::stepSearch(int direction)
{
    if (m_hits.isEmpty()) {
        if (!m_searchBar->isVisible())
            showSearchBar();
        else if (!m_searchField->text().isEmpty() && m_searchField->text() != m_searchText)
            startSearch();
        return;
    }
    m_currentHit = (m_currentHit + direction + m_hits.size()) % m_hits.size();
    showCurrentHit();
}

void MainWindow::showCurrentHit()
{
    if (m_currentHit < 0 || m_currentHit >= m_hits.size())
        return;
    m_searchStatus->setText(tr(" %1 of %2").arg(m_currentHit + 1).arg(m_hits.size()));
    const int page = m_hits.at(m_currentHit).page;
    if (page != m_page) {
        goToPage(page); // the highlights are drawn when the page arrives
        return;
    }
    QList<QRectF> onPage;
    for (const engine::SearchHit &hit : m_hits)
        if (hit.page == m_page)
            onPage += hit.rects;
    m_view->setSearchHits(onPage, m_hits.at(m_currentHit).rects);
}

void MainWindow::onObjectsFound(int page, const QList<engine::ObjectInfo> &objects)
{
    if (page != m_page)
        return;
    m_inspector->clear();
    m_view->setSelection(objects.isEmpty() ? QRectF() : objects.first().bounds);
    if (objects.isEmpty()) {
        new QTreeWidgetItem(m_inspector, { tr("Nothing here"), tr("only paper") });
        return;
    }
    auto row = [](QTreeWidgetItem *parent, const QString &name, const QString &value) {
        new QTreeWidgetItem(parent, { name, value });
    };
    for (int i = 0; i < objects.size(); ++i) {
        const engine::ObjectInfo &o = objects.at(i);
        auto *top = new QTreeWidgetItem(
            m_inspector, { o.kind, i == 0 ? tr("on top") : tr("underneath (%1)").arg(i) });
        row(top, tr("Colour space"), o.colorSpace);
        if (!o.values.isEmpty())
            row(top, tr("Colour"), o.values.join(QStringLiteral(", ")));
        row(top, tr("Overprint"), o.overprint ? tr("on (mode %1)").arg(o.overprintMode) : tr("off"));
        if (o.opacity < 1.0)
            row(top, tr("Opacity"), tr("%1 %").arg(qRound(o.opacity * 100)));
        if (!o.font.isEmpty()) {
            row(top, tr("Font"), o.fontSubstituted ? tr("%1 (substituted, not embedded)").arg(o.font) : o.font);
            row(top, tr("Font size"), tr("%1 pt").arg(o.fontSize, 0, 'f', 1));
        }
        if (o.imageWidth > 0) {
            row(top, tr("Pixels"), tr("%1 × %2, %3 bit").arg(o.imageWidth).arg(o.imageHeight).arg(o.bitsPerComponent));
            row(top, tr("Resolution"), qAbs(o.ppiX - o.ppiY) < 1.0
                                           ? tr("%1 ppi").arg(qRound(o.ppiX))
                                           : tr("%1 × %2 ppi").arg(qRound(o.ppiX)).arg(qRound(o.ppiY)));
        }
        row(top, tr("Size"), tr("%1 × %2 mm")
                                 .arg(o.bounds.width() * kMmPerPoint, 0, 'f', 1)
                                 .arg(o.bounds.height() * kMmPerPoint, 0, 'f', 1));
        top->setExpanded(i == 0);
    }
    m_inspector->resizeColumnToContents(0);
}

void MainWindow::goToPage(int page)
{
    if (m_info.pageCount == 0)
        return;
    page = qBound(0, page, m_info.pageCount - 1);
    if (page == m_page)
        return;
    m_page = page;
    QSignalBlocker block(m_pageSpin);
    m_pageSpin->setValue(page + 1);
    QSignalBlocker blockList(m_thumbnails);
    m_thumbnails->setCurrentRow(page);
    m_view->setSelection({});
    m_view->setSearchHits({}, {});
    render();
}

void MainWindow::zoomBy(int steps)
{
    m_fit = false;
    m_fitAction->setChecked(false);
    m_zoom *= steps > 0 ? 1.25 : 0.8;
    render();
}

void MainWindow::setFit(bool fit)
{
    m_fit = fit;
    m_fitAction->setChecked(fit);
    if (fit)
        render();
}

double MainWindow::fitZoom() const
{
    const QSizeF page = viewSize(m_page);
    if (page.isEmpty())
        return 1.0;
    const QSize room = m_view->maximumViewportSize() - QSize(28, 28);
    return qMin(room.width() / page.width(), room.height() / page.height());
}

void MainWindow::render()
{
    if (m_info.pageCount == 0)
        return;
    if (m_fit)
        m_zoom = fitZoom();
    const QSizeF page = viewSize(m_page);
    double limit = kMaxZoom;
    if (!page.isEmpty())
        limit = qMin(limit, std::sqrt(kMaxPixels / (page.width() * page.height())));
    m_zoom = qBound(kMinZoom, m_zoom, limit);
    m_zoomLabel->setText(tr("%1 %").arg(qRound(m_zoom * 72.0 / logicalDpiX() * 100)));
    m_worker->setLatestRequest(++m_requestId);
    emit requestRender(m_page, m_zoom, m_rotation, m_requestId);
}

void MainWindow::onRendered(const engine::PageRender &render, int requestId)
{
    if (requestId != m_requestId)
        return; // a newer request is on its way
    if (!render.image) {
        m_view->clear();
        statusBar()->showMessage(tr("Page %1 could not be rendered: %2").arg(render.page + 1).arg(render.error));
        return;
    }
    m_render = render;
    m_panel->setInks(render.image->inks);

    const engine::PageBoxes &b = render.boxes;
    const QRectF finished = b.hasTrim ? b.trim : (b.hasCrop ? b.crop : b.media);
    m_sizeLabel->setText(tr("%1: %2 × %3 mm")
                             .arg(b.hasTrim ? tr("Trim size") : tr("Page size"))
                             .arg(finished.width() * kMmPerPoint, 0, 'f', 1)
                             .arg(finished.height() * kMmPerPoint, 0, 'f', 1));
    recomposite();
    if (m_currentHit >= 0 && m_currentHit < m_hits.size() && m_hits.at(m_currentHit).page == m_page)
        showCurrentHit();

    if (!m_screenshot.isEmpty()) {
        const QString file = m_screenshot;
        m_screenshot.clear();
        QTimer::singleShot(200, this, [this, file] {
            grab().save(file);
            // The Output Preview window is saved next to it as <name>-preview.png.
            QString preview = file;
            preview.insert(preview.lastIndexOf(QLatin1Char('.')), QStringLiteral("-preview"));
            m_previewWindow->grab().save(preview);
            qApp->quit();
        });
    }
}

engine::CompositeOptions MainWindow::compositeOptions() const
{
    engine::CompositeOptions o;
    o.visible = m_panel->visibleInks();
    const QString choice = m_panel->simulationChoice();
    if (choice == QLatin1String("intent"))
        o.simulationProfile = m_info.outputIntentProfile;
    else if (!choice.isEmpty())
        o.simulationProfile = engine::ProfileLibrary::load(choice);
    // No choice saved means automatic: the screen's own profile if the
    // system reports one, sRGB otherwise.
    QString monitor = QSettings().value(QStringLiteral("monitorProfile")).toString();
    if (monitor.isEmpty())
        monitor = engine::ProfileLibrary::systemMonitorProfile();
    if (!monitor.isEmpty())
        o.monitorProfile = engine::ProfileLibrary::load(monitor);
    o.intent = m_panel->intent();
    o.showCoverage = m_panel->showCoverage();
    o.coverageLimit = m_panel->coverageLimit();
    return o;
}

void MainWindow::recomposite()
{
    if (!m_render.image)
        return;
    bool calibrated = false;
    const QImage image = engine::composite(*m_render.image, compositeOptions(), &calibrated);
    m_panel->setCalibrated(calibrated);
    m_view->setPage(image, m_render.boxes, m_render.zoom);
}

void MainWindow::refreshProfiles()
{
    QString intentName;
    if (!m_info.outputIntentProfile.isEmpty()) {
        for (const engine::OutputIntentInfo &oi : m_info.outputIntents) {
            if (oi.hasProfile && oi.profileComponents == 4) {
                intentName = oi.conditionIdentifier.isEmpty() ? tr("embedded profile")
                                                              : oi.conditionIdentifier;
                break;
            }
        }
    }
    m_panel->setProfiles(m_library.list(), intentName);
    recomposite();
}

void MainWindow::updateBanner()
{
    if (m_info.pageCount == 0) {
        m_banner->setText(tr("Open a PDF with File > Open, or drop one onto the window."));
        return;
    }
    QStringList parts;
    parts << (m_info.pdfxVersion.isEmpty() ? tr("Not marked as PDF/X")
                                           : tr("Claims %1 (not verified)").arg(m_info.pdfxVersion));
    if (m_info.outputIntents.isEmpty()) {
        parts << tr("No output intent");
    } else {
        const engine::OutputIntentInfo &oi = m_info.outputIntents.first();
        QString name = oi.conditionIdentifier.isEmpty() ? oi.info : oi.conditionIdentifier;
        if (name.isEmpty())
            name = tr("unnamed");
        parts << tr("Output intent: %1%2").arg(name, oi.hasProfile ? QString() : tr(" (no embedded profile)"));
    }
    m_banner->setText(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::updateRecentMenu()
{
    m_recent->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString &path : recent)
        if (QFileInfo::exists(path))
            m_recent->addAction(QDir::toNativeSeparators(path), this, [this, path] { openFile(path); });
    m_recent->setEnabled(!m_recent->isEmpty());
}

void MainWindow::showProperties()
{
    if (m_info.pageCount == 0)
        return;
    QStringList lines;
    lines << tr("File: %1").arg(QDir::toNativeSeparators(m_info.path));
    if (!m_info.title.isEmpty())
        lines << tr("Title: %1").arg(m_info.title);
    lines << tr("Format: %1").arg(m_info.pdfVersion) << tr("Pages: %1").arg(m_info.pageCount)
          << tr("PDF/X: %1").arg(m_info.pdfxVersion.isEmpty() ? tr("not marked") : m_info.pdfxVersion);
    if (m_info.outputIntents.isEmpty())
        lines << tr("Output intents: none");
    for (const engine::OutputIntentInfo &oi : m_info.outputIntents) {
        lines << QString() << tr("Output intent (%1)").arg(oi.subtype)
              << tr("  Condition: %1").arg(oi.conditionIdentifier) << tr("  Info: %1").arg(oi.info)
              << tr("  Registry: %1").arg(oi.registry)
              << tr("  Embedded profile: %1")
                     .arg(oi.hasProfile ? tr("yes, %1 channels").arg(oi.profileComponents) : tr("no"));
    }
    QMessageBox::information(this, tr("Document properties"), lines.join(QLatin1Char('\n')));
}

void MainWindow::showProfiles()
{
    ProfileDialog dialog(&m_library, this);
    dialog.exec();
    refreshProfiles();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_previewPlaced)
        return;
    m_previewPlaced = true;
    QSettings settings;
    const QRect saved = settings.value(QStringLiteral("outputPreviewGeometry")).toRect();
    bool onScreen = false;
    for (QScreen *screen : QGuiApplication::screens())
        onScreen = onScreen || screen->availableGeometry().intersects(saved);
    if (saved.isValid() && onScreen) {
        m_previewWindow->setGeometry(saved);
    } else {
        // First start: over the right-hand side of the main window.
        m_previewWindow->resize(340, 520);
        m_previewWindow->move(geometry().topRight() + QPoint(-380, 90));
    }
    m_previewWindow->setVisible(settings.value(QStringLiteral("outputPreviewVisible"), true).toBool());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("outputPreviewGeometry"), m_previewWindow->geometry());
    settings.setValue(QStringLiteral("outputPreviewVisible"), m_previewWindow->isVisible());
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_fit)
        render();
}
