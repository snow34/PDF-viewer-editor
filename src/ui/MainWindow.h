#pragma once

#include "engine/ProfileLibrary.h"
#include "engine/Types.h"

#include <QMainWindow>
#include <QThread>

class OutputPreviewPanel;
class PageView;
class QDockWidget;
class RenderWorker;
class QLabel;
class QMenu;
class QSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
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
};
