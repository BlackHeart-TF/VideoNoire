#pragma once

#include "ClipItem.h"

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTemporaryDir>

#include <memory>

class VideoExporter : public QObject {
    Q_OBJECT

public:
    explicit VideoExporter(QObject *parent = nullptr);

    bool isRunning() const;
    void exportProject(const QList<ClipItem> &clips, const QString &outputPath);

signals:
    void logMessage(const QString &message);
    void finished(bool ok, const QString &message);

private slots:
    void handleReadyRead();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct Command {
        QString program;
        QStringList arguments;
        QString description;
    };

    void enqueueTrimCommands(const QList<ClipItem> &clips);
    bool enqueueConcatCommand();
    void enqueueAudioOverlayCommand(const QList<ClipItem> &audioClips, const QString &outputPath, double projectDurationSeconds);
    void startNextCommand();
    void fail(const QString &message);

    QProcess m_process;
    QQueue<Command> m_commands;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QStringList m_trimmedClipPaths;
    QString m_finalOutputPath;
};
