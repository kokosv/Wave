#ifndef AUDIOTREEWIDGET_H
#define AUDIOTREEWIDGET_H

#include <QTreeWidget>
#include <QMimeData>

class AudioTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit AudioTreeWidget(QWidget *parent = nullptr);

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
};

#endif // AUDIOTREEWIDGET_H
