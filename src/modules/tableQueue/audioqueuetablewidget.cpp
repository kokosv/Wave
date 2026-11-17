#include "audioqueuetablewidget.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>

namespace {
bool isAudioFile(const QString &path)
{
    static const QStringList extensions = {"mp3", "wav", "flac"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
}

AudioQueueTableWidget::AudioQueueTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
}

void AudioQueueTableWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTableWidget::dragEnterEvent(event);
    }
}

void AudioQueueTableWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTableWidget::dragMoveEvent(event);
    }
}

void AudioQueueTableWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        QTableWidget::dropEvent(event);
        return;
    }

    QStringList filePaths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        if (isAudioFile(path)) {
            filePaths << path;
        }
    }

    if (!filePaths.isEmpty()) {
        emit queueFilesDropped(filePaths);
    }
    event->acceptProposedAction();
}
