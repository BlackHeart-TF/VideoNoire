#include "GstPreviewEngine.h"

#include <QCoreApplication>
#include <QUrl>
#include <QWidget>

#include <gst/video/videooverlay.h>

GstPreviewEngine::GstPreviewEngine(QObject *parent)
    : PreviewEngine(parent)
{
    static bool gstInitialized = false;
    if (!gstInitialized) {
        int argc = 0;
        char **argv = nullptr;
        gst_init(&argc, &argv);
        gstInitialized = true;
    }

    connect(&m_pollTimer, &QTimer::timeout, this, &GstPreviewEngine::pollState);
    m_pollTimer.start(50);
    ensurePipeline();
}

GstPreviewEngine::~GstPreviewEngine()
{
    if (m_pipeline != nullptr) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        GstBus *bus = gst_element_get_bus(m_pipeline);
        if (bus != nullptr) {
            gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
            gst_object_unref(bus);
        }
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

void GstPreviewEngine::setVideoOutput(QWidget *widget)
{
    m_videoOutput = widget;
    if (m_videoOutput != nullptr) {
        m_videoOutput->setAttribute(Qt::WA_NativeWindow, true);
        m_videoOutput->winId();
    }
}

void GstPreviewEngine::setSource(const QString &filePath)
{
    ensurePipeline();
    if (m_pipeline == nullptr || m_sourcePath == filePath) {
        return;
    }

    m_sourcePath = filePath;
    m_lastDuration = -1;

    gst_element_set_state(m_pipeline, GST_STATE_READY);
    const QByteArray uri = QUrl::fromLocalFile(filePath).toEncoded();
    g_object_set(G_OBJECT(m_pipeline), "uri", uri.constData(), nullptr);
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
}

QString GstPreviewEngine::source() const
{
    return m_sourcePath;
}

bool GstPreviewEngine::hasSource() const
{
    return !m_sourcePath.isEmpty();
}

void GstPreviewEngine::play()
{
    ensurePipeline();
    if (m_pipeline != nullptr && hasSource()) {
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    }
}

void GstPreviewEngine::pause()
{
    if (m_pipeline != nullptr) {
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    }
}

void GstPreviewEngine::stop()
{
    if (m_pipeline != nullptr) {
        gst_element_set_state(m_pipeline, GST_STATE_READY);
    }
}

bool GstPreviewEngine::isPlaying() const
{
    if (m_pipeline == nullptr) {
        return false;
    }

    GstState state = GST_STATE_NULL;
    gst_element_get_state(m_pipeline, &state, nullptr, 0);
    return state == GST_STATE_PLAYING;
}

void GstPreviewEngine::setPosition(qint64 positionMilliseconds)
{
    if (m_pipeline == nullptr || !hasSource()) {
        return;
    }

    const gint64 positionNanoseconds = static_cast<gint64>(positionMilliseconds) * GST_MSECOND;
    gst_element_seek_simple(
        m_pipeline,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        positionNanoseconds);
}

qint64 GstPreviewEngine::position() const
{
    if (m_pipeline == nullptr) {
        return 0;
    }

    gint64 positionNanoseconds = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &positionNanoseconds)) {
        return 0;
    }

    return static_cast<qint64>(positionNanoseconds / GST_MSECOND);
}

void GstPreviewEngine::pollState()
{
    if (m_pipeline == nullptr || !hasSource()) {
        return;
    }

    emit positionChanged(position());

    gint64 durationNanoseconds = 0;
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &durationNanoseconds)) {
        const qint64 durationMilliseconds = static_cast<qint64>(durationNanoseconds / GST_MSECOND);
        if (durationMilliseconds > 0 && durationMilliseconds != m_lastDuration) {
            m_lastDuration = durationMilliseconds;
            emit durationChanged(durationMilliseconds);
        }
    }
}

GstBusSyncReply GstPreviewEngine::handleBusMessage(GstBus *bus, GstMessage *message, gpointer userData)
{
    Q_UNUSED(bus)

    auto *engine = static_cast<GstPreviewEngine *>(userData);
    if (engine == nullptr) {
        return GST_BUS_PASS;
    }

    if (gst_is_video_overlay_prepare_window_handle_message(message)) {
        engine->attachVideoOverlay(message);
        return GST_BUS_DROP;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        engine->emitErrorFromMessage(message);
    }

    return GST_BUS_PASS;
}

void GstPreviewEngine::ensurePipeline()
{
    if (m_pipeline != nullptr) {
        return;
    }

    m_pipeline = gst_element_factory_make("playbin", "preview-playbin");
    if (m_pipeline == nullptr) {
        emit errorOccurred(QStringLiteral("Could not create GStreamer playbin."));
        return;
    }

    GstElement *videoSink = gst_element_factory_make("autovideosink", "preview-video-sink");
    if (videoSink != nullptr) {
        g_object_set(G_OBJECT(m_pipeline), "video-sink", videoSink, nullptr);
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    if (bus != nullptr) {
        gst_bus_set_sync_handler(bus, &GstPreviewEngine::handleBusMessage, this, nullptr);
        gst_object_unref(bus);
    }
}

void GstPreviewEngine::attachVideoOverlay(GstMessage *message)
{
    if (m_videoOutput == nullptr) {
        return;
    }

    const WId windowId = m_videoOutput->winId();
    auto *overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message));
    gst_video_overlay_set_window_handle(overlay, static_cast<guintptr>(windowId));
    gst_video_overlay_handle_events(overlay, true);
}

void GstPreviewEngine::emitErrorFromMessage(GstMessage *message)
{
    GError *error = nullptr;
    gchar *debugInfo = nullptr;
    gst_message_parse_error(message, &error, &debugInfo);

    const QString errorText = error != nullptr
        ? QString::fromUtf8(error->message)
        : QStringLiteral("Unknown GStreamer error.");

    QMetaObject::invokeMethod(this, [this, errorText]() {
        emit errorOccurred(errorText);
    }, Qt::QueuedConnection);

    if (error != nullptr) {
        g_error_free(error);
    }
    g_free(debugInfo);
}
