#pragma once

#include "Customization.h"
#include "engine/ProfileLibrary.h"
#include "engine/Types.h"

#include <QHash>
#include <QMainWindow>
#include <QThread>

class OutputPreviewPanel;
class PageView;
class QDockWidget;
class RenderWorker;
class QLabel;
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
    void requestRender(int page, double zoom, int requestId);

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
    void showItemList();
    void resetCustomization();
    void chooseFile();
    void onOpened(const engine::DocumentInfo &info, const QList<QSizeF> &sizes);
    void onRendered(const engine::PageRender &render, int requestId);
    void goToPage(int page);
    void zoomBy(int steps);
    void setFit(bool fit);
    double fitZoom() const;
    void render();
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
    QList<QSizeF> m_sizes;
    engine::PageRender m_render;
    int m_page = 0;
    double m_zoom = 1.0;
    bool m_fit = true;
    int m_requestId = 0;
    QString m_screenshot;

    QSpinBox *m_pageSpin;
    QLabel *m_pageCount;
    QLabel *m_zoomLabel;
    QLabel *m_banner;
    QLabel *m_sizeLabel;
    QMenu *m_recent;
    QAction *m_fitAction;

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
