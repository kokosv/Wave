#include "waveformprogressbar.h"
#include <QPainter>
#include <QStyleOptionProgressBar>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QDebug>

WaveformProgressBar::WaveformProgressBar(QWidget *parent)
    : QProgressBar(parent), m_decoder(nullptr)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setValue(0);
}

WaveformProgressBar::~WaveformProgressBar()
{
    stopAndClearDecoder(); // Ensure decoder is cleaned up on destruction
}

void WaveformProgressBar::setAudioFile(const QString &filePath)
{
    if (m_audioFilePath == filePath && !m_waveformData.isEmpty()) {
        return; // Avoid reloading the same file if waveform is already loaded
    }

    m_audioFilePath = filePath;
    stopAndClearDecoder(); // Stop any ongoing decoding and clear data
    m_waveformData.clear();
    m_tempSamples.clear();
    m_pendingWaveform.clear();
    m_isLoading = false;
    update(); // Redraw to show placeholder line
    if (!filePath.isEmpty()) {
        loadWaveformData();
    }
}

void WaveformProgressBar::loadWaveformData()
{
    if (m_audioFilePath.isEmpty()) return;

    // Stop any existing decoder before creating a new one
    stopAndClearDecoder();

    m_decoder = new QAudioDecoder(this);

    // Without an explicit format, the decoder picks whatever the file's
    // native format happens to be (often 32-bit float) while the buffer
    // reader below assumed 16-bit integer samples - garbage amplitudes in,
    // garbage (or an empty-looking) waveform out. Pin it down explicitly.
    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Int16);
    m_decoder->setAudioFormat(format);

    m_decoder->setSource(QUrl::fromLocalFile(m_audioFilePath));

    m_pendingWaveform.clear();
    m_isLoading = true;

    connect(m_decoder, &QAudioDecoder::bufferReady, this, [=]() {
        QAudioBuffer buffer = m_decoder->read();
        if (!buffer.isValid()) return;

        const qint16 *data = buffer.constData<qint16>();
        int sampleCount = buffer.sampleCount();

        for (int i = 0; i < sampleCount; ++i) {
            m_pendingWaveform.append(data[i] / 32768.0f);
        }
    });

    connect(m_decoder, &QAudioDecoder::finished, this, [=]() {
        QVector<float> finalData;
        int targetSamples = qMin(1000, qMax(1, width()));

        m_tempSamples = m_pendingWaveform;

        if (!m_tempSamples.isEmpty()) {
            for (int i = 0; i < targetSamples; ++i) {
                int srcIndex = (i * m_tempSamples.size()) / targetSamples;
                finalData.append(qBound(-1.0f, m_tempSamples.value(srcIndex), 1.0f));
            }
        }

        m_waveformData = finalData;
        m_pendingWaveform.clear();
        m_isLoading = false;
        update();
        emit waveformReady();
        m_decoder->deleteLater();
        m_decoder = nullptr; // Clear pointer after decoding
    });

    connect(m_decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this, [=](QAudioDecoder::Error error) {
        qDebug() << "Waveform decode failed for" << m_audioFilePath << ":" << error << m_decoder->errorString();
        m_pendingWaveform.clear();
        m_isLoading = false;
        update();
        if (m_decoder) {
            m_decoder->deleteLater();
            m_decoder = nullptr;
        }
    });

    m_decoder->start();
}

void WaveformProgressBar::stopAndClearDecoder()
{
    if (m_decoder) {
        // Disconnect signals to prevent further processing
        disconnect(m_decoder, nullptr, this, nullptr);
        // Stop decoding if it's running
        if (m_decoder->isDecoding()) {
            m_decoder->stop();
        }
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
}

void WaveformProgressBar::regenerateWaveform()
{
    m_waveformData.clear();
    int targetSamples = qMin(1000, width());
    if (!m_tempSamples.isEmpty()) {
        for (int i = 0; i < targetSamples; ++i) {
            int srcIndex = (i * m_tempSamples.size()) / targetSamples;
            m_waveformData.append(qBound(-1.0f, m_tempSamples[srcIndex], 1.0f));
        }
    }
    update();
}

void WaveformProgressBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        int newValue = minimum() + (maximum() - minimum()) * event->position().x() / width();
        setValue(newValue);
        emit userSeeked(newValue);
    }
    QProgressBar::mousePressEvent(event);
}

void WaveformProgressBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        int newValue = minimum() + (maximum() - minimum()) * event->position().x() / width();
        setValue(newValue);
        emit userSeeked(newValue);
    }
    QProgressBar::mouseMoveEvent(event);
}

void WaveformProgressBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QProgressBar::mouseReleaseEvent(event);
}

void WaveformProgressBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int range = maximum() - minimum();
    float progress = range > 0 ? (value() - minimum()) / static_cast<float>(range) : 0.0f;
    int progressWidth = static_cast<int>(w * progress);

    QColor bgColor("#5A5673");   // unplayed track
    QColor fgColor("#BD93F9");   // played portion
    QColor waveUnplayed("#938A93");
    QColor wavePlayed("#2E2A3A");

    // The colored fill always reflects play position, even if the waveform
    // itself hasn't (or can't) finish decoding - the progress bar is never
    // just a flat, uninformative line.
    painter.fillRect(QRect(0, 0, w, h), bgColor);
    if (progressWidth > 0) {
        painter.fillRect(QRect(0, 0, progressWidth, h), fgColor);
    }

    if (!m_waveformData.isEmpty() && !m_isLoading) {
        // The waveform itself *is* the progress bar's surface: its bars are
        // drawn on top of the fill above, using a darker stroke over the
        // played (purple) region so it stays visible over both colors.
        for (int i = 0; i < m_waveformData.size(); ++i) {
            int x = (i * w) / m_waveformData.size();
            float amplitude = m_waveformData[i];
            int lineHeight = static_cast<int>(amplitude * h / 2);
            painter.setPen(QPen(x <= progressWidth ? wavePlayed : waveUnplayed, 1));
            painter.drawLine(x, h / 2, x, h / 2 + lineHeight);
            painter.drawLine(x, h / 2, x, h / 2 - lineHeight);
        }
    } else {
        // Placeholder center line while the waveform is still decoding (or
        // failed to decode) - the colored progress fill above still works.
        painter.setPen(QPen(Qt::white, 1));
        int centerY = height() / 2;
        painter.drawLine(0, centerY, width(), centerY);
    }
}

void WaveformProgressBar::resizeEvent(QResizeEvent *event)
{
    QProgressBar::resizeEvent(event);
    regenerateWaveform(); // Use existing samples instead of re-decoding
}

