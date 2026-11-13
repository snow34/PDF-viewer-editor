#include "MainWindow.h"

#include "OutputPreviewPanel.h"
#include "PageView.h"
#include "ProfileDialog.h"
#include "RenderWorker.h"
#include "engine/Compositor.h"

#include <QApplication>
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
} // namespace

MainWindow::MainWindow()
{
    qRegisterMetaType<engine::PageRender>();
    qRegisterMetaType<engine::DocumentInfo>();
    qRegisterMetaType<QList<QSizeF>>();

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

    m_banner = new QLabel;
    m_sizeLabel = new QLabel;
    m_zoomLabel = new QLabel;
    statusBar()->addWidget(m_banner, 1);
    statusBar()->addPermanentWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    buildActions();
    QAction *togglePreview = m_previewWindow->toggleViewAction();
    togglePreview->setShortcut(QKeySequence(tr("Ctrl+Shift+Y")));
    menuBar()->addMenu(tr("&Window"))->addAction(togglePreview);

    m_worker = new RenderWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &MainWindow::requestOpen, m_worker, &RenderWorker::openDocument);
    connect(this, &MainWindow::requestRender, m_worker, &RenderWorker::renderPage);
    connect(m_worker, &RenderWorker::documentOpened, this, &MainWindow::onOpened);
    connect(m_worker, &RenderWorker::pageRendered, this, &MainWindow::onRendered);
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

void MainWindow::buildActions()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    auto *open = file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    m_recent = file->addMenu(tr("Open &recent"));
    file->addAction(tr("&Properties…"), QKeySequence(tr("Ctrl+D")), this, &MainWindow::showProperties);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *view = menuBar()->addMenu(tr("&View"));
    auto *zoomIn = view->addAction(tr("Zoom &in"), QKeySequence::ZoomIn, this, [this] { zoomBy(1); });
    auto *zoomOut = view->addAction(tr("Zoom &out"), QKeySequence::ZoomOut, this, [this] { zoomBy(-1); });
    view->addAction(tr("&Actual size"), QKeySequence(tr("Ctrl+1")), this, [this] {
        m_fit = false;
        m_fitAction->setChecked(false);
        m_zoom = logicalDpiX() / 72.0;
        render();
    });
    m_fitAction = view->addAction(tr("&Fit page"), QKeySequence(tr("Ctrl+0")), this, &MainWindow::setFit);
    m_fitAction->setCheckable(true);
    m_fitAction->setChecked(true);
    view->addSeparator();
    auto *prev = view->addAction(tr("&Previous page"), QKeySequence(Qt::Key_PageUp), this,
                                 [this] { goToPage(m_page - 1); });
    auto *next = view->addAction(tr("&Next page"), QKeySequence(Qt::Key_PageDown), this,
                                 [this] { goToPage(m_page + 1); });

    auto *tools = menuBar()->addMenu(tr("&Tools"));
    auto *profiles = tools->addAction(tr("&Colour profiles…"), this, &MainWindow::showProfiles);

    auto *bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addAction(open);
    bar->addSeparator();
    bar->addAction(prev);
    m_pageSpin = new QSpinBox;
    m_pageSpin->setRange(0, 0);
    m_pageSpin->setKeyboardTracking(false);
    bar->addWidget(m_pageSpin);
    m_pageCount = new QLabel;
    bar->addWidget(m_pageCount);
    bar->addAction(next);
    bar->addSeparator();
    bar->addAction(zoomOut);
    bar->addAction(zoomIn);
    bar->addAction(m_fitAction);
    bar->addSeparator();
    bar->addAction(profiles);
    connect(m_pageSpin, &QSpinBox::valueChanged, this, [this](int v) { goToPage(v - 1); });

    updateRecentMenu();
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
    m_render = {};
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
    render();
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
    const QSizeF page = m_sizes.value(m_page);
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
    const QSizeF page = m_sizes.value(m_page);
    double limit = kMaxZoom;
    if (!page.isEmpty())
        limit = qMin(limit, std::sqrt(kMaxPixels / (page.width() * page.height())));
    m_zoom = qBound(kMinZoom, m_zoom, limit);
    m_zoomLabel->setText(tr("%1 %").arg(qRound(m_zoom * 72.0 / logicalDpiX() * 100)));
    m_worker->setLatestRequest(++m_requestId);
    emit requestRender(m_page, m_zoom, m_requestId);
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

void MainWindow::recomposite()
{
    if (!m_render.image)
        return;
    engine::CompositeOptions o;
    o.visible = m_panel->visibleInks();
    const QString choice = m_panel->simulationChoice();
    if (choice == QLatin1String("intent"))
        o.simulationProfile = m_info.outputIntentProfile;
    else if (!choice.isEmpty())
        o.simulationProfile = engine::ProfileLibrary::load(choice);
    const QString monitor = QSettings().value(QStringLiteral("monitorProfile")).toString();
    if (!monitor.isEmpty())
        o.monitorProfile = engine::ProfileLibrary::load(monitor);
    o.intent = m_panel->intent();
    o.showCoverage = m_panel->showCoverage();
    o.coverageLimit = m_panel->coverageLimit();

    bool calibrated = false;
    const QImage image = engine::composite(*m_render.image, o, &calibrated);
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
