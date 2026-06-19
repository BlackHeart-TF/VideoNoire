#pragma once

#include "ClipItem.h"

#include <QList>
#include <QPoint>
#include <QWidget>

class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QMouseEvent;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setClips(const QList<ClipItem> &clips);
    void setPlayheadSeconds(double seconds);
    void setSelectedClip(int clipIndex);
    void setTrackCounts(int videoTracks, int audioTracks);

signals:
    void clipPositionHovered(int clipIndex, double sourceSeconds, QPoint globalAnchor);
    void clipContextSelected(int clipIndex);
    void clipDeleted(int clipIndex);
    void clipPositionSelected(int clipIndex, double sourceSeconds);
    void clipMoved(int clipIndex, double timelineSeconds, int trackIndex);
    void splitClipRequested(int clipIndex, double sourceSeconds);
    void sourceDropped(const QString &filePath, ClipMediaType mediaType, double timelineSeconds, int trackIndex);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct ClipRect {
        int clipIndex = -1;
        QRectF rect;
        double timelineStartSeconds = 0.0;
        double sourceStartSeconds = 0.0;
    };

    QRectF clipRectFor(int clipIndex) const;
    int clipTrackForLane(int lane, ClipMediaType mediaType) const;
    int laneAtPosition(const QPointF &position) const;
    int laneForClip(const ClipItem &clip) const;
    QString laneLabel(int lane) const;
    double pixelsPerSecond() const;
    double rulerHeight() const;
    double laneHeight() const;
    double labelWidth() const;
    double snappedTimelineStart(double proposedStartSeconds, int movingClipIndex, double movingDurationSeconds) const;
    double timelineEndSeconds() const;
    QList<ClipRect> clipRects() const;

    QList<ClipItem> m_clips;
    int m_selectedClip = -1;
    int m_videoTracks = 2;
    int m_audioTracks = 2;
    double m_playheadSeconds = 0.0;
    double m_hoverMarkerSeconds = 0.0;
    int m_dragClipIndex = -1;
    double m_dragGrabOffsetSeconds = 0.0;
    bool m_draggingClip = false;
    bool m_hasHoverMarker = false;
    int m_lastHoverClipIndex = -1;
    double m_lastHoverSourceSeconds = -1.0;
};
