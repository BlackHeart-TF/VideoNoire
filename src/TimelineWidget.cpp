#include "TimelineWidget.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace {
constexpr const char *SourceMimeType = "application/x-clipthing-source";
}

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(static_cast<int>(rulerHeight() + laneHeight() * 4 + 24.0));
    setAcceptDrops(true);
    setMouseTracking(true);
}

void TimelineWidget::setClips(const QList<ClipItem> &clips)
{
    m_clips = clips;

    int videoTracks = 2;
    int audioTracks = 2;
    for (const ClipItem &clip : m_clips) {
        if (clip.mediaType == ClipMediaType::Video) {
            videoTracks = qMax(videoTracks, clip.trackIndex + 1);
        } else {
            audioTracks = qMax(audioTracks, clip.trackIndex + 1);
        }
    }

    setTrackCounts(videoTracks, audioTracks);
    update();
}

void TimelineWidget::setPlayheadSeconds(double seconds)
{
    m_playheadSeconds = qMax(0.0, seconds);
    update();
}

void TimelineWidget::setSelectedClip(int clipIndex)
{
    m_selectedClip = clipIndex;
    update();
}

void TimelineWidget::setTrackCounts(int videoTracks, int audioTracks)
{
    m_videoTracks = qMax(2, videoTracks);
    m_audioTracks = qMax(2, audioTracks);
    setMinimumHeight(static_cast<int>(rulerHeight() + laneHeight() * (m_videoTracks + m_audioTracks) + 24.0));
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF position = event->pos();
    for (const ClipRect &clipRect : clipRects()) {
        if (!clipRect.rect.contains(position)) {
            continue;
        }

        const double offsetSeconds = qMax(0.0, (position.x() - clipRect.rect.left()) / pixelsPerSecond());
        const double sourceSeconds = clipRect.sourceStartSeconds + offsetSeconds;

        m_selectedClip = clipRect.clipIndex;
        emit clipContextSelected(clipRect.clipIndex);
        update();

        QMenu menu(this);
        QAction *splitAction = menu.addAction(QStringLiteral("Split at Position"));
        QAction *deleteAction = menu.addAction(QStringLiteral("Delete"));
        QAction *selectedAction = menu.exec(event->globalPos());
        if (selectedAction == splitAction) {
            emit splitClipRequested(clipRect.clipIndex, sourceSeconds);
        } else if (selectedAction == deleteAction) {
            emit clipDeleted(clipRect.clipIndex);
        }
        return;
    }

    QWidget::contextMenuEvent(event);
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(SourceMimeType))) {
        event->acceptProposedAction();
        return;
    }

    QWidget::dragEnterEvent(event);
}

void TimelineWidget::dropEvent(QDropEvent *event)
{
    const QString mimeType = QString::fromLatin1(SourceMimeType);
    if (!event->mimeData()->hasFormat(mimeType)) {
        QWidget::dropEvent(event);
        return;
    }

    QByteArray payload = event->mimeData()->data(mimeType);
    QDataStream stream(&payload, QIODevice::ReadOnly);
    QString filePath;
    int mediaTypeValue = 0;
    double durationSeconds = 0.0;
    stream >> filePath >> mediaTypeValue >> durationSeconds;

    const QPointF position = event->position();
    const int lane = laneAtPosition(position);
    const ClipMediaType mediaType = static_cast<ClipMediaType>(mediaTypeValue);
    const int trackIndex = clipTrackForLane(lane, mediaType);
    const double durationSecondsOrDefault = durationSeconds > 0.0 ? durationSeconds : 10.0;
    const double rawTimelineSeconds = qMax(0.0, (position.x() - labelWidth()) / pixelsPerSecond());
    const double timelineSeconds = snappedTimelineStart(rawTimelineSeconds, -1, durationSecondsOrDefault);

    emit sourceDropped(filePath, mediaType, timelineSeconds, trackIndex);
    event->acceptProposedAction();
}

void TimelineWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)

    m_lastHoverClipIndex = -1;
    m_lastHoverSourceSeconds = -1.0;
    m_hasHoverMarker = false;
    update();
    emit clipPositionHovered(-1, 0.0, {});
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragClipIndex < 0 || !(event->buttons() & Qt::LeftButton)) {
        const QPointF position = event->position();
        for (const ClipRect &clipRect : clipRects()) {
            if (!clipRect.rect.contains(position)) {
                continue;
            }

            const double offsetSeconds = qMax(0.0, (position.x() - clipRect.rect.left()) / pixelsPerSecond());
            const double sourceSeconds = clipRect.sourceStartSeconds + offsetSeconds;
            m_hoverMarkerSeconds = clipRect.timelineStartSeconds + offsetSeconds;
            m_hasHoverMarker = true;
            update();
            if (clipRect.clipIndex != m_lastHoverClipIndex || std::abs(sourceSeconds - m_lastHoverSourceSeconds) > 0.050) {
                m_lastHoverClipIndex = clipRect.clipIndex;
                m_lastHoverSourceSeconds = sourceSeconds;
                const QPoint anchor = mapToGlobal(QPoint(static_cast<int>(labelWidth() + m_hoverMarkerSeconds * pixelsPerSecond()), 0));
                emit clipPositionHovered(clipRect.clipIndex, sourceSeconds, anchor);
            }
            return;
        }

        m_lastHoverClipIndex = -1;
        m_lastHoverSourceSeconds = -1.0;
        m_hasHoverMarker = false;
        update();
        emit clipPositionHovered(-1, 0.0, {});
        QWidget::mouseMoveEvent(event);
        return;
    }

    const ClipItem &clip = m_clips.at(m_dragClipIndex);
    const QPointF position = event->position();
    const double rawStart = (position.x() - labelWidth()) / pixelsPerSecond() - m_dragGrabOffsetSeconds;
    const double snappedStart = snappedTimelineStart(qMax(0.0, rawStart), m_dragClipIndex, clip.timelineDurationSeconds());
    const int lane = laneAtPosition(position);
    const int trackIndex = clipTrackForLane(lane, clip.mediaType);

    m_draggingClip = true;
    m_clips[m_dragClipIndex].timelineStartSeconds = snappedStart;
    m_clips[m_dragClipIndex].trackIndex = trackIndex;
    m_selectedClip = m_dragClipIndex;
    update();
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF position = event->position();
    for (const ClipRect &clipRect : clipRects()) {
        if (!clipRect.rect.contains(position)) {
            continue;
        }

        const double offsetSeconds = qMax(0.0, (position.x() - clipRect.rect.left()) / pixelsPerSecond());
        m_selectedClip = clipRect.clipIndex;
        m_playheadSeconds = clipRect.timelineStartSeconds + offsetSeconds;
        m_dragClipIndex = clipRect.clipIndex;
        m_dragGrabOffsetSeconds = offsetSeconds;
        m_draggingClip = false;
        emit clipPositionSelected(clipRect.clipIndex, clipRect.sourceStartSeconds + offsetSeconds);
        update();
        return;
    }

    if (position.x() >= labelWidth() && position.y() >= rulerHeight()) {
        m_playheadSeconds = qMax(0.0, (position.x() - labelWidth()) / pixelsPerSecond());
        update();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    if (m_dragClipIndex >= 0 && m_draggingClip) {
        const ClipItem &clip = m_clips.at(m_dragClipIndex);
        emit clipMoved(m_dragClipIndex, clip.timelineStartSeconds, clip.trackIndex);
    }

    m_dragClipIndex = -1;
    m_dragGrabOffsetSeconds = 0.0;
    m_draggingClip = false;
}

void TimelineWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(24, 27, 31));

    const double laneH = laneHeight();
    const double rulerH = rulerHeight();
    const double labelsW = labelWidth();
    const int totalLanes = m_videoTracks + m_audioTracks;
    const double endSeconds = timelineEndSeconds();
    const int tickCount = qMax(10, static_cast<int>(qCeil(endSeconds / 5.0)));

    painter.setPen(QColor(95, 101, 112));
    painter.drawLine(QPointF(labelsW, rulerH), QPointF(width(), rulerH));

    for (int tick = 0; tick <= tickCount; ++tick) {
        const double seconds = tick * 5.0;
        const double x = labelsW + seconds * pixelsPerSecond();
        painter.drawLine(QPointF(x, rulerH - 8.0), QPointF(x, height()));
        painter.setPen(QColor(210, 214, 222));
        painter.drawText(QRectF(x + 4.0, 4.0, 80.0, rulerH - 8.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%1s").arg(seconds, 0, 'f', 0));
        painter.setPen(QColor(58, 63, 72));
    }

    for (int lane = 0; lane < totalLanes; ++lane) {
        const double y = rulerH + lane * laneH;
        const QColor fill = lane % 2 == 0 ? QColor(34, 38, 44) : QColor(30, 34, 40);
        painter.fillRect(QRectF(0.0, y, width(), laneH), fill);
        painter.setPen(QColor(72, 78, 88));
        painter.drawLine(QPointF(0.0, y), QPointF(width(), y));
        painter.setPen(QColor(225, 229, 236));
        painter.drawText(QRectF(8.0, y, labelsW - 16.0, laneH), Qt::AlignVCenter | Qt::AlignLeft, laneLabel(lane));
    }

    for (const ClipRect &clipRect : clipRects()) {
        const ClipItem &clip = m_clips.at(clipRect.clipIndex);
        const bool selected = clipRect.clipIndex == m_selectedClip;
        const QColor baseColor = clip.mediaType == ClipMediaType::Video ? QColor(68, 132, 206) : QColor(83, 166, 112);
        const QColor borderColor = selected ? QColor(255, 214, 102) : QColor(18, 21, 26);

        painter.setBrush(baseColor);
        painter.setPen(QPen(borderColor, selected ? 3.0 : 1.5));
        painter.drawRoundedRect(clipRect.rect, 6.0, 6.0);

        painter.setPen(QColor(250, 250, 250));
        const QString name = QFileInfo(clip.filePath).fileName();
        const QString text = QStringLiteral("%1\nsrc %2")
                                 .arg(name)
                                 .arg(clip.sourceRangeLabel());
        painter.drawText(clipRect.rect.adjusted(8.0, 4.0, -8.0, -4.0), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
    }

    const double playheadX = labelsW + m_playheadSeconds * pixelsPerSecond();
    painter.setPen(QPen(QColor(255, 90, 90), 2.0));
    painter.drawLine(QPointF(playheadX, rulerH), QPointF(playheadX, height()));

    if (m_hasHoverMarker) {
        const double hoverX = labelsW + m_hoverMarkerSeconds * pixelsPerSecond();
        painter.setPen(QPen(QColor(255, 214, 102), 1.5, Qt::DashLine));
        painter.drawLine(QPointF(hoverX, 0.0), QPointF(hoverX, height()));
    }
}

QRectF TimelineWidget::clipRectFor(int clipIndex) const
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return {};
    }

    const ClipItem &clip = m_clips.at(clipIndex);
    const int lane = laneForClip(clip);
    const double x = labelWidth() + clip.timelineStartSeconds * pixelsPerSecond();
    const double y = rulerHeight() + lane * laneHeight() + 8.0;
    const double widthPixels = qMax(42.0, clip.timelineDurationSeconds() * pixelsPerSecond());
    return QRectF(x, y, widthPixels, laneHeight() - 16.0);
}

int TimelineWidget::clipTrackForLane(int lane, ClipMediaType mediaType) const
{
    if (mediaType == ClipMediaType::Video) {
        return qBound(0, lane, m_videoTracks - 1);
    }

    return qBound(0, lane - m_videoTracks, m_audioTracks - 1);
}

int TimelineWidget::laneAtPosition(const QPointF &position) const
{
    if (position.y() < rulerHeight()) {
        return 0;
    }

    const int totalLanes = m_videoTracks + m_audioTracks;
    const int lane = static_cast<int>((position.y() - rulerHeight()) / laneHeight());
    return qBound(0, lane, totalLanes - 1);
}

int TimelineWidget::laneForClip(const ClipItem &clip) const
{
    if (clip.mediaType == ClipMediaType::Video) {
        return qBound(0, clip.trackIndex, m_videoTracks - 1);
    }

    return m_videoTracks + qBound(0, clip.trackIndex, m_audioTracks - 1);
}

QString TimelineWidget::laneLabel(int lane) const
{
    if (lane < m_videoTracks) {
        return QStringLiteral("Video %1").arg(lane + 1);
    }

    return QStringLiteral("Audio %1").arg(lane - m_videoTracks + 1);
}

double TimelineWidget::pixelsPerSecond() const
{
    return 24.0;
}

double TimelineWidget::rulerHeight() const
{
    return 32.0;
}

double TimelineWidget::laneHeight() const
{
    return 58.0;
}

double TimelineWidget::labelWidth() const
{
    return 92.0;
}

double TimelineWidget::snappedTimelineStart(double proposedStartSeconds, int movingClipIndex, double movingDurationSeconds) const
{
    const double snapThresholdSeconds = 10.0 / pixelsPerSecond();
    double snappedStart = proposedStartSeconds;
    double bestDistance = snapThresholdSeconds;

    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == movingClipIndex) {
            continue;
        }

        const ClipItem &clip = m_clips.at(i);
        const double edges[] = {
            clip.timelineStartSeconds,
            clip.timelineStartSeconds + clip.timelineDurationSeconds()
        };

        for (double edge : edges) {
            const double startDistance = std::abs(proposedStartSeconds - edge);
            if (startDistance < bestDistance) {
                bestDistance = startDistance;
                snappedStart = edge;
            }

            const double endAlignedStart = edge - movingDurationSeconds;
            const double endDistance = std::abs(proposedStartSeconds - endAlignedStart);
            if (endDistance < bestDistance) {
                bestDistance = endDistance;
                snappedStart = endAlignedStart;
            }
        }
    }

    return qMax(0.0, snappedStart);
}

double TimelineWidget::timelineEndSeconds() const
{
    double end = 30.0;
    for (const ClipItem &clip : m_clips) {
        end = qMax(end, clip.timelineStartSeconds + clip.timelineDurationSeconds());
    }
    return end;
}

QList<TimelineWidget::ClipRect> TimelineWidget::clipRects() const
{
    QList<ClipRect> rects;
    for (int i = 0; i < m_clips.size(); ++i) {
        rects.append({ i, clipRectFor(i), m_clips.at(i).timelineStartSeconds, m_clips.at(i).startSeconds });
    }
    return rects;
}
