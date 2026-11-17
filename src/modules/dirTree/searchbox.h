#ifndef SEARCHBOX_H
#define SEARCHBOX_H

#include <QLineEdit>
#include <QAction>
#include <QLabel>

class SearchBox : public QLineEdit
{
    Q_OBJECT
public:
    explicit SearchBox(QWidget *parent = nullptr);

    QString currentFilter() const { return m_currentFilter; }

signals:
    void searchTriggered(const QString &filter, const QString &text);

public slots:
    void changeTextFromMenu(QAction *action);
    void onSearch();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateFilterBadge();
    void layoutPrefixWidgets();

    QString m_currentFilter;
    QLabel *m_iconLabel;
    QLabel *m_filterBadge;
};

#endif // SEARCHBOX_H
