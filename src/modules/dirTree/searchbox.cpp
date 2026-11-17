// searchbox.cpp
#include "searchbox.h"
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QResizeEvent>

SearchBox::SearchBox(QWidget *parent)
    : QLineEdit(parent), m_currentFilter("File Names")
{
    setPlaceholderText("Search...");

    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QIcon(":/icons/search.png").pixmap(14, 14));
    m_iconLabel->setStyleSheet("background: transparent; border: none;");

    // A persistent badge showing which field we're filtering by - unlike the
    // placeholder text, this stays visible once the user actually types.
    m_filterBadge = new QLabel(this);
    m_filterBadge->setStyleSheet(
        "QLabel { background-color: #5A5673; color: #D0CCD0; border-radius: 0px; "
        "padding: 1px 6px; font-size: 11px; }");
    m_filterBadge->setAlignment(Qt::AlignCenter);

    updateFilterBadge();

    connect(this, &QLineEdit::returnPressed, this, &SearchBox::onSearch);
}

void SearchBox::updateFilterBadge()
{
    m_filterBadge->setText(m_currentFilter);
    m_filterBadge->adjustSize();
    layoutPrefixWidgets();
}

void SearchBox::layoutPrefixWidgets()
{
    const int iconSize = 14;
    const int spacing = 4;
    const int leftPad = 6;

    m_iconLabel->setGeometry(leftPad, (height() - iconSize) / 2, iconSize, iconSize);

    int badgeX = leftPad + iconSize + spacing;
    m_filterBadge->move(badgeX, (height() - m_filterBadge->height()) / 2);

    setTextMargins(badgeX + m_filterBadge->width() + spacing, 0, 0, 0);
}

void SearchBox::resizeEvent(QResizeEvent *event)
{
    QLineEdit::resizeEvent(event);
    layoutPrefixWidgets();
}

void SearchBox::changeTextFromMenu(QAction *action)
{
    m_currentFilter = action->text();
    updateFilterBadge();
    setFocus();
}

void SearchBox::onSearch()
{
    if (!text().isEmpty()) {
        emit searchTriggered(m_currentFilter, text());
    }
}
