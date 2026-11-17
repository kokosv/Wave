#include "audiotreewidget.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <functional>

namespace {
const QStringList kAudioExtensions = {"*.mp3", "*.wav", "*.flac"};

void collectAudioFiles(const QString &path, QList<QUrl> &urls)
{
    std::function<void(const QString &)> collect = [&](const QString &dirPath) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList(kAudioExtensions, QDir::Files);
        for (const QFileInfo &file : files) {
            urls << QUrl::fromLocalFile(file.absoluteFilePath());
        }
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &subDir : dirs) {
            collect(subDir.absoluteFilePath());
        }
    };
    collect(path);
}
}

AudioTreeWidget::AudioTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
}

QMimeData *AudioTreeWidget::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    QList<QUrl> urls;
    for (QTreeWidgetItem *item : items) {
        QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) continue;

        QFileInfo info(path);
        if (info.isDir()) {
            collectAudioFiles(path, urls);
        } else {
            urls << QUrl::fromLocalFile(path);
        }
    }

    QMimeData *data = new QMimeData();
    data->setUrls(urls);
    return data;
}
