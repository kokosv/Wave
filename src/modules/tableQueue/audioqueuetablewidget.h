#ifndef AUDIOQUEUETABLEWIDGET_H
#define AUDIOQUEUETABLEWIDGET_H

#include <QTableWidget>

class AudioQueueTableWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit AudioQueueTableWidget(QWidget *parent = nullptr);

signals:
    void queueFilesDropped(const QStringList &filePaths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

#endif // AUDIOQUEUETABLEWIDGET_H
