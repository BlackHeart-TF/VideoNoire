#pragma once

#include "PreviewEngine.h"

#include <QTimer>

#include <gst/gst.h>

class GstPreviewEngine : public PreviewEngine {
    Q_OBJECT

public:
    explicit GstPreviewEngine(QObject *parent = nullptr);
    ~GstPreviewEngine() override;

    void setVideoOutput(QWidget *widget) override;
    void setSource(const QString &filePath) override;
    QString source() const override;
    bool hasSource() const override;
    void play() override;
    void pause() override;
    void stop() override;
    bool isPlaying() const override;
    void setPosition(qint64 positionMilliseconds) override;
    qint64 position() const override;

private slots:
    void pollState();

private:
    static GstBusSyncReply handleBusMessage(GstBus *bus, GstMessage *message, gpointer userData);

    void ensurePipeline();
    void attachVideoOverlay(GstMessage *message);
    void emitErrorFromMessage(GstMessage *message);

    GstElement *m_pipeline = nullptr;
    QWidget *m_videoOutput = nullptr;
    QTimer m_pollTimer;
    QString m_sourcePath;
    qint64 m_lastDuration = -1;
};
