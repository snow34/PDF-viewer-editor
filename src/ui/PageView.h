// Scrollable view of one rendered page with page box overlays.
#pragma once

#include "engine/Types.h"

#include <QImage>
#include <QScrollArea>

class PageCanvas;

class PageView : public QScrollArea {
    Q_OBJECT
public:
    explicit PageView(QWidget *parent = nullptr);

    // `zoom` is the scale the image was rendered at (pixels per point).
    void setPage(const QImage &image, const engine::PageBoxes &boxes, double zoom);
    void setImage(const QImage &image);
    void setShowBoxes(bool on);
    void clear();
    // Overlays, in points: search hits (one of them current) and the object
    // picked with the inspector.
    void setSearchHits(const QList<QRectF> &hits, const QList<QRectF> &current);
    void setSelection(const QRectF &bounds);

signals:
    void hovered(int x, int y); // image pixel under the cursor
    void hoverLeft();
    void clicked(QPointF point); // position on the page in points
    void zoomRequested(int steps);
    void fileDropped(const QString &path);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    PageCanvas *m_canvas;
};
