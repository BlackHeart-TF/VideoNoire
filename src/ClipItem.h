#pragma once

#include <QFileInfo>
#include <QString>

enum class ClipMediaType {
    Video,
    Audio
};

struct ClipItem {
    QString filePath;
    ClipMediaType mediaType = ClipMediaType::Video;
    int trackIndex = 0;
    double timelineStartSeconds = 0.0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double sourceDurationSeconds = 0.0;

    double timelineDurationSeconds() const
    {
        if (endSeconds > startSeconds) {
            return endSeconds - startSeconds;
        }

        if (sourceDurationSeconds > startSeconds) {
            return sourceDurationSeconds - startSeconds;
        }

        return 10.0;
    }

    double sourceEndSeconds() const
    {
        if (endSeconds > startSeconds) {
            return endSeconds;
        }

        return sourceDurationSeconds;
    }

    QString sourceRangeLabel() const
    {
        const QString endText = sourceEndSeconds() > startSeconds
            ? QString::number(sourceEndSeconds(), 'f', 2)
            : QStringLiteral("end");
        return QStringLiteral("%1s -> %2s").arg(startSeconds, 0, 'f', 2).arg(endText);
    }

    QString timelineRangeLabel() const
    {
        return QStringLiteral("%1s -> %2s")
            .arg(timelineStartSeconds, 0, 'f', 2)
            .arg(timelineStartSeconds + timelineDurationSeconds(), 0, 'f', 2);
    }

    QString mediaTypeLabel() const
    {
        return mediaType == ClipMediaType::Video ? QStringLiteral("V") : QStringLiteral("A");
    }

    QString displayName() const
    {
        QFileInfo info(filePath);
        QString label = QStringLiteral("%1%2 timeline %3  %4")
                            .arg(mediaTypeLabel())
                            .arg(trackIndex + 1)
                            .arg(timelineRangeLabel())
                            .arg(info.fileName());
        label += QStringLiteral("  source [%1]").arg(sourceRangeLabel());
        return label;
    }
};
