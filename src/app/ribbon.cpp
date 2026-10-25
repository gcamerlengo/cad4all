// SPDX-License-Identifier: GPL-3.0-or-later
#include "ribbon.h"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kRows = 3;

QToolButton* button(QAction* action, bool large, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setDefaultAction(action);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    if (large) {
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(32, 32));
        b->setMinimumWidth(56);
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    } else {
        b->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(16, 16));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    return b;
}

} // namespace

RibbonPanel::RibbonPanel(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 0);
    layout->setSpacing(0);
    row_ = new QHBoxLayout;
    row_->setSpacing(2);
    layout->addLayout(row_, 1);
    auto* label = new QLabel(title, this);
    label->setAlignment(Qt::AlignCenter);
    label->setEnabled(false);  // the palette's dimmed text, like AutoCAD's panel titles
    layout->addWidget(label);
}

void RibbonPanel::addLarge(QAction* action)
{
    column_ = nullptr;
    row_->addWidget(button(action, true, this));
}

void RibbonPanel::nextColumn()
{
    if (!column_ || columnItems_ == kRows) {
        column_ = new QGridLayout;
        column_->setSpacing(0);
        column_->setRowStretch(kRows, 1);
        row_->addLayout(column_);
        columnItems_ = 0;
    }
}

void RibbonPanel::addSmall(QAction* action)
{
    addSmall(button(action, false, this));
}

void RibbonPanel::addSmall(QWidget* widget)
{
    nextColumn();
    column_->addWidget(widget, columnItems_++, 0);
}

Ribbon::Ribbon(QWidget* parent)
    : QTabWidget(parent)
{
    setDocumentMode(true);
    setUsesScrollButtons(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QWidget* Ribbon::addPage(const QString& title)
{
    auto* page = new QWidget(this);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addStretch(1);
    addTab(page, title);
    return page;
}

RibbonPanel* Ribbon::addPanel(QWidget* page, const QString& title)
{
    auto* layout = static_cast<QHBoxLayout*>(page->layout());
    // Before the trailing stretch, with a separator after each panel.
    const int at = layout->count() - 1;
    auto* panel = new RibbonPanel(title, page);
    layout->insertWidget(at, panel);
    auto* line = new QFrame(page);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->insertWidget(at + 1, line);
    return panel;
}
