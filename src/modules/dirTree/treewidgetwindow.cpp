#include "treewidgetwindow.h"
#include <QFileDialog>
#include <QMenu>
#include <QDir>
#include <QStyle>
#include <qevent.h>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>

TreeWidgetWindow::TreeWidgetWindow(QTreeWidget *treeWidget, QObject *parent)
    : QObject(parent), m_treeWidget(treeWidget), m_lastOpenedDir(), m_metadataCache(1000)
{
    m_treeWidget->setHeaderLabel("Audio Browser");
    m_treeWidget->setColumnCount(1);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &TreeWidgetWindow::onCustomContextMenuRequested);

    m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Restore state immediately after construction
    restoreState();
}

void TreeWidgetWindow::clearAllDirectories()
{
    m_treeWidget->clear();
}

bool TreeWidgetWindow::isAudioFile(const QString &path) const
{
    return path.endsWith(".mp3", Qt::CaseInsensitive) ||
           path.endsWith(".wav", Qt::CaseInsensitive) ||
           path.endsWith(".flac", Qt::CaseInsensitive);
}

void TreeWidgetWindow::onCustomContextMenuRequested(const QPoint &pos)
{
    QMenu contextMenu(m_treeWidget->viewport());

    QTreeWidgetItem *clickedItem = m_treeWidget->itemAt(pos);
    QList<QTreeWidgetItem*> selectedItems = m_treeWidget->selectedItems();
    if (clickedItem && !selectedItems.contains(clickedItem)) {
        selectedItems = {clickedItem};
    }

    QStringList audioFiles;
    QStringList directories;
    for (QTreeWidgetItem *item : selectedItems) {
        QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) continue;

        QFileInfo info(path);
        if (info.isDir()) {
            directories << path;
        } else if (isAudioFile(path)) {
            audioFiles << path;
        }
    }

    QAction *addToQueueAction = contextMenu.addAction(
        audioFiles.size() > 1 ? QString("Add %1 songs to queue").arg(audioFiles.size())
                               : "Add to queue");
    QAction *addFolderToQueueAction = contextMenu.addAction(
        directories.size() > 1 ? "Add folders to queue" : "Add folder to queue");
    contextMenu.addSeparator();
    QAction *openDirAction = contextMenu.addAction("Open Directory...");
    QAction *refreshAction = contextMenu.addAction("Refresh");

    addToQueueAction->setEnabled(!audioFiles.isEmpty());
    addFolderToQueueAction->setEnabled(!directories.isEmpty());
    refreshAction->setEnabled(!m_lastOpenedDir.isEmpty());

    connect(addToQueueAction, &QAction::triggered, this, [this, audioFiles]() {
        for (const QString &filePath : audioFiles) {
            emit fileAddRequested(filePath);
        }
    });

    connect(addFolderToQueueAction, &QAction::triggered, this, [this, directories]() {
        for (const QString &dirPath : directories) {
            QList<AudioMetadata> files;
            collectAudioFilesUnder(dirPath, files);
            std::sort(files.begin(), files.end(), [](const AudioMetadata &a, const AudioMetadata &b) {
                return a.filePath.compare(b.filePath, Qt::CaseInsensitive) < 0;
            });
            for (const AudioMetadata &meta : files) {
                emit fileAddRequested(meta.filePath);
            }
        }
    });

    connect(openDirAction, &QAction::triggered, this, [this, pos]() {
        addDirectoryToTree(pos);
    });

    connect(refreshAction, &QAction::triggered, this, &TreeWidgetWindow::refreshTree);

    contextMenu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
}

void TreeWidgetWindow::refreshTree()
{
    if (m_lastOpenedDir.isEmpty() || !QDir(m_lastOpenedDir).exists()) return;

    QSet<QString> expandedPaths = captureExpandedPaths();

    clearAllDirectories();
    m_metadataCache.clear();

    QTreeWidgetItem* newItem = new QTreeWidgetItem(m_treeWidget);
    newItem->setText(0, QDir(m_lastOpenedDir).dirName());
    newItem->setData(0, Qt::UserRole, m_lastOpenedDir);
    newItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

    scanDirectory(newItem, m_lastOpenedDir);
    applyExpandedPaths(expandedPaths);

    emit directoryAdded();
}

QSet<QString> TreeWidgetWindow::captureExpandedPaths() const
{
    QSet<QString> expandedPaths;
    QTreeWidgetItemIterator it(m_treeWidget);
    while (*it) {
        if ((*it)->isExpanded() && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
            expandedPaths << (*it)->data(0, Qt::UserRole).toString();
        }
        ++it;
    }
    return expandedPaths;
}

void TreeWidgetWindow::applyExpandedPaths(const QSet<QString> &expandedPaths)
{
    QTreeWidgetItemIterator it(m_treeWidget);
    while (*it) {
        QString itemPath = (*it)->data(0, Qt::UserRole).toString();
        if (expandedPaths.contains(itemPath)) {
            (*it)->setExpanded(true);
        }
        ++it;
    }
}

void TreeWidgetWindow::collectAudioFilesUnder(const QString &path, QList<AudioMetadata> &results)
{
    std::function<void(const QString&)> collector = [&](const QString &dirPath) {
        QDir dir(dirPath);

        const QFileInfoList files = dir.entryInfoList(audioExtensions, QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            AudioMetadata* meta = m_metadataCache.object(fileInfo.absoluteFilePath());
            if (!meta) {
                meta = new AudioMetadata(m_metadataReader.readMetadata(fileInfo.absoluteFilePath()));
                m_metadataCache.insert(fileInfo.absoluteFilePath(), meta);
            }
            results.append(*meta);
        }

        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &dirInfo : dirs) {
            collector(dirInfo.absoluteFilePath());
        }
    };

    collector(path);
}

void TreeWidgetWindow::addDirectoryToTree(const QPoint &pos)
{
    QTreeWidgetItem *clickedItem = m_treeWidget->itemAt(pos);
    QString startDir = clickedItem ? clickedItem->data(0, Qt::UserRole).toString() : m_lastOpenedDir;

    QString dir = QFileDialog::getExistingDirectory(
        m_treeWidget,
        "Select Audio Directory",
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (!dir.isEmpty()) {
        m_lastOpenedDir = dir; // Store the last opened directory
        clearAllDirectories();

        QTreeWidgetItem *newItem = new QTreeWidgetItem(m_treeWidget);
        newItem->setText(0, QDir(dir).dirName());
        newItem->setData(0, Qt::UserRole, dir);
        newItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

        scanDirectory(newItem, dir);
        emit directoryAdded();
    }
}

void TreeWidgetWindow::scanDirectory(QTreeWidgetItem* parentItem, const QString& path)
{
    QDir dir(path);
    const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& dirInfo : dirs) {
        QTreeWidgetItem* dirItem = new QTreeWidgetItem(parentItem);
        dirItem->setText(0, dirInfo.fileName());
        dirItem->setData(0, Qt::UserRole, dirInfo.absoluteFilePath());
        dirItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));
        scanDirectory(dirItem, dirInfo.absoluteFilePath());
    }
    addAudioFilesToTree(parentItem, path);
}

void TreeWidgetWindow::addAudioFilesToTree(QTreeWidgetItem *parent, const QString &dirPath)
{
    QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList(audioExtensions, QDir::Files);

    for (const QFileInfo &fileInfo : std::as_const(files)) {
        QTreeWidgetItem *fileItem = new QTreeWidgetItem(parent);
        fileItem->setText(0, fileInfo.fileName());
        fileItem->setData(0, Qt::UserRole, fileInfo.absoluteFilePath());
        fileItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_FileIcon));
    }
}

QString TreeWidgetWindow::getConfigPath() const
{
    // First try application directory
    QString appDirPath = QCoreApplication::applicationDirPath();
    QFileInfo appDir(appDirPath);

    // Check if we can write to application directory
    if (appDir.isWritable()) {
        return appDirPath + "/config.ini";
    }

    // Fall back to AppData location if application directory isn't writable
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/" + QCoreApplication::applicationName() + "/config.ini";
}

void TreeWidgetWindow::saveState()
{
    QString configPath = getConfigPath();
    QFileInfo configFile(configPath);
    QDir().mkpath(configFile.absolutePath());

    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("lastOpenedDir", m_lastOpenedDir);
    settings.setValue("expandedPaths", QStringList(captureExpandedPaths().values()));
}

void TreeWidgetWindow::restoreState()
{
    QString configPath = getConfigPath();
    if (!QFile::exists(configPath)) return;

    QSettings settings(configPath, QSettings::IniFormat);
    m_lastOpenedDir = settings.value("lastOpenedDir", "").toString();

    if (!m_lastOpenedDir.isEmpty() && QDir(m_lastOpenedDir).exists()) {
        clearAllDirectories();

        QTreeWidgetItem* newItem = new QTreeWidgetItem(m_treeWidget);
        newItem->setText(0, QDir(m_lastOpenedDir).dirName());
        newItem->setData(0, Qt::UserRole, m_lastOpenedDir);
        newItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

        scanDirectory(newItem, m_lastOpenedDir);

        // Restore expanded state after directory is loaded
        QStringList expandedPaths = settings.value("expandedPaths").toStringList();
        applyExpandedPaths(QSet<QString>(expandedPaths.begin(), expandedPaths.end()));

        emit directoryAdded();
    }
}

bool TreeWidgetWindow::matchesSearch(const QFileInfo &fileInfo,
                                     const QString &filter, const QString &searchText)
{
    if (searchText.isEmpty()) return false;

    QString lowerSearch = searchText.toLower();
    QString filePath = fileInfo.absoluteFilePath();
    AudioMetadata* meta = m_metadataCache.object(filePath);

    if (!meta) {
        // Not in cache, read and store
        meta = new AudioMetadata(m_metadataReader.readMetadata(filePath));
        m_metadataCache.insert(filePath, meta);
    }

    if (filter == "File Names") {
        return fileInfo.fileName().toLower().contains(lowerSearch);
    }
    else if (filter == "Artist") {
        return meta->artist.toLower().contains(lowerSearch);
    }
    else if (filter == "Album") {
        return meta->album.toLower().contains(lowerSearch);
    }
    else if (filter == "Title") {
        return meta->title.toLower().contains(lowerSearch);
    }
    else if (filter == "Genre") {
        return meta->genre.toLower().contains(lowerSearch);
    }

    return false;
}

void TreeWidgetWindow::searchFiles(const QString &filter, const QString &searchText)
{
    if (m_lastOpenedDir.isEmpty()) return;

    QList<AudioMetadata> matchingFiles;
    collectMatchingFiles(filter, searchText, matchingFiles);

    if (matchingFiles.isEmpty()) {
        clearAllDirectories();
        QTreeWidgetItem* noResults = new QTreeWidgetItem(m_treeWidget);
        noResults->setText(0, "No results found");
        noResults->setFlags(noResults->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    QMap<QString, QList<AudioMetadata>> groupedItems;
    groupFiles(matchingFiles, filter, groupedItems);
    buildGroupedView("Search Results", groupedItems);
}

void TreeWidgetWindow::buildGroupedView(const QString& title, const QMap<QString, QList<AudioMetadata>>& groupedItems)
{
    clearAllDirectories();

    QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_treeWidget);
    rootItem->setText(0, title);
    rootItem->setExpanded(true);

    for (auto it = groupedItems.constBegin(); it != groupedItems.constEnd(); ++it) {
        QTreeWidgetItem* groupItem = new QTreeWidgetItem(rootItem);
        groupItem->setText(0, it.key());
        groupItem->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_DirIcon));

        // Sort items by title
        QList<AudioMetadata> sortedItems = it.value();
        std::sort(sortedItems.begin(), sortedItems.end(),
                  [](const AudioMetadata &a, const AudioMetadata &b) {
                      return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
                  });

        // Add items to group
        for (const AudioMetadata& meta : sortedItems) {
            QTreeWidgetItem* item = new QTreeWidgetItem(groupItem);
            item->setText(0, meta.title);
            item->setData(0, Qt::UserRole, meta.filePath);
            item->setIcon(0, m_treeWidget->style()->standardIcon(QStyle::SP_FileIcon));
        }

        groupItem->setExpanded(true);
    }
}

void TreeWidgetWindow::groupFiles(const QList<AudioMetadata>& files, const QString& groupBy,
                                  QMap<QString, QList<AudioMetadata>>& result)
{
    for (const AudioMetadata& meta : files) {
        QString groupKey;
        if (groupBy == "Artist") groupKey = meta.artist.isEmpty() ? "Unknown Artist" : meta.artist;
        else if (groupBy == "Album") groupKey = meta.album.isEmpty() ? "Unknown Album" : meta.album;
        else if (groupBy == "Genre") groupKey = meta.genre.isEmpty() ? "Unknown Genre" : meta.genre;
        else groupKey = "Matching Files";

        result[groupKey].append(meta);
    }
}

void TreeWidgetWindow::collectAllAudioFiles(QList<AudioMetadata>& results)
{
    collectAudioFilesUnder(m_lastOpenedDir, results);
}

void TreeWidgetWindow::collectMatchingFiles(const QString &filter, const QString &searchText,
                                            QList<AudioMetadata>& results)
{
    std::function<void(const QString&)> collector = [&](const QString &path) {
        QDir dir(path);

        const QFileInfoList files = dir.entryInfoList(audioExtensions, QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            AudioMetadata* meta = m_metadataCache.object(fileInfo.absoluteFilePath());
            if (!meta) {
                meta = new AudioMetadata(m_metadataReader.readMetadata(fileInfo.absoluteFilePath()));
                m_metadataCache.insert(fileInfo.absoluteFilePath(), meta);
            }

            if (matchesSearch(fileInfo, filter, searchText)) {
                results.append(*meta);
            }
        }

        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &dirInfo : dirs) {
            collector(dirInfo.absoluteFilePath());
        }
    };

    collector(m_lastOpenedDir);
}


