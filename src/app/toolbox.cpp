// SPDX-License-Identifier: GPL-3.0-or-later
#include "toolbox.h"

#include "commandengine.h"

#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

using namespace cad;

Toolbox::Toolbox(CommandEngine& engine, QWidget* parent)
    : QWidget(parent)
    , engine_(engine)
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(1);
    layout_->addStretch(1);
    connect(&engine_, &CommandEngine::changed, this, &Toolbox::refresh);
}

void Toolbox::addSection(const QString& title)
{
    auto* label = new QLabel(title, this);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    label->setContentsMargins(2, tools_.empty() ? 0 : 8, 0, 2);
    layout_->insertWidget(layout_->count() - 1, label);
}

void Toolbox::addTool(const QString& text, const QString& command, const QIcon& icon)
{
    auto* b = new QToolButton(this);
    b->setText(text);
    b->setIcon(icon);
    b->setIconSize(QSize(22, 22));
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setAutoRaise(true);
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(command.isEmpty() ? tr("Selezione: termina il comando in corso") : QStringLiteral("%1 (%2)").arg(text, command));
    layout_->insertWidget(layout_->count() - 1, b);
    tools_.push_back({command, b});
    connect(b, &QToolButton::clicked, this, [this, command] {
        if (command.isEmpty()) {
            engine_.cancel();
        } else if (engine_.activeCommandName() != command) {
            engine_.cancel();
            engine_.run(command);
        }
        refresh();
        emit toolChosen(command);
    });
    refresh();
}

void Toolbox::refresh()
{
    const QString active = engine_.activeCommandName();
    bool found = false;
    for (const Tool& t : tools_)
        found = found || (!t.command.isEmpty() && t.command == active);
    for (const Tool& t : tools_)
        t.button->setChecked(found ? t.command == active : t.command.isEmpty());
}
