#include "audioqueuetable.h"
#include "audioqueuetablewidget.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QFileInfo>
#include <QHeaderView>
#include <QMimeData>
#include <QMessageBox>
#include <QTimer>
#include <QEvent>
#include <QResizeEvent>
#include <QSet>
#include <algorithm>  // For std::shuffle
#include <random>     // For random engine

namespace {
// Name / Artist / Album share the bulk of the width equally, Genre and
// Duration get smaller fixed shares. Applied to columns 1..5; column 0
// (the queue position number) gets its own fixed width below.
constexpr double kColumnPercentages[5] = {0.25, 0.25, 0.25, 0.15, 0.10};
constexpr int kMinColumnWidth = 40;
constexpr int kIndexColumnWidth = 36;
}

AudioQueueTable::AudioQueueTable(QTableWidget *tableWidget,
                                 AudioPlayer* audioPlayer,
                                 WaveformProgressBar* bar,
                                 PlayButton* button,
                                 QLabel* metadataLabel,
                                 QLabel* coverArtLabel,
                                 QObject *parent)
    : QObject(parent),
    m_tableWidget(tableWidget),
    currentSongIndex(-1),
    m_metadataLabel(metadataLabel),
    m_coverArtLabel(coverArtLabel),
    player(audioPlayer),
    progressBar(bar),
    playButton(button)
{
    m_tableWidget->setColumnCount(6);
    m_tableWidget->setHorizontalHeaderLabels({"#", "Name", "Artist", "Album", "Genre", "Duration"});
    m_tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_tableWidget->horizontalHeader()->setStretchLastSection(false);
    m_tableWidget->verticalHeader()->setStretchLastSection(false);
    m_tableWidget->verticalHeader()->setVisible(false); // temp solution to a style problem
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Give every column a sensible starting width once the widget has a real size.
    QTimer::singleShot(0, this, [this]() { applyColumnWidthPercentages(); });

    // Install a resize watcher on the table so columns keep their proportions
    // when the window/splitter is resized, instead of the last column
    // silently growing past the table's right edge and underlapping the
    // metadata panel next to it.
    m_tableWidget->installEventFilter(this);

    // Dragging a column border only ever resizes its *neighbor* in the
    // opposite direction, so the total width is always exactly the table's
    // width - a hard border, no column can spill past the table's edge.
    connect(m_tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int oldSize, int newSize) {
                if (m_resizingColumns) return;
                if (logicalIndex < 1 || logicalIndex + 1 >= m_tableWidget->columnCount()) return;

                QHeaderView *header = m_tableWidget->horizontalHeader();
                int delta = newSize - oldSize;
                int neighbor = logicalIndex + 1;
                int neighborNewSize = qMax(kMinColumnWidth, header->sectionSize(neighbor) - delta);

                m_resizingColumns = true;
                header->resizeSection(neighbor, neighborNewSize);
                m_resizingColumns = false;
            });

    // Metadata label setup
    if (m_metadataLabel) {
        m_metadataLabel->setTextFormat(Qt::RichText);
        m_metadataLabel->setWordWrap(true);
        m_metadataLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    // Connect double-click signal
    connect(m_tableWidget, &QTableWidget::itemDoubleClicked,
            this, &AudioQueueTable::handleItemDoubleClick);

    // Connect for end of song signal
    connect(player->getPlayer(), &QMediaPlayer::mediaStatusChanged,
            this, &AudioQueueTable::onMediaStatusChanged);

    // Drag-and-drop from the directory tree
    if (auto *dropTable = qobject_cast<AudioQueueTableWidget*>(tableWidget)) {
        connect(dropTable, &AudioQueueTableWidget::queueFilesDropped,
                this, &AudioQueueTable::handleFilesDropped);
    }

    resetPlayerUI();
}

bool AudioQueueTable::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tableWidget && event->type() == QEvent::Resize) {
        applyColumnWidthPercentages();
    }
    return QObject::eventFilter(watched, event);
}

void AudioQueueTable::applyColumnWidthPercentages()
{
    int totalWidth = m_tableWidget->viewport()->width();
    if (totalWidth <= 0) return;

    m_resizingColumns = true;
    m_tableWidget->setColumnWidth(0, kIndexColumnWidth);

    int remaining = qMax(0, totalWidth - kIndexColumnWidth);
    int assigned = 0;
    for (int col = 0; col < 5; ++col) {
        int width = (col == 4) ? (remaining - assigned)
                                : static_cast<int>(remaining * kColumnPercentages[col]);
        width = qMax(kMinColumnWidth, width);
        m_tableWidget->setColumnWidth(col + 1, width);
        assigned += width;
    }
    m_resizingColumns = false;
}

void AudioQueueTable::setDisplayWidgets(QLabel* metadataLabel, QLabel* coverArtLabel)
{
    m_metadataLabel = metadataLabel;
    m_coverArtLabel = coverArtLabel;

    if (m_metadataLabel) {
        m_metadataLabel->setTextFormat(Qt::RichText);
        m_metadataLabel->setWordWrap(true);
        m_metadataLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
}

void AudioQueueTable::handleItemDoubleClick(QTableWidgetItem* item)
{
    currentSongIndex = item->row();
    playCurrentIndex();
}

void AudioQueueTable::playCurrentIndex()
{
    if (isEmpty() || currentSongIndex < 0 || currentSongIndex >= m_fileQueue.size()) {
        return;
    }

    QString filePath = m_fileQueue.at(currentSongIndex);

    progressBar->setAudioFile(filePath);

    pendingFilePath = filePath;
    player->setCurrentSong(filePath);
    player->play();
    playButton->setToPlaying();

    displayMetadata(currentSongIndex);
    displayCoverArt(filePath);

    emit rowDoubleClicked(currentSongIndex, filePath);
    emit songChanged();
}

void AudioQueueTable::displayMetadata(int row)
{
    if (!m_metadataLabel) return;
    if (row < 0 || row >= m_fileQueue.size()) return;

    QString filePath = m_fileQueue.at(row);
    AudioMetadata meta = m_metadataReader.readMetadata(filePath);

    QString html = QStringLiteral(
        "<div style='line-height:150%;'>"
        "<div style='font-size:16px; font-weight:600;'>%1</div>"
        "<div style='color:#BD93F9; margin-bottom:8px;'>%2</div>"
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td style='color:#9A95A8; padding-right:10px;'>Album</td><td>%3</td></tr>"
        "<tr><td style='color:#9A95A8; padding-right:10px;'>Genre</td><td>%4</td></tr>"
        "<tr><td style='color:#9A95A8; padding-right:10px;'>Duration</td><td>%5</td></tr>"
        "<tr><td style='color:#9A95A8; padding-right:10px;'>Bitrate</td><td>%6 kbps</td></tr>"
        "</table></div>")
        .arg(meta.title.toHtmlEscaped(),
             meta.artist.toHtmlEscaped(),
             meta.album.toHtmlEscaped(),
             meta.genre.toHtmlEscaped(),
             meta.duration.toHtmlEscaped(),
             meta.bitrate.toHtmlEscaped());

    m_metadataLabel->setText(html);
}

void AudioQueueTable::displayCoverArt(const QString& filePath)
{
    if (!m_coverArtLabel) return;

    // Clear previous content
    m_coverArtLabel->clear();

    QImage cover = m_metadataReader.extractCoverArt(filePath);
    if (!cover.isNull()) {
        // Convert to pixmap and scale properly
        QPixmap pixmap = QPixmap::fromImage(cover);

        // Calculate scaled size maintaining aspect ratio
        int labelWidth = m_coverArtLabel->width() - 2; // Padding
        int labelHeight = m_coverArtLabel->height() - 2;
        pixmap = pixmap.scaled(labelWidth, labelHeight,
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);

        m_coverArtLabel->setPixmap(pixmap);
    } else {
        // Show placeholder text if no art found
        m_coverArtLabel->setText("No Cover Art");
        m_coverArtLabel->setStyleSheet("QLabel { color: gray; font-style: italic; }");
        m_coverArtLabel->setAlignment(Qt::AlignCenter);
    }
}

void AudioQueueTable::resetPlayerUI()
{
    pendingFilePath.clear();
    player->stopAndClear();
    progressBar->setAudioFile(QString());
    progressBar->setValue(0);
    playButton->setToPaused();

    if (m_metadataLabel) {
        m_metadataLabel->clear();
    }
    if (m_coverArtLabel) {
        m_coverArtLabel->clear();
        m_coverArtLabel->setText("No Cover Art");
        m_coverArtLabel->setStyleSheet("QLabel { color: gray; font-style: italic; }");
        m_coverArtLabel->setAlignment(Qt::AlignCenter);
    }

    emit songChanged();
}

void AudioQueueTable::onCustomContextMenuRequested()
{
    // Create context menu
    m_contextMenu = new QMenu(m_tableWidget);
    QAction *moveUpAction = m_contextMenu->addAction("Move up");
    QAction *moveDownAction = m_contextMenu->addAction("Move down");
    QAction *removeAction = m_contextMenu->addAction("Remove song");
    QAction *clearAction = m_contextMenu->addAction("Clear queue");

    // Connect actions
    connect(moveUpAction, &QAction::triggered, this, &AudioQueueTable::moveUp);
    connect(moveDownAction, &QAction::triggered, this, &AudioQueueTable::moveDown);
    connect(removeAction, &QAction::triggered, this, &AudioQueueTable::removeSelectedRows);
    connect(clearAction, &QAction::triggered, this, &AudioQueueTable::clearQueue);

    // Context menu request handler
    connect(m_tableWidget, &QTableWidget::customContextMenuRequested, this,
            [this, moveUpAction, moveDownAction, removeAction, clearAction](const QPoint &pos) {
                QList<QTableWidgetSelectionRange> ranges = m_tableWidget->selectedRanges();
                bool hasSelection = m_tableWidget->selectedItems().count() > 0;
                bool singleSelection = ranges.count() == 1;
                int selectedRow = singleSelection ? ranges.first().topRow() : -1;

                // Enable/disable actions based on selection state and position,
                // so actions that would be no-ops are visibly greyed out.
                moveUpAction->setEnabled(hasSelection && singleSelection && selectedRow > 0);
                moveDownAction->setEnabled(hasSelection && singleSelection &&
                                            selectedRow >= 0 && selectedRow < m_fileQueue.size() - 1);
                removeAction->setEnabled(hasSelection);
                clearAction->setEnabled(!m_fileQueue.isEmpty());

                m_contextMenu->exec(m_tableWidget->viewport()->mapToGlobal(pos));
            });
}

bool AudioQueueTable::checkExists(const QString &filePath)
{
    // Check if file already exists in queue
    int existingIndex = m_fileQueue.indexOf(filePath);
    if (existingIndex != -1) {
        // Highlight the existing row briefly
        m_tableWidget->selectRow(existingIndex);
        QTimer::singleShot(300, this, [this]() {
            m_tableWidget->clearSelection();
        });

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(m_tableWidget,
                                      "Duplicate Entry",
                                      "This song is already in the queue!\nAdd again?",
                                      QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return false;
        }
    }

    return true;
}

void AudioQueueTable::writeRow(int row, const AudioMetadata &meta, const QString &filePath)
{
    QTableWidgetItem *nameItem = new QTableWidgetItem(meta.title.isEmpty() ?
                                                          QFileInfo(filePath).fileName() : meta.title);
    nameItem->setData(Qt::UserRole, filePath);
    m_tableWidget->setItem(row, 1, nameItem);

    m_tableWidget->setItem(row, 2, new QTableWidgetItem(meta.artist));
    m_tableWidget->setItem(row, 3, new QTableWidgetItem(meta.album));
    m_tableWidget->setItem(row, 4, new QTableWidgetItem(meta.genre));

    QTableWidgetItem *durationItem = new QTableWidgetItem(meta.duration);
    durationItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_tableWidget->setItem(row, 5, durationItem);
}

void AudioQueueTable::renumberRows()
{
    for (int row = 0; row < m_tableWidget->rowCount(); ++row) {
        QTableWidgetItem *indexItem = m_tableWidget->item(row, 0);
        if (!indexItem) {
            indexItem = new QTableWidgetItem();
            indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
            indexItem->setTextAlignment(Qt::AlignCenter);
            m_tableWidget->setItem(row, 0, indexItem);
        }
        indexItem->setText(QString::number(row + 1));
    }
}

void AudioQueueTable::enqueue(const QString& filePath) {

    if (!this->checkExists(filePath)){
        return;
    }

    // Add to internal queue
    m_fileQueue.enqueue(filePath);

    // Add to table UI
    AudioMetadata meta = m_metadataReader.readMetadata(filePath);
    const int row = m_tableWidget->rowCount();
    m_tableWidget->insertRow(row);
    writeRow(row, meta, filePath);
    renumberRows();

    emit queueChanged();
}

void AudioQueueTable::handleFilesDropped(const QStringList &filePaths)
{
    for (const QString &filePath : filePaths) {
        enqueue(filePath);
    }
}

QString AudioQueueTable::dequeue() {
    if (m_fileQueue.isEmpty())
        return QString();

    // Remove from internal queue
    QString filePath = m_fileQueue.dequeue();

    // Remove from table UI (first row)
    if (m_tableWidget->rowCount() > 0) {
        m_tableWidget->removeRow(0);
    }

    return filePath;
}

QString AudioQueueTable::peekFront() const {
    return m_fileQueue.isEmpty() ? QString() : m_fileQueue.head();
}

bool AudioQueueTable::isEmpty() const {
    return m_fileQueue.isEmpty();
}

void AudioQueueTable::clearQueue() {
    m_fileQueue.clear();
    m_tableWidget->setRowCount(0);
    currentSongIndex = -1;
    resetPlayerUI();
    emit queueChanged();
}

int AudioQueueTable::count() const {
    return m_fileQueue.size();
}

// Iteration methods
QString AudioQueueTable::at(int index) const {
    return (index >= 0 && index < m_fileQueue.size()) ? m_fileQueue.at(index) : QString();
}

QList<QString> AudioQueueTable::getAllItems() const {
    return m_fileQueue.toList();
}

void AudioQueueTable::removeRow(int row)
{
    if (row < 0 || row >= m_fileQueue.size() || row >= m_tableWidget->rowCount()) return;

    QString removedFile = m_fileQueue.at(row);
    m_fileQueue.removeAt(row);
    m_tableWidget->removeRow(row);

    bool removedCurrentSong = (row == currentSongIndex);
    if (row < currentSongIndex) {
        currentSongIndex--;
    }

    emit rowRemoved(row, removedFile);
    renumberRows();

    if (removedCurrentSong) {
        if (!m_fileQueue.isEmpty()) {
            if (currentSongIndex >= m_fileQueue.size()) currentSongIndex = 0;
            playCurrentIndex();
        } else {
            currentSongIndex = -1;
            resetPlayerUI();
        }
    }

    emit queueChanged();
}

void AudioQueueTable::removeSelectedRows()
{
    // Get selected rows (using selectedRanges for better accuracy)
    QList<QTableWidgetSelectionRange> ranges = m_tableWidget->selectedRanges();
    QList<int> rows;

    foreach(const QTableWidgetSelectionRange &range, ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.append(row);
        }
    }

    // Remove duplicates and sort in reverse order so earlier removals don't
    // shift the indices of rows still queued for removal.
    rows = QSet<int>(rows.begin(), rows.end()).values();
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    bool removedCurrentSong = false;

    foreach(int row, rows) {
        if (row < 0 || row >= m_fileQueue.size() || row >= m_tableWidget->rowCount()) continue;

        QString removedFile = m_fileQueue.at(row);
        m_fileQueue.removeAt(row);
        m_tableWidget->removeRow(row);

        if (row == currentSongIndex) {
            removedCurrentSong = true;
        } else if (row < currentSongIndex) {
            currentSongIndex--;
        }

        emit rowRemoved(row, removedFile);
    }

    renumberRows();

    if (removedCurrentSong) {
        if (!m_fileQueue.isEmpty()) {
            if (currentSongIndex < 0 || currentSongIndex >= m_fileQueue.size()) currentSongIndex = 0;
            playCurrentIndex();
        } else {
            currentSongIndex = -1;
            resetPlayerUI();
        }
    } else if (m_fileQueue.isEmpty()) {
        currentSongIndex = -1;
        resetPlayerUI();
    }

    emit queueChanged();
}

void AudioQueueTable::moveUp()
{
    QList<QTableWidgetSelectionRange> ranges = m_tableWidget->selectedRanges();
    if (ranges.isEmpty()) return;

    int selectedRow = ranges.first().topRow();
    if (selectedRow <= 0) return; // Can't move top item up

    // Move in data structure
    m_fileQueue.swapItemsAt(selectedRow, selectedRow - 1);

    // Move in UI
    swapTableRows(selectedRow, selectedRow - 1);

    if (currentSongIndex == selectedRow) currentSongIndex = selectedRow - 1;
    else if (currentSongIndex == selectedRow - 1) currentSongIndex = selectedRow;

    renumberRows();

    // Reselect the moved item
    m_tableWidget->selectRow(selectedRow - 1);
    emit queueChanged();
}

void AudioQueueTable::moveDown()
{
    QList<QTableWidgetSelectionRange> ranges = m_tableWidget->selectedRanges();
    if (ranges.isEmpty()) return;

    int selectedRow = ranges.first().topRow();
    if (selectedRow >= m_fileQueue.size() - 1) return; // Can't move bottom item down

    // Move in data structure
    m_fileQueue.swapItemsAt(selectedRow, selectedRow + 1);

    // Move in UI
    swapTableRows(selectedRow, selectedRow + 1);

    if (currentSongIndex == selectedRow) currentSongIndex = selectedRow + 1;
    else if (currentSongIndex == selectedRow + 1) currentSongIndex = selectedRow;

    renumberRows();

    // Reselect the moved item
    m_tableWidget->selectRow(selectedRow + 1);
    emit queueChanged();
}

void AudioQueueTable::swapTableRows(int row1, int row2)
{
    for (int col = 0; col < m_tableWidget->columnCount(); ++col) {
        QTableWidgetItem* item1 = m_tableWidget->takeItem(row1, col);
        QTableWidgetItem* item2 = m_tableWidget->takeItem(row2, col);

        m_tableWidget->setItem(row1, col, item2);
        m_tableWidget->setItem(row2, col, item1);
    }
}

void AudioQueueTable::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia) {
        if (loopEnabled) {
            // Restart the current song using the path instead of index
            int index = m_fileQueue.indexOf(pendingFilePath);
            if (index != -1) {
                currentSongIndex = index;
                QString currentSong = m_fileQueue.at(currentSongIndex);
                player->setCurrentSong(currentSong);
                progressBar->setValue(0);  // Reset progress bar
                playButton->setToPlaying();
                player->play();
            }
        } else {
            nextSong();
        }
    }
}

void AudioQueueTable::nextSong()
{
    if (isEmpty()) return;

    // A song that has been fully played (or explicitly skipped past) is
    // consumed from the queue - it won't be offered again.
    int finishedIndex = currentSongIndex;
    if (finishedIndex >= 0 && finishedIndex < m_fileQueue.size()) {
        QString finishedFile = m_fileQueue.at(finishedIndex);
        m_fileQueue.removeAt(finishedIndex);
        m_tableWidget->removeRow(finishedIndex);
        emit rowRemoved(finishedIndex, finishedFile);
        renumberRows();
    }

    if (m_fileQueue.isEmpty()) {
        currentSongIndex = -1;
        resetPlayerUI();
        emit queueChanged();
        return;
    }

    if (currentSongIndex < 0 || currentSongIndex >= m_fileQueue.size()) {
        currentSongIndex = 0;
    }

    playCurrentIndex();
    emit queueChanged();
}

void AudioQueueTable::previousSong()
{
    if (isEmpty()) return;

    currentSongIndex--;
    if (currentSongIndex < 0) currentSongIndex = m_fileQueue.size() - 1;

    playCurrentIndex();
}

void AudioQueueTable::shuffleQueue()
{
    if (m_fileQueue.isEmpty()) {
        return;
    }

    QString currentFile = (currentSongIndex >= 0 && currentSongIndex < m_fileQueue.size())
                               ? m_fileQueue.at(currentSongIndex) : QString();

    // Shuffle the queue using std::shuffle
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(m_fileQueue.begin(), m_fileQueue.end(), g);

    // Now update the UI (table) to reflect the shuffled order
    for (int row = 0; row < m_fileQueue.size(); ++row) {
        QString filePath = m_fileQueue.at(row);
        AudioMetadata meta = m_metadataReader.readMetadata(filePath);
        writeRow(row, meta, filePath);
    }
    renumberRows();

    currentSongIndex = currentFile.isEmpty() ? -1 : m_fileQueue.indexOf(currentFile);

    emit queueChanged();  // Notify that the queue has changed
}

void AudioQueueTable::setLoopEnabled(bool enabled) {
    loopEnabled = enabled;
}

bool AudioQueueTable::isLoopEnabled() const {
    return loopEnabled;
}

bool AudioQueueTable::hasActiveSong() const {
    return currentSongIndex >= 0 && currentSongIndex < m_fileQueue.size();
}

void AudioQueueTable::playFirstOrCurrent() {
    if (isEmpty()) return;
    if (currentSongIndex < 0 || currentSongIndex >= m_fileQueue.size()) {
        currentSongIndex = 0;
    }
    playCurrentIndex();
}
