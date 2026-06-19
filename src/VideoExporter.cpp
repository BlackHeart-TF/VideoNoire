#include "VideoExporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardError, this, &VideoExporter::handleReadyRead);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &VideoExporter::handleReadyRead);
    connect(&m_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &VideoExporter::handleProcessFinished);
}

bool VideoExporter::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void VideoExporter::exportProject(const QList<ClipItem> &clips, const QString &outputPath)
{
    if (isRunning()) {
        emit finished(false, QStringLiteral("An export is already running."));
        return;
    }

    QList<ClipItem> videoClips;
    QList<ClipItem> audioClips;
    for (const ClipItem &clip : clips) {
        if (clip.mediaType == ClipMediaType::Video) {
            videoClips.append(clip);
        } else {
            audioClips.append(clip);
        }
    }

    if (videoClips.isEmpty()) {
        emit finished(false, QStringLiteral("Add at least one video clip before exporting."));
        return;
    }

    m_commands.clear();
    m_trimmedClipPaths.clear();
    m_finalOutputPath = outputPath;
    m_tempDir = std::make_unique<QTemporaryDir>();

    if (!m_tempDir->isValid()) {
        emit finished(false, QStringLiteral("Could not create a temporary export folder."));
        return;
    }

    std::sort(videoClips.begin(), videoClips.end(), [](const ClipItem &left, const ClipItem &right) {
        if (left.timelineStartSeconds != right.timelineStartSeconds) {
            return left.timelineStartSeconds < right.timelineStartSeconds;
        }
        return left.trackIndex < right.trackIndex;
    });

    double projectDurationSeconds = 0.0;
    for (const ClipItem &clip : clips) {
        projectDurationSeconds = qMax(projectDurationSeconds, clip.timelineStartSeconds + clip.timelineDurationSeconds());
    }

    enqueueTrimCommands(videoClips);
    if (!enqueueConcatCommand()) {
        return;
    }
    enqueueAudioOverlayCommand(audioClips, outputPath, projectDurationSeconds);
    startNextCommand();
}

void VideoExporter::enqueueTrimCommands(const QList<ClipItem> &clips)
{
    for (int i = 0; i < clips.size(); ++i) {
        const ClipItem &clip = clips.at(i);
        const QString outPath = QDir(m_tempDir->path()).filePath(QStringLiteral("clip_%1.mp4").arg(i, 4, 10, QChar('0')));
        m_trimmedClipPaths.append(outPath);

        QStringList args;
        args << QStringLiteral("-y");
        if (clip.startSeconds > 0.0) {
            args << QStringLiteral("-ss") << QString::number(clip.startSeconds, 'f', 3);
        }
        if (clip.endSeconds > 0.0 && clip.endSeconds > clip.startSeconds) {
            args << QStringLiteral("-to") << QString::number(clip.endSeconds, 'f', 3);
        }
        args << QStringLiteral("-i") << clip.filePath;
        args << QStringLiteral("-vf") << QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2,setsar=1");
        args << QStringLiteral("-c:v") << QStringLiteral("libx264");
        args << QStringLiteral("-preset") << QStringLiteral("veryfast");
        args << QStringLiteral("-crf") << QStringLiteral("20");
        args << QStringLiteral("-c:a") << QStringLiteral("aac");
        args << QStringLiteral("-ar") << QStringLiteral("48000");
        args << QStringLiteral("-ac") << QStringLiteral("2");
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        args << outPath;

        m_commands.enqueue({ QStringLiteral("ffmpeg"), args, QStringLiteral("Rendering clip %1 of %2").arg(i + 1).arg(clips.size()) });
    }
}

bool VideoExporter::enqueueConcatCommand()
{
    const QString listPath = QDir(m_tempDir->path()).filePath(QStringLiteral("concat.txt"));
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fail(QStringLiteral("Could not create FFmpeg concat list."));
        return false;
    }

    QTextStream stream(&listFile);
    for (const QString &clipPath : m_trimmedClipPaths) {
        QString escapedPath = clipPath;
        escapedPath.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        stream << "file '" << escapedPath << "'\n";
    }

    const QString joinedPath = QDir(m_tempDir->path()).filePath(QStringLiteral("joined.mp4"));
    QStringList args;
    args << QStringLiteral("-y");
    args << QStringLiteral("-f") << QStringLiteral("concat");
    args << QStringLiteral("-safe") << QStringLiteral("0");
    args << QStringLiteral("-i") << listPath;
    args << QStringLiteral("-c") << QStringLiteral("copy");
    args << joinedPath;

    m_commands.enqueue({ QStringLiteral("ffmpeg"), args, QStringLiteral("Joining clips") });
    return true;
}

void VideoExporter::enqueueAudioOverlayCommand(const QList<ClipItem> &audioClips, const QString &outputPath, double projectDurationSeconds)
{
    const QString joinedPath = QDir(m_tempDir->path()).filePath(QStringLiteral("joined.mp4"));

    if (audioClips.isEmpty()) {
        QStringList args;
        args << QStringLiteral("-y") << QStringLiteral("-i") << joinedPath;
        args << QStringLiteral("-c") << QStringLiteral("copy");
        args << outputPath;
        m_commands.enqueue({ QStringLiteral("ffmpeg"), args, QStringLiteral("Writing final video") });
        return;
    }

    const QString audioBedPath = QDir(m_tempDir->path()).filePath(QStringLiteral("timeline_audio.wav"));

    QStringList audioArgs;
    audioArgs << QStringLiteral("-y");

    for (const ClipItem &audioClip : audioClips) {
        audioArgs << QStringLiteral("-i") << audioClip.filePath;
    }

    const double audioDurationSeconds = qMax(0.1, projectDurationSeconds);
    audioArgs << QStringLiteral("-f") << QStringLiteral("lavfi");
    audioArgs << QStringLiteral("-t") << QString::number(audioDurationSeconds, 'f', 3);
    audioArgs << QStringLiteral("-i") << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000");

    QStringList filterParts;
    QStringList mixInputs;
    mixInputs << QStringLiteral("[silent]");

    for (int i = 0; i < audioClips.size(); ++i) {
        const ClipItem &audioClip = audioClips.at(i);
        const int inputIndex = i;
        const int delayMs = qMax(0, static_cast<int>(audioClip.timelineStartSeconds * 1000.0));
        QString trim = QStringLiteral("atrim=start=%1").arg(audioClip.startSeconds, 0, 'f', 3);
        if (audioClip.endSeconds > audioClip.startSeconds) {
            trim += QStringLiteral(":end=%1").arg(audioClip.endSeconds, 0, 'f', 3);
        }

        filterParts << QStringLiteral("[%1:a]%2,asetpts=PTS-STARTPTS,adelay=%3|%3[a%4]")
                           .arg(inputIndex)
                           .arg(trim)
                           .arg(delayMs)
                           .arg(inputIndex);
        mixInputs << QStringLiteral("[a%1]").arg(inputIndex);
    }

    const int silentInputIndex = audioClips.size();
    filterParts << QStringLiteral("[%1:a]atrim=duration=%2,asetpts=PTS-STARTPTS[silent]")
                       .arg(silentInputIndex)
                       .arg(audioDurationSeconds, 0, 'f', 3);

    const QString filter = QStringLiteral("%1;%2amix=inputs=%3:duration=longest:dropout_transition=2[aout]")
                               .arg(filterParts.join(QStringLiteral(";")))
                               .arg(mixInputs.join(QString()))
                               .arg(mixInputs.size());

    audioArgs << QStringLiteral("-filter_complex") << filter;
    audioArgs << QStringLiteral("-map") << QStringLiteral("[aout]");
    audioArgs << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le");
    audioArgs << audioBedPath;
    m_commands.enqueue({ QStringLiteral("ffmpeg"), audioArgs, QStringLiteral("Rendering timeline audio") });

    QStringList muxArgs;
    muxArgs << QStringLiteral("-y");
    muxArgs << QStringLiteral("-i") << joinedPath;
    muxArgs << QStringLiteral("-i") << audioBedPath;
    muxArgs << QStringLiteral("-map") << QStringLiteral("0:v:0");
    muxArgs << QStringLiteral("-map") << QStringLiteral("1:a:0");
    muxArgs << QStringLiteral("-c:v") << QStringLiteral("copy");
    muxArgs << QStringLiteral("-c:a") << QStringLiteral("aac");
    muxArgs << QStringLiteral("-shortest");
    muxArgs << outputPath;
    m_commands.enqueue({ QStringLiteral("ffmpeg"), muxArgs, QStringLiteral("Muxing final audio and video") });
}

void VideoExporter::startNextCommand()
{
    if (m_commands.isEmpty()) {
        m_tempDir.reset();
        emit finished(true, QStringLiteral("Export complete: %1").arg(QFileInfo(m_finalOutputPath).absoluteFilePath()));
        return;
    }

    const Command command = m_commands.dequeue();
    emit logMessage(command.description);
    m_process.start(command.program, command.arguments);
}

void VideoExporter::handleReadyRead()
{
    const QString output = QString::fromLocal8Bit(m_process.readAllStandardOutput() + m_process.readAllStandardError()).trimmed();
    if (!output.isEmpty()) {
        emit logMessage(output);
    }
}

void VideoExporter::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        fail(QStringLiteral("FFmpeg failed. Make sure ffmpeg is installed and available in PATH."));
        return;
    }

    startNextCommand();
}

void VideoExporter::fail(const QString &message)
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }

    m_commands.clear();
    m_trimmedClipPaths.clear();
    m_tempDir.reset();
    emit finished(false, message);
}
