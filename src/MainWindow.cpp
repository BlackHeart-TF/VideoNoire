#include "MainWindow.h"

#include "GstPreviewEngine.h"

#include <algorithm>
#include <cmath>

#include <QBoxLayout>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>

namespace {
ClipMediaType mediaTypeForFile(const QString &filePath, ClipMediaType fallbackType)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QStringList audioExtensions = {
        QStringLiteral("aac"),
        QStringLiteral("flac"),
        QStringLiteral("m4a"),
        QStringLiteral("mp3"),
        QStringLiteral("ogg"),
        QStringLiteral("opus"),
        QStringLiteral("wav")
    };

    if (audioExtensions.contains(suffix)) {
        return ClipMediaType::Audio;
    }

    return fallbackType;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_previewEngine(new GstPreviewEngine(this))
    , m_exporter(new VideoExporter(this))
    , m_loopTimer(new QTimer(this))
    , m_hoverPreviewTimer(new QTimer(this))
    , m_hoverThumbnailProcess(new QProcess(this))
{
    buildUi();

    connect(m_clipList, &QListWidget::currentRowChanged, this, &MainWindow::selectSource);
    connect(m_startSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateSelectedTrim);
    connect(m_endSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateSelectedTrim);
    connect(m_timelineStartSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateSelectedTrim);
    connect(m_trackCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateSelectedTrack);
    connect(m_timeline, &TimelineWidget::clipPositionHovered, this, &MainWindow::previewTimelineHover);
    connect(m_timeline, &TimelineWidget::clipContextSelected, this, &MainWindow::selectTimelineClip);
    connect(m_timeline, &TimelineWidget::clipDeleted, this, &MainWindow::deleteTimelineClip);
    connect(m_timeline, &TimelineWidget::clipPositionSelected, this, &MainWindow::previewTimelinePosition);
    connect(m_timeline, &TimelineWidget::clipMoved, this, &MainWindow::moveTimelineClip);
    connect(m_timeline, &TimelineWidget::splitClipRequested, this, &MainWindow::splitClipAtPosition);
    connect(m_timeline, &TimelineWidget::sourceDropped, this, &MainWindow::addSourceToTimeline);
    connect(m_previewEngine, &PreviewEngine::durationChanged, this, &MainWindow::updateSelectedDuration);
    connect(m_previewEngine, &PreviewEngine::positionChanged, this, &MainWindow::updateTimelinePlayhead);
    connect(m_previewEngine, &PreviewEngine::errorOccurred, m_log, &QTextEdit::append);
    connect(m_loopTimer, &QTimer::timeout, this, &MainWindow::enforceTimelineLoop);
    m_hoverPreviewTimer->setSingleShot(true);
    connect(m_hoverPreviewTimer, &QTimer::timeout, this, &MainWindow::renderHoverThumbnail);
    connect(m_hoverThumbnailProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &MainWindow::showHoverThumbnail);
    connect(m_exporter, &VideoExporter::logMessage, m_log, &QTextEdit::append);
    connect(m_exporter, &VideoExporter::finished, this, &MainWindow::handleExportFinished);
    m_loopTimer->start(100);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("ClipThing"));
    resize(1100, 720);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);

    auto *leftPanel = new QVBoxLayout();
    auto *clipControls = new QHBoxLayout();
    auto *addButton = new QPushButton(QStringLiteral("Add Media"));
    auto *removeButton = new QPushButton(QStringLiteral("Remove"));

    clipControls->addWidget(addButton);
    clipControls->addWidget(removeButton);

    m_clipList = new SourceListWidget();
    leftPanel->addLayout(clipControls);
    leftPanel->addWidget(m_clipList, 1);

    auto *trimBox = new QGroupBox(QStringLiteral("Selected Clip Trim"));
    auto *trimLayout = new QFormLayout(trimBox);
    m_startSpin = new QDoubleSpinBox();
    m_endSpin = new QDoubleSpinBox();
    m_timelineStartSpin = new QDoubleSpinBox();
    for (QDoubleSpinBox *spin : { m_startSpin, m_endSpin, m_timelineStartSpin }) {
        spin->setRange(0.0, 24.0 * 60.0 * 60.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.5);
        spin->setSuffix(QStringLiteral(" s"));
    }
    m_trackCombo = new QComboBox();
    for (int i = 0; i < 4; ++i) {
        m_trackCombo->addItem(QStringLiteral("Track %1").arg(i + 1), i);
    }
    trimLayout->addRow(QStringLiteral("Timeline Start"), m_timelineStartSpin);
    trimLayout->addRow(QStringLiteral("Track"), m_trackCombo);
    trimLayout->addRow(QStringLiteral("Start"), m_startSpin);
    trimLayout->addRow(QStringLiteral("End (0 = full clip)"), m_endSpin);
    leftPanel->addWidget(trimBox);

    auto *exportBox = new QGroupBox(QStringLiteral("Export"));
    auto *exportLayout = new QHBoxLayout(exportBox);
    m_exportButton = new QPushButton(QStringLiteral("Export Video"));
    exportLayout->addStretch(1);
    exportLayout->addWidget(m_exportButton);
    exportLayout->addStretch(1);
    leftPanel->addWidget(exportBox);

    auto *rightPanel = new QVBoxLayout();
    auto *videoWidget = new QWidget();
    videoWidget->setMinimumSize(520, 300);
    videoWidget->setStyleSheet(QStringLiteral("background: black;"));
    m_previewEngine->setVideoOutput(videoWidget);
    auto *playbackControls = new QHBoxLayout();
    m_toStartButton = new QPushButton();
    m_toStartButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    m_toStartButton->setText(QStringLiteral("Start"));
    m_toStartButton->setToolTip(QStringLiteral("To start"));
    m_playPauseButton = new QPushButton();
    m_playPauseButton->setCheckable(true);
    m_playPauseButton->setToolTip(QStringLiteral("Play"));
    updatePlayPauseButtonIcon();
    playbackControls->addStretch(1);
    playbackControls->addWidget(m_toStartButton);
    playbackControls->addWidget(m_playPauseButton);
    playbackControls->addStretch(1);
    m_timeline = new TimelineWidget();

    m_statusLabel = new QLabel(QStringLiteral("Add clips to start."));
    m_hoverPreviewPopup = new QLabel(nullptr, Qt::ToolTip);
    m_hoverPreviewPopup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_hoverPreviewPopup->setStyleSheet(QStringLiteral("background: #111; border: 1px solid #ddd; padding: 2px;"));
    m_log = new QTextEdit();
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("Export log"));

    rightPanel->addWidget(videoWidget, 3);
    rightPanel->addLayout(playbackControls);
    rightPanel->addWidget(m_statusLabel);
    rightPanel->addWidget(m_timeline, 2);
    rightPanel->addWidget(m_log, 2);

    rootLayout->addLayout(leftPanel, 2);
    rootLayout->addLayout(rightPanel, 3);
    setCentralWidget(central);

    connect(addButton, &QPushButton::clicked, this, &MainWindow::addClips);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedClip);
    connect(m_toStartButton, &QPushButton::clicked, this, &MainWindow::jumpToStart);
    connect(m_playPauseButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::exportVideo);
}

void MainWindow::updatePlayPauseButtonIcon()
{
    const bool playing = m_playPauseButton->isChecked();
    m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    m_playPauseButton->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    m_playPauseButton->setToolTip(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
}

void MainWindow::applyPlaybackIntent()
{
    if (m_playPauseButton->isChecked() && !m_previewEngine->hasSource()) {
        startTimelinePlaybackAt(m_timelinePlaybackSeconds);
        return;
    }

    if (!m_previewEngine->hasSource()) {
        return;
    }

    if (m_playPauseButton->isChecked()) {
        m_previewEngine->play();
    } else {
        m_previewEngine->pause();
    }
    syncAudioLayers(m_timelinePlaybackSeconds);
}

QList<int> MainWindow::playableTimelineClipIndices() const
{
    QList<int> indices;
    bool hasVideo = false;
    for (const ClipItem &clip : m_clips) {
        if (clip.mediaType == ClipMediaType::Video) {
            hasVideo = true;
            break;
        }
    }

    for (int i = 0; i < m_clips.size(); ++i) {
        if (!hasVideo || m_clips.at(i).mediaType == ClipMediaType::Video) {
            indices.append(i);
        }
    }

    std::sort(indices.begin(), indices.end(), [this](int leftIndex, int rightIndex) {
        const ClipItem &left = m_clips.at(leftIndex);
        const ClipItem &right = m_clips.at(rightIndex);
        if (std::abs(left.timelineStartSeconds - right.timelineStartSeconds) > 0.001) {
            return left.timelineStartSeconds < right.timelineStartSeconds;
        }
        return left.trackIndex < right.trackIndex;
    });

    return indices;
}

void MainWindow::startTimelinePlaybackAt(double timelineSeconds)
{
    const QList<int> indices = playableTimelineClipIndices();
    if (indices.isEmpty()) {
        return;
    }

    int chosenIndex = indices.first();
    double chosenSourceSeconds = m_clips.at(chosenIndex).startSeconds;

    for (int clipIndex : indices) {
        const ClipItem &clip = m_clips.at(clipIndex);
        const double clipStart = clip.timelineStartSeconds;
        const double clipEnd = clip.timelineStartSeconds + clip.timelineDurationSeconds();
        if (timelineSeconds >= clipStart && timelineSeconds < clipEnd) {
            chosenIndex = clipIndex;
            chosenSourceSeconds = clip.startSeconds + (timelineSeconds - clipStart);
            break;
        }

        if (clipStart >= timelineSeconds) {
            chosenIndex = clipIndex;
            chosenSourceSeconds = clip.startSeconds;
            break;
        }
    }

    loadTimelineClip(chosenIndex, chosenSourceSeconds);
}

void MainWindow::loadTimelineClip(int clipIndex, double sourceSeconds)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    const ClipItem &clip = m_clips.at(clipIndex);
    m_hoverPreviewActive = false;
    m_selectedTimelineClipIndex = clipIndex;
    m_timeline->setSelectedClip(clipIndex);
    m_previewClipIndex = clipIndex;
    m_previewSourceIndex = -1;
    m_previewEngine->setSource(clip.filePath);
    m_previewEngine->setPosition(static_cast<qint64>(sourceSeconds * 1000.0));
    m_timelinePlaybackSeconds = clip.timelineStartSeconds + qMax(0.0, sourceSeconds - clip.startSeconds);
    m_pendingSeekClipIndex = clipIndex;
    m_pendingSeekSourceSeconds = sourceSeconds;
    m_timeline->setPlayheadSeconds(m_timelinePlaybackSeconds);

    if (m_playPauseButton->isChecked()) {
        m_previewEngine->play();
    } else {
        m_previewEngine->pause();
    }
    syncAudioLayers(m_timelinePlaybackSeconds);
}

void MainWindow::addClips()
{
    QString dialogTitle = QStringLiteral("Add media");
    const QString fileFilter = QStringLiteral("Media Files (*.mp4 *.mov *.mkv *.avi *.webm *.mp3 *.wav *.aac *.flac *.ogg *.opus *.m4a);;Video Files (*.mp4 *.mov *.mkv *.avi *.webm);;Audio Files (*.mp3 *.wav *.aac *.flac *.ogg *.opus *.m4a);;All Files (*)");
    QString selectedFilter = QStringLiteral("Media Files (*.mp4 *.mov *.mkv *.avi *.webm *.mp3 *.wav *.aac *.flac *.ogg *.opus *.m4a)");
    ClipMediaType fallbackType = ClipMediaType::Video;

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        dialogTitle,
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        fileFilter,
        &selectedFilter);

    if (selectedFilter.startsWith(QStringLiteral("Audio Files"))) {
        fallbackType = ClipMediaType::Audio;
    }

    const bool shouldSeedTimeline = m_sources.isEmpty() && m_clips.isEmpty();
    for (const QString &file : files) {
        ClipItem source;
        source.filePath = file;
        source.mediaType = mediaTypeForFile(file, fallbackType);
        m_sources.append(source);
    }

    if (shouldSeedTimeline && !files.isEmpty()) {
        addSourceToTimeline(files.first(), mediaTypeForFile(files.first(), fallbackType), 0.0, 0);
    }

    refreshClipList();
    if (!files.isEmpty() && m_clipList->currentRow() < 0) {
        m_clipList->setCurrentRow(0);
    }
}

void MainWindow::removeSelectedClip()
{
    const int row = m_clipList->currentRow();
    if (row < 0 || row >= m_sources.size()) {
        return;
    }

    m_sources.removeAt(row);
    refreshClipList();
    if (!m_sources.isEmpty()) {
        m_clipList->setCurrentRow(qMin(row, m_sources.size() - 1));
    }
}

void MainWindow::addSourceToTimeline(const QString &filePath, ClipMediaType mediaType, double timelineSeconds, int trackIndex)
{
    ClipItem clip;
    clip.filePath = filePath;
    clip.mediaType = mediaType;
    clip.trackIndex = trackIndex;
    clip.timelineStartSeconds = timelineSeconds;

    for (const ClipItem &source : m_sources) {
        if (source.filePath == filePath && source.mediaType == mediaType) {
            clip.sourceDurationSeconds = source.sourceDurationSeconds;
            break;
        }
    }

    m_clips.append(clip);
    m_selectedTimelineClipIndex = m_clips.size() - 1;
    refreshClipList();
    m_timeline->setSelectedClip(m_selectedTimelineClipIndex);
    m_statusLabel->setText(QStringLiteral("Added %1 to timeline").arg(QFileInfo(filePath).fileName()));
}

void MainWindow::moveTimelineClip(int clipIndex, double timelineSeconds, int trackIndex)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    ClipItem &clip = m_clips[clipIndex];
    clip.timelineStartSeconds = timelineSeconds;
    clip.trackIndex = trackIndex;
    m_selectedTimelineClipIndex = clipIndex;
    m_timelinePlaybackSeconds = timelineSeconds;

    m_timelineStartSpin->blockSignals(true);
    m_trackCombo->blockSignals(true);
    m_timelineStartSpin->setValue(clip.timelineStartSeconds);
    m_trackCombo->setCurrentIndex(qBound(0, clip.trackIndex, m_trackCombo->count() - 1));
    m_timelineStartSpin->blockSignals(false);
    m_trackCombo->blockSignals(false);

    refreshClipList();
    m_timeline->setSelectedClip(clipIndex);
}

void MainWindow::updateSelectedTrim()
{
    ClipItem *clip = selectedClip();
    if (clip == nullptr) {
        return;
    }

    clip->startSeconds = m_startSpin->value();
    clip->endSeconds = m_endSpin->value();
    clip->timelineStartSeconds = m_timelineStartSpin->value();
    refreshClipList();
}

void MainWindow::updateSelectedTrack()
{
    ClipItem *clip = selectedClip();
    if (clip == nullptr) {
        return;
    }

    clip->trackIndex = m_trackCombo->currentData().toInt();
    refreshClipList();
}

void MainWindow::selectSource(int row)
{
    if (row < 0 || row >= m_sources.size()) {
        return;
    }

    previewSource(row);
}

void MainWindow::previewSource(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_sources.size()) {
        return;
    }

    const ClipItem &source = m_sources.at(sourceIndex);
    m_previewClipIndex = -1;
    m_previewSourceIndex = sourceIndex;
    m_pendingSeekClipIndex = -1;
    stopAudioLayers();
    m_previewEngine->setSource(source.filePath);
    m_previewEngine->setPosition(0);
    applyPlaybackIntent();
    m_statusLabel->setText(QFileInfo(source.filePath).absoluteFilePath());
}

void MainWindow::selectTimelineClip(int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    m_selectedTimelineClipIndex = clipIndex;
    m_timeline->setSelectedClip(clipIndex);
    syncSelectedClipControls();
}

void MainWindow::syncSelectedClipControls()
{
    const ClipItem *clip = selectedClip();
    if (clip == nullptr) {
        return;
    }

    m_startSpin->blockSignals(true);
    m_endSpin->blockSignals(true);
    m_timelineStartSpin->blockSignals(true);
    m_trackCombo->blockSignals(true);
    m_startSpin->setValue(clip->startSeconds);
    m_endSpin->setValue(clip->endSeconds);
    m_timelineStartSpin->setValue(clip->timelineStartSeconds);
    m_trackCombo->setCurrentIndex(qBound(0, clip->trackIndex, m_trackCombo->count() - 1));
    m_startSpin->blockSignals(false);
    m_endSpin->blockSignals(false);
    m_timelineStartSpin->blockSignals(false);
    m_trackCombo->blockSignals(false);
}

void MainWindow::deleteTimelineClip(int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    const bool deletingPreviewClip = clipIndex == m_previewClipIndex;
    m_clips.removeAt(clipIndex);

    if (m_selectedTimelineClipIndex == clipIndex) {
        m_selectedTimelineClipIndex = -1;
    } else if (m_selectedTimelineClipIndex > clipIndex) {
        --m_selectedTimelineClipIndex;
    }

    if (m_previewClipIndex == clipIndex) {
        m_previewClipIndex = -1;
        m_previewEngine->stop();
    } else if (m_previewClipIndex > clipIndex) {
        --m_previewClipIndex;
    }

    for (AudioLayerPreview &layer : m_audioLayerPreviews) {
        if (layer.clipIndex > clipIndex) {
            --layer.clipIndex;
        }
    }

    stopAudioLayers();
    refreshClipList();
    m_timeline->setSelectedClip(m_selectedTimelineClipIndex);

    if (deletingPreviewClip && m_playPauseButton->isChecked()) {
        startTimelinePlaybackAt(m_timelinePlaybackSeconds);
    }
}

void MainWindow::previewTimelineHover(int clipIndex, double sourceSeconds, QPoint globalAnchor)
{
    if (m_playPauseButton->isChecked() || clipIndex < 0 || clipIndex >= m_clips.size()) {
        m_hoverPreviewTimer->stop();
        if (m_hoverThumbnailProcess->state() != QProcess::NotRunning) {
            m_hoverThumbnailProcess->kill();
        }
        m_hoverPreviewPopup->hide();
        return;
    }

    const ClipItem &clip = m_clips.at(clipIndex);
    if (clip.mediaType != ClipMediaType::Video) {
        m_hoverPreviewTimer->stop();
        if (m_hoverThumbnailProcess->state() != QProcess::NotRunning) {
            m_hoverThumbnailProcess->kill();
        }
        m_hoverPreviewPopup->hide();
        return;
    }

    m_pendingHoverClipIndex = clipIndex;
    m_pendingHoverSourceSeconds = sourceSeconds;
    m_pendingHoverAnchor = globalAnchor;
    m_hoverPreviewTimer->start(120);
}

void MainWindow::renderHoverThumbnail()
{
    if (m_pendingHoverClipIndex < 0 || m_pendingHoverClipIndex >= m_clips.size()) {
        m_hoverPreviewPopup->hide();
        return;
    }

    const ClipItem &clip = m_clips.at(m_pendingHoverClipIndex);
    if (clip.mediaType != ClipMediaType::Video) {
        m_hoverPreviewPopup->hide();
        return;
    }

    if (m_hoverThumbnailProcess->state() != QProcess::NotRunning) {
        m_hoverThumbnailProcess->kill();
    }

    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error");
    args << QStringLiteral("-ss") << QString::number(m_pendingHoverSourceSeconds, 'f', 3);
    args << QStringLiteral("-i") << clip.filePath;
    args << QStringLiteral("-frames:v") << QStringLiteral("1");
    args << QStringLiteral("-vf") << QStringLiteral("scale=168:-1");
    args << QStringLiteral("-f") << QStringLiteral("image2pipe");
    args << QStringLiteral("-vcodec") << QStringLiteral("mjpeg");
    args << QStringLiteral("pipe:1");

    m_hoverThumbnailProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_hoverThumbnailProcess->start(QStringLiteral("ffmpeg"), args);
}

void MainWindow::showHoverThumbnail(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_hoverPreviewPopup->hide();
        return;
    }

    QPixmap thumbnail;
    if (!thumbnail.loadFromData(m_hoverThumbnailProcess->readAllStandardOutput(), "JPG")) {
        m_hoverPreviewPopup->hide();
        return;
    }

    m_hoverPreviewPopup->setPixmap(thumbnail);
    m_hoverPreviewPopup->adjustSize();
    m_hoverPreviewPopup->move(
        m_pendingHoverAnchor.x(),
        m_pendingHoverAnchor.y() - m_hoverPreviewPopup->height());
    m_hoverPreviewPopup->show();
}

void MainWindow::previewTimelinePosition(int clipIndex, double sourceSeconds)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    m_selectedTimelineClipIndex = clipIndex;
    m_timeline->setSelectedClip(clipIndex);
    syncSelectedClipControls();

    const ClipItem &clip = m_clips.at(clipIndex);
    loadTimelineClip(clipIndex, sourceSeconds);
    m_hoverPreviewActive = false;
    m_statusLabel->setText(QStringLiteral("Previewing %1 at %2s")
                               .arg(QFileInfo(m_clips.at(clipIndex).filePath).fileName())
                               .arg(sourceSeconds, 0, 'f', 2));
}

void MainWindow::splitClipAtPosition(int clipIndex, double sourceSeconds)
{
    if (clipIndex < 0 || clipIndex >= m_clips.size()) {
        return;
    }

    ClipItem &clip = m_clips[clipIndex];
    const double clipStart = clip.startSeconds;
    const double clipEnd = clip.endSeconds > clip.startSeconds
        ? clip.endSeconds
        : clip.startSeconds + clip.timelineDurationSeconds();

    if (sourceSeconds <= clipStart + 0.050 || sourceSeconds >= clipEnd - 0.050) {
        QMessageBox::information(this, QStringLiteral("Split skipped"), QStringLiteral("Choose a split point inside the clip, away from the edges."));
        return;
    }

    ClipItem rightClip = clip;
    const double leftDuration = sourceSeconds - clipStart;

    clip.endSeconds = sourceSeconds;
    rightClip.startSeconds = sourceSeconds;
    rightClip.endSeconds = clipEnd;
    rightClip.timelineStartSeconds = clip.timelineStartSeconds + leftDuration;

    m_clips.insert(clipIndex + 1, rightClip);
    m_selectedTimelineClipIndex = clipIndex + 1;
    refreshClipList();
    m_timeline->setSelectedClip(m_selectedTimelineClipIndex);
    m_statusLabel->setText(QStringLiteral("Split clip at %1s").arg(sourceSeconds, 0, 'f', 2));
}

void MainWindow::jumpToStart()
{
    m_hoverPreviewTimer->stop();
    m_hoverPreviewPopup->hide();
    if (m_hoverThumbnailProcess->state() != QProcess::NotRunning) {
        m_hoverThumbnailProcess->kill();
    }

    m_timelinePlaybackSeconds = 0.0;
    m_timeline->setPlayheadSeconds(0.0);

    const QList<int> indices = playableTimelineClipIndices();
    if (indices.isEmpty()) {
        m_previewClipIndex = -1;
        m_previewEngine->stop();
        stopAudioLayers();
        return;
    }

    loadTimelineClip(indices.first(), m_clips.at(indices.first()).startSeconds);
}

void MainWindow::togglePlayback()
{
    updatePlayPauseButtonIcon();
    applyPlaybackIntent();
}

void MainWindow::enforceTimelineLoop()
{
    if (!m_playPauseButton->isChecked()) {
        return;
    }

    if (m_previewClipIndex < 0 || m_previewClipIndex >= m_clips.size()) {
        startTimelinePlaybackAt(m_timelinePlaybackSeconds);
        syncAudioLayers(m_timelinePlaybackSeconds);
        return;
    }

    const ClipItem &clip = m_clips.at(m_previewClipIndex);
    const double startSeconds = clip.startSeconds;
    double endSeconds = clip.sourceEndSeconds();
    if (endSeconds <= startSeconds) {
        endSeconds = startSeconds + clip.timelineDurationSeconds();
    }

    if (endSeconds <= startSeconds) {
        return;
    }

    const qint64 endPosition = static_cast<qint64>(endSeconds * 1000.0);
    if (m_previewEngine->position() >= endPosition) {
        const QList<int> indices = playableTimelineClipIndices();
        if (indices.isEmpty()) {
            return;
        }

        int nextIndex = indices.first();
        for (int i = 0; i < indices.size(); ++i) {
            if (indices.at(i) == m_previewClipIndex) {
                if (i + 1 < indices.size()) {
                    nextIndex = indices.at(i + 1);
                }
                break;
            }
        }

        loadTimelineClip(nextIndex, m_clips.at(nextIndex).startSeconds);
    } else if (!m_previewEngine->isPlaying() && m_previewEngine->hasSource()) {
        if (m_previewEngine->position() >= endPosition - 250) {
            const QList<int> indices = playableTimelineClipIndices();
            if (indices.isEmpty()) {
                return;
            }

            int nextIndex = indices.first();
            for (int i = 0; i < indices.size(); ++i) {
                if (indices.at(i) == m_previewClipIndex) {
                    if (i + 1 < indices.size()) {
                        nextIndex = indices.at(i + 1);
                    }
                    break;
                }
            }

            loadTimelineClip(nextIndex, m_clips.at(nextIndex).startSeconds);
            return;
        }

        m_previewEngine->play();
    }
    syncAudioLayers(m_timelinePlaybackSeconds);
}

void MainWindow::updateTimelinePlayhead(qint64 positionMilliseconds)
{
    if (m_previewClipIndex < 0 || m_previewClipIndex >= m_clips.size()) {
        return;
    }

    const ClipItem &clip = m_clips.at(m_previewClipIndex);
    const double sourceSeconds = static_cast<double>(positionMilliseconds) / 1000.0;

    if (m_pendingSeekClipIndex == m_previewClipIndex) {
        if (std::abs(sourceSeconds - m_pendingSeekSourceSeconds) > 1.5) {
            return;
        }
        m_pendingSeekClipIndex = -1;
    }

    if (m_hoverPreviewActive && !m_playPauseButton->isChecked()) {
        return;
    }

    const double timelineSeconds = clip.timelineStartSeconds + qMax(0.0, sourceSeconds - clip.startSeconds);
    m_timelinePlaybackSeconds = timelineSeconds;
    m_timeline->setPlayheadSeconds(timelineSeconds);
    syncAudioLayers(timelineSeconds);
}

void MainWindow::updateSelectedDuration(qint64 durationMilliseconds)
{
    if (durationMilliseconds <= 0) {
        return;
    }

    const double durationSeconds = static_cast<double>(durationMilliseconds) / 1000.0;

    if (m_previewSourceIndex >= 0 && m_previewSourceIndex < m_sources.size()) {
        ClipItem &source = m_sources[m_previewSourceIndex];
        if (!qFuzzyCompare(source.sourceDurationSeconds, durationSeconds)) {
            source.sourceDurationSeconds = durationSeconds;
            for (ClipItem &clip : m_clips) {
                if (clip.filePath == source.filePath && clip.mediaType == source.mediaType) {
                    clip.sourceDurationSeconds = durationSeconds;
                }
            }
            refreshClipList();
        }
        return;
    }

    ClipItem *clip = selectedClip();
    if (clip != nullptr && !qFuzzyCompare(clip->sourceDurationSeconds, durationSeconds)) {
        clip->sourceDurationSeconds = durationSeconds;
        for (ClipItem &source : m_sources) {
            if (source.filePath == clip->filePath && source.mediaType == clip->mediaType) {
                source.sourceDurationSeconds = durationSeconds;
            }
        }
        for (ClipItem &timelineClip : m_clips) {
            if (timelineClip.filePath == clip->filePath && timelineClip.mediaType == clip->mediaType) {
                timelineClip.sourceDurationSeconds = durationSeconds;
            }
        }
        refreshClipList();
    }
}

void MainWindow::exportVideo()
{
    QList<ClipItem> videoClips;
    for (const ClipItem &clip : m_clips) {
        if (clip.mediaType == ClipMediaType::Video) {
            videoClips.append(clip);
        }
    }

    std::sort(videoClips.begin(), videoClips.end(), [](const ClipItem &left, const ClipItem &right) {
        if (std::abs(left.timelineStartSeconds - right.timelineStartSeconds) > 0.001) {
            return left.timelineStartSeconds < right.timelineStartSeconds;
        }
        return left.trackIndex < right.trackIndex;
    });

    if (videoClips.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No clips"), QStringLiteral("Add at least one clip before exporting."));
        return;
    }

    const QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export video"),
        QDir::home().filePath(QStringLiteral("clipthing-export.mp4")),
        QStringLiteral("MP4 Video (*.mp4);;All Files (*)"));

    if (outputPath.trimmed().isEmpty()) {
        return;
    }

    m_log->clear();
    setControlsEnabled(false);
    m_exporter->exportProject(m_clips, outputPath.trimmed());
}

void MainWindow::handleExportFinished(bool ok, const QString &message)
{
    setControlsEnabled(true);
    m_statusLabel->setText(message);
    m_log->append(message);

    if (ok) {
        QMessageBox::information(this, QStringLiteral("Export complete"), message);
    } else {
        QMessageBox::critical(this, QStringLiteral("Export failed"), message);
    }
}

void MainWindow::refreshClipList()
{
    const int currentRow = m_clipList->currentRow();
    m_clipList->blockSignals(true);
    m_clipList->clear();
    for (int i = 0; i < m_sources.size(); ++i) {
        const ClipItem &source = m_sources.at(i);
        auto *item = new QListWidgetItem(QStringLiteral("%1%2  %3")
                                             .arg(source.mediaTypeLabel())
                                             .arg(i + 1)
                                             .arg(QFileInfo(source.filePath).fileName()));
        item->setData(Qt::UserRole, source.filePath);
        item->setData(Qt::UserRole + 1, static_cast<int>(source.mediaType));
        item->setData(Qt::UserRole + 2, source.sourceDurationSeconds);
        m_clipList->addItem(item);
    }
    if (currentRow >= 0 && currentRow < m_sources.size()) {
        m_clipList->setCurrentRow(currentRow);
    }
    m_clipList->blockSignals(false);
    m_timeline->setClips(m_clips);
    m_timeline->setSelectedClip(m_selectedTimelineClipIndex);
}

void MainWindow::stopAudioLayers()
{
    for (const AudioLayerPreview &layer : m_audioLayerPreviews) {
        if (layer.engine != nullptr) {
            layer.engine->stop();
            layer.engine->deleteLater();
        }
    }
    m_audioLayerPreviews.clear();
}

void MainWindow::syncAudioLayers(double timelineSeconds)
{
    QList<int> desiredClipIndices;
    for (int i = 0; i < m_clips.size(); ++i) {
        const ClipItem &clip = m_clips.at(i);
        if (clip.mediaType != ClipMediaType::Audio || i == m_previewClipIndex) {
            continue;
        }

        const double clipStart = clip.timelineStartSeconds;
        const double clipEnd = clip.timelineStartSeconds + clip.timelineDurationSeconds();
        if (timelineSeconds >= clipStart && timelineSeconds < clipEnd) {
            desiredClipIndices.append(i);
        }
    }

    for (int i = m_audioLayerPreviews.size() - 1; i >= 0; --i) {
        const AudioLayerPreview &layer = m_audioLayerPreviews.at(i);
        if (!desiredClipIndices.contains(layer.clipIndex)) {
            if (layer.engine != nullptr) {
                layer.engine->stop();
                layer.engine->deleteLater();
            }
            m_audioLayerPreviews.removeAt(i);
        }
    }

    for (int clipIndex : desiredClipIndices) {
        const ClipItem &clip = m_clips.at(clipIndex);
        const qint64 desiredPositionMs = static_cast<qint64>((clip.startSeconds + (timelineSeconds - clip.timelineStartSeconds)) * 1000.0);

        PreviewEngine *engine = nullptr;
        for (const AudioLayerPreview &layer : m_audioLayerPreviews) {
            if (layer.clipIndex == clipIndex) {
                engine = layer.engine;
                break;
            }
        }

        if (engine == nullptr) {
            engine = new GstPreviewEngine(this);
            connect(engine, &PreviewEngine::errorOccurred, m_log, &QTextEdit::append);
            engine->setSource(clip.filePath);
            engine->setPosition(desiredPositionMs);
            m_audioLayerPreviews.append({ clipIndex, engine });
        } else if (std::abs(engine->position() - desiredPositionMs) > 500) {
            engine->setPosition(desiredPositionMs);
        }

        if (m_playPauseButton->isChecked()) {
            engine->play();
        } else {
            engine->pause();
        }
    }
}

void MainWindow::setControlsEnabled(bool enabled)
{
    m_clipList->setEnabled(enabled);
    m_startSpin->setEnabled(enabled && selectedClip() != nullptr);
    m_endSpin->setEnabled(enabled && selectedClip() != nullptr);
    m_timelineStartSpin->setEnabled(enabled && selectedClip() != nullptr);
    m_trackCombo->setEnabled(enabled && selectedClip() != nullptr);
    m_timeline->setEnabled(enabled);
    m_toStartButton->setEnabled(enabled);
    m_playPauseButton->setEnabled(enabled);
    m_exportButton->setEnabled(enabled);
}

ClipItem *MainWindow::selectedClip()
{
    if (m_selectedTimelineClipIndex < 0 || m_selectedTimelineClipIndex >= m_clips.size()) {
        return nullptr;
    }
    return &m_clips[m_selectedTimelineClipIndex];
}
