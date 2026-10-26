// SPDX-License-Identifier: GPL-3.0-or-later
#include "commandline.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

bool CommandInput::event(QEvent* event)
{
    // With nothing typed, Ctrl+Z and friends belong to the drawing, not to
    // the text field's own undo.
    if (event->type() == QEvent::ShortcutOverride && text().isEmpty()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo) || key->matches(QKeySequence::SelectAll)) {
            event->ignore();
            return true;
        }
    }
    return QLineEdit::event(event);
}

void CommandInput::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        if (spaceIsText_)
            break;
        [[fallthrough]];
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString value = text();
        if (!value.trimmed().isEmpty()) {
            history_.removeAll(value);
            history_.append(value);
        }
        historyPos_ = static_cast<int>(history_.size());
        clear();
        emit submitted(value);
        return;
    }
    case Qt::Key_Escape:
        clear();
        emit escapePressed();
        return;
    case Qt::Key_Delete:
        if (text().isEmpty()) {
            emit deletePressed();
            return;
        }
        break;
    case Qt::Key_Up:
        if (historyPos_ > 0)
            setText(history_.at(--historyPos_));
        return;
    case Qt::Key_Down:
        if (historyPos_ < history_.size() - 1) {
            setText(history_.at(++historyPos_));
        } else {
            historyPos_ = static_cast<int>(history_.size());
            clear();
        }
        return;
    default:
        break;
    }
    QLineEdit::keyPressEvent(event);
}

CommandLine::CommandLine(QWidget* parent)
    : QWidget(parent)
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    setObjectName(QStringLiteral("commandLine"));
    history_ = new QPlainTextEdit(this);
    history_->setObjectName(QStringLiteral("commandHistory"));
    history_->setReadOnly(true);
    history_->setFont(mono);
    history_->setFocusPolicy(Qt::NoFocus);
    history_->setMaximumBlockCount(500);
    history_->setFixedHeight(QFontMetrics(mono).lineSpacing() * 5 + 8);

    prompt_ = new QLabel(this);
    prompt_->setObjectName(QStringLiteral("commandPrompt"));
    prompt_->setFont(mono);
    input_ = new CommandInput(this);
    input_->setObjectName(QStringLiteral("commandInput"));
    input_->setFont(mono);
    input_->setPlaceholderText(tr("Digitare un comando (es. LINEA o L)"));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(prompt_);
    row->addWidget(input_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(2);
    layout->addWidget(history_);
    layout->addLayout(row);
}

void CommandLine::setPrompt(const QString& prompt)
{
    prompt_->setText(prompt);
}

void CommandLine::appendMessage(const QString& text)
{
    history_->appendPlainText(text);
}
