// SPDX-License-Identifier: GPL-3.0-or-later
// The AutoCAD-style command line: a short history of messages, the current
// prompt and an input field where Enter and Space both confirm.
#pragma once

#include <QLineEdit>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPlainTextEdit;

class CommandInput : public QLineEdit {
    Q_OBJECT

public:
    using QLineEdit::QLineEdit;

signals:
    void submitted(const QString& text);
    void escapePressed();
    void deletePressed();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QStringList history_;
    int historyPos_ = 0;
};

class CommandLine : public QWidget {
    Q_OBJECT

public:
    explicit CommandLine(QWidget* parent = nullptr);

    CommandInput* input() const { return input_; }
    void setPrompt(const QString& prompt);
    void appendMessage(const QString& text);

private:
    QPlainTextEdit* history_;
    QLabel* prompt_;
    CommandInput* input_;
};
