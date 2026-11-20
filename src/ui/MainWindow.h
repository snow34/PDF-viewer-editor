#pragma once

#include "Customization.h"
#include "TileKey.h"
#include "engine/Compositor.h"
#include "engine/ProfileLibrary.h"
#include "engine/Types.h"
#include "preflight/Types.h"

#include <QHash>
#include <QMainWindow>
#include <QThread>

class OutputPreviewPanel;
class PageView;
class PreflightPanel;
class QDockWidget;
class RenderWorker;
class CompositeWorker;
class QTimer;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QMenu;
class QSpinBox;
class QToolBar;
class QWidgetAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Safe mode ignores the user's theme and layout files.
    explicit MainWindow(bool safeMode = false);
    ~MainWindow() override;

    void openFile(const QString &path);
    // Saves a picture of the window once the next page has been drawn, then quits.
    void screenshotWhenReady(const QString &file) { m_screenshot = file; }

signals:
    void requestOpen(const QString &path);
    void requestTiles(const QList<TileKey> &tiles, double zoom, int rotation, int generation, int batchId);
    void requestThumbnail(int page, int size, int rotation, const engine::CompositeOptions &options,
                          int generation);
    void requestTileComposite(const TileKey &key, int generation, int optionsId,
                              const std::shared_ptr<engine::SeparatedImage> &image, const QRect &area,
                              const engine::CompositeOptions &options);
    void requestSearch(const QString &text, int rotation, int searchId);
    void requestInspect(int page, QPointF point, int rotation);
    void requestPreflight(int standard, int checkId);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildActions();
    QAction *addItem(const QString &id, const QString &text, const QString &help,
                     const QKeySequence &shortcut = {});
    void applyCustomization();
    void applyLayout(const Layout &layout, QStringList *problems);
    void setDevMode(bool on);
    void applyBoxVisibility();
    void showItemList();
    void resetCustomization();
    void chooseFile();
    void onOpened(const engine::DocumentInfo &info, const QList<engine::PageInfo> &pages);
    void layoutPages();
    engine::PageBoxes rotatedBoxes(int page) const;
    void scheduleTiles();
    void onTileRendered(const TileKey &key, const engine::PageRender &render, int generation);
    void composeTile(const TileKey &key);
    void onTileComposed(const TileKey &key, int generation, int optionsId, const QImage &image, const QRect &area,
                        bool calibrated);
    void evictStore();
    void showInkValues();
    void onCurrentPageChanged(int page);
    void maybeTakeScreenshot();
    void onThumbnail(int page, const QImage &image, int generation);
    void requestNextThumbnail(int page);
    void restartThumbnails();
    void rotateBy(int degrees);
    QSizeF viewSize(int page) const;
    engine::CompositeOptions compositeOptions() const;
    void showSearchBar();
    void startSearch();
    void onSearchFinished(const QString &text, const QList<engine::SearchHit> &hits, int searchId);
    void stepSearch(int direction);
    void showCurrentHit();
    void onObjectsFound(int page, const QList<engine::ObjectInfo> &objects);
    void startPreflight();
    void showFinding(const preflight::Finding &finding);
    QRectF toCurrentRotation(const QRectF &rect, int page) const;
    void goToPage(int page);
    void zoomBy(int steps, QPoint anchor = QPoint(-1, -1));
    void setZoom(double zoom, QPoint anchor = QPoint(-1, -1));
    void setFit(bool fit);
    double fitZoom() const;
    void recomposite();
    void refreshProfiles();
    void updateBanner();
    void updateRecentMenu();
    void showProperties();
    void showProfiles();

    QThread m_thread;
    RenderWorker *m_worker;
    PageView *m_view;
    OutputPreviewPanel *m_panel;
    QDockWidget *m_previewWindow;
    bool m_previewPlaced = false;
    engine::ProfileLibrary m_library;

    engine::DocumentInfo m_info;
    QList<engine::PageInfo> m_pages;
    QList<QSizeF> m_sizes;  // points, rotation 0
    QStringList m_allInks;  // process inks and every spot ink in the document

    // Ink channels of the tiles the view needs, for the current zoom and
    // rotation (generation). Kept for ink readout and for compositing again
    // when the display settings change.
    struct StoredTile {
        std::shared_ptr<engine::SeparatedImage> image;
        QRect area;
    };
    QHash<TileKey, StoredTile> m_store;
    int m_storeGeneration = -1;
    int m_batchId = 0;
    engine::CompositeOptions m_options; // display settings for every tile
    int m_optionsId = 0;
    QTimer *m_scheduleTimer = nullptr;
    int m_hoverPage = -1;
    QPointF m_hoverPoint;

    QThread m_compositeThread;
    CompositeWorker *m_compositor = nullptr;
    QTimer *m_resizeTimer = nullptr;
    // Profile files are read once, not on every screen update.
    mutable QString m_simulationPath, m_monitorPath;
    mutable QByteArray m_simulationData, m_monitorData;
    QString m_systemMonitorProfile;
    int m_page = 0;
    double m_zoom = 1.0;
    bool m_fit = true;
    int m_rotation = 0;

    QDockWidget *m_thumbnailDock;
    QListWidget *m_thumbnails;
    int m_thumbGeneration = 0;
    int m_nextThumbnail = 0;

    QDockWidget *m_inspectorDock;
    QTreeWidget *m_inspector;

    QDockWidget *m_preflightDock;
    PreflightPanel *m_preflight;
    int m_preflightId = 0;

    QToolBar *m_searchBar;
    QLineEdit *m_searchField;
    QLabel *m_searchStatus;
    QString m_searchText;
    QList<engine::SearchHit> m_hits;
    int m_currentHit = -1;
    int m_searchId = 0;
    QString m_screenshot;

    QSpinBox *m_pageSpin;
    QLabel *m_pageCount;
    QLabel *m_zoomLabel;
    QLabel *m_banner;
    QLabel *m_sizeLabel;
    QMenu *m_recent;
    QAction *m_fitAction;
    QAction *m_boxTrim, *m_boxBleed, *m_boxCrop, *m_boxArt, *m_boxMedia;

    // Everything that can be placed in the toolbar or a menu, by id.
    QHash<QString, QAction *> m_items;
    QHash<QString, QKeySequence> m_defaultShortcuts;
    QList<QPair<QString, QString>> m_itemHelp; // id, description (in creation order)
    QToolBar *m_toolBar;
    QObject *m_layoutObjects = nullptr;
    QMenu *m_customiseMenu;
    QAction *m_devModeAction;
    QList<QAction *> m_devActions;
    Customization m_customization;
    bool m_safeMode;
};
