#pragma once

#include "ClipItem.h"
#include "PreviewEngine.h"
#include "SourceListWidget.h"
#include "TimelineWidget.h"
#include "VideoExporter.h"

#include <QMainWindow>
#include <QList>
#include <QPoint>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QProcess;
class QPushButton;
class QTextEdit;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void addClips();
    void removeSelectedClip();
    void addSourceToTimeline(const QString &filePath, ClipMediaType mediaType, double timelineSeconds, int trackIndex);
    void moveTimelineClip(int clipIndex, double timelineSeconds, int trackIndex);
    void updateSelectedTrim();
    void updateSelectedTrack();
    void selectSource(int row);
    void selectTimelineClip(int clipIndex);
    void deleteTimelineClip(int clipIndex);
    void previewTimelineHover(int clipIndex, double sourceSeconds, QPoint globalAnchor);
    void previewTimelinePosition(int clipIndex, double sourceSeconds);
    void splitClipAtPosition(int clipIndex, double sourceSeconds);
    void jumpToStart();
    void togglePlayback();
    void enforceTimelineLoop();
    void renderHoverThumbnail();
    void showHoverThumbnail(int exitCode, QProcess::ExitStatus exitStatus);
    void updateTimelinePlayhead(qint64 positionMilliseconds);
    void updateSelectedDuration(qint64 durationMilliseconds);
    void exportVideo();
    void handleExportFinished(bool ok, const QString &message);

private:
    struct AudioLayerPreview {
        int clipIndex = -1;
        PreviewEngine *engine = nullptr;
    };

    void buildUi();
    void applyPlaybackIntent();
    void updatePlayPauseButtonIcon();
    QList<int> playableTimelineClipIndices() const;
    void startTimelinePlaybackAt(double timelineSeconds);
    void loadTimelineClip(int clipIndex, double sourceSeconds);
    void previewSource(int sourceIndex);
    void refreshClipList();
    void syncSelectedClipControls();
    void stopAudioLayers();
    void syncAudioLayers(double timelineSeconds);
    void setControlsEnabled(bool enabled);
    ClipItem *selectedClip();

    QList<ClipItem> m_sources;
    QList<ClipItem> m_clips;
    PreviewEngine *m_previewEngine = nullptr;
    VideoExporter *m_exporter = nullptr;
    QTimer *m_loopTimer = nullptr;
    QTimer *m_hoverPreviewTimer = nullptr;
    QProcess *m_hoverThumbnailProcess = nullptr;
    QList<AudioLayerPreview> m_audioLayerPreviews;

    SourceListWidget *m_clipList = nullptr;
    QDoubleSpinBox *m_startSpin = nullptr;
    QDoubleSpinBox *m_endSpin = nullptr;
    QDoubleSpinBox *m_timelineStartSpin = nullptr;
    QComboBox *m_trackCombo = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QTextEdit *m_log = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_toStartButton = nullptr;
    QPushButton *m_playPauseButton = nullptr;
    QLabel *m_hoverPreviewPopup = nullptr;
    QLabel *m_statusLabel = nullptr;
    int m_previewClipIndex = -1;
    int m_previewSourceIndex = -1;
    int m_selectedTimelineClipIndex = -1;
    double m_timelinePlaybackSeconds = 0.0;
    int m_pendingSeekClipIndex = -1;
    double m_pendingSeekSourceSeconds = 0.0;
    int m_pendingHoverClipIndex = -1;
    double m_pendingHoverSourceSeconds = 0.0;
    QPoint m_pendingHoverAnchor;
    bool m_hoverPreviewActive = false;
};
