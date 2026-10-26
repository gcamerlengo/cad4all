// SPDX-License-Identifier: GPL-3.0-or-later
#include "ribbon.h"

#include <QAction>
#include <QActionEvent>

#include <cstdlib>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kRows = 3;

// A long name on two lines, split at the space nearest the middle
// ("Quota / lineare"), as AutoCAD's large buttons do.
QString twoLines(const QString& text)
{
    if (text.size() < 10)
        return text;
    qsizetype best = -1;
    for (qsizetype i = text.indexOf(QLatin1Char(' ')); i >= 0; i = text.indexOf(QLatin1Char(' '), i + 1))
        if (best < 0 || std::abs(i - text.size() / 2) < std::abs(best - text.size() / 2))
            best = i;
    if (best < 0)
        return text;
    QString wrapped = text;
    wrapped[best] = QLatin1Char('\n');
    return wrapped;
}

// Keeps the two-line name of a large button when its action changes.
class RibbonButton : public QToolButton {
public:
    RibbonButton(bool large, QWidget* parent) : QToolButton(parent), large_(large) {}

    void setAction(QAction* action)
    {
        setDefaultAction(action);
        wrap();
    }

protected:
    void actionEvent(QActionEvent* event) override
    {
        QToolButton::actionEvent(event);
        wrap();
    }

private:
    void wrap()
    {
        if (large_ && defaultAction())
            setText(twoLines(defaultAction()->iconText()));
    }

    bool large_;
};

QToolButton* button(QAction* action, bool large, QWidget* parent)
{
    auto* b = new RibbonButton(large, parent);
    b->setAction(action);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    // Never narrower than the text: a narrow window scrolls the ribbon instead.
    if (large) {
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(32, 32));
        b->setMinimumWidth(56);
        b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    } else {
        b->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(16, 16));
        b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    return b;
}

// A page of the ribbon: its panels in a row that scrolls sideways when the
// window is narrower than the panels.
class RibbonPage : public QScrollArea {
public:
    explicit RibbonPage(QWidget* parent) : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setObjectName(QStringLiteral("ribbonPage"));
        auto* content = new QWidget(this);
        content->setObjectName(QStringLiteral("ribbonContent"));
        auto* layout = new QHBoxLayout(content);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);
        layout->addStretch(1);
        setWidget(content);
    }

    QSize sizeHint() const override
    {
        // Tall enough for the panels and the scroll bar under them.
        const QSize content = widget()->sizeHint();
        return {content.width(), content.height() + horizontalScrollBar()->sizeHint().height()};
    }
    QSize minimumSizeHint() const override { return {0, sizeHint().height()}; }

protected:
    // The wheel scrolls the panels sideways.
    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint delta = event->angleDelta();
        QScrollBar* bar = horizontalScrollBar();
        bar->setValue(bar->value() - (std::abs(delta.x()) > std::abs(delta.y()) ? delta.x() : delta.y()));
        event->accept();
    }
};

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
    label->setObjectName(QStringLiteral("ribbonPanelTitle"));
    label->setAlignment(Qt::AlignCenter);
    QFont small = label->font();
    small.setPointSizeF(small.pointSizeF() * 0.88);
    label->setFont(small);
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
    setObjectName(QStringLiteral("ribbon"));
    setDocumentMode(true);
    setUsesScrollButtons(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QWidget* Ribbon::addPage(const QString& title)
{
    auto* page = new RibbonPage(this);
    addTab(page, title);
    return page->widget();
}

RibbonPanel* Ribbon::addPanel(QWidget* page, const QString& title)
{
    auto* layout = static_cast<QHBoxLayout*>(page->layout());
    // Before the trailing stretch, with a separator after each panel.
    const int at = layout->count() - 1;
    auto* panel = new RibbonPanel(title, page);
    layout->insertWidget(at, panel);
    auto* line = new QFrame(page);
    line->setObjectName(QStringLiteral("ribbonSeparator"));
    line->setFixedWidth(7);  // a 1 pixel line between 3 pixel margins
    layout->insertWidget(at + 1, line);
    return panel;
}
