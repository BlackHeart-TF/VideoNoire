#pragma once

#include <QObject>
#include <QString>

class QWidget;

class PreviewEngine : public QObject {
    Q_OBJECT

public:
    explicit PreviewEngine(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~PreviewEngine() override = default;

    virtual void setVideoOutput(QWidget *widget) = 0;
    virtual void setSource(const QString &filePath) = 0;
    virtual QString source() const = 0;
    virtual bool hasSource() const = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool isPlaying() const = 0;
    virtual void setPosition(qint64 positionMilliseconds) = 0;
    virtual qint64 position() const = 0;

signals:
    void durationChanged(qint64 durationMilliseconds);
    void positionChanged(qint64 positionMilliseconds);
    void errorOccurred(const QString &message);
};
