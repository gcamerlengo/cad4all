// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace theme {

namespace {

struct Colors {
    QColor window;   // behind the panels: menu bar, status bar, splitters
    QColor panel;    // ribbon, toolbars, docked panels
    QColor field;    // text fields, lists, command history
    QColor hover;    // a button under the mouse
    QColor pressed;  // a button held down
    QColor border;
    QColor text;
    QColor dim;      // secondary text: panel titles, headers, hints
    QColor accent;
    QColor scroll;   // scroll bar handles
};

Colors colors(Theme theme)
{
    if (theme == Theme::Light)
        return {QColor(0xe8, 0xeb, 0xef), QColor(0xf7, 0xf8, 0xfa), QColor(0xff, 0xff, 0xff), QColor(0xe3, 0xe8, 0xef),
                QColor(0xd3, 0xdb, 0xe6), QColor(0xcf, 0xd5, 0xdd), QColor(0x1f, 0x25, 0x2d), QColor(0x67, 0x70, 0x7c),
                QColor(0x25, 0x6b, 0xd6), QColor(0xb9, 0xc1, 0xcc)};
    return {QColor(0x1e, 0x22, 0x28), QColor(0x29, 0x2e, 0x36), QColor(0x1a, 0x1e, 0x23), QColor(0x36, 0x3d, 0x47),
            QColor(0x42, 0x4a, 0x56), QColor(0x39, 0x40, 0x4a), QColor(0xe3, 0xe6, 0xea), QColor(0x95, 0x9e, 0xa9),
            QColor(0x4c, 0x9a, 0xff), QColor(0x4a, 0x52, 0x5e)};
}

QPalette palette(const Colors& c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.panel);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.field);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.hover);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, c.field);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.dim);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Light, c.pressed.lighter(115));
    p.setColor(QPalette::Midlight, c.pressed);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.window);
    p.setColor(QPalette::Shadow, Qt::black);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.dim.darker(c.text.lightness() > 128 ? 130 : 70));
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.border);
    return p;
}

// Placeholders such as @panel are the theme's colours.
const char* const kStyleSheet = R"(
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @window; width: 5px; height: 5px; }
QMainWindow::separator:hover { background: @accent; }
QSplitter::handle { background: @window; }
QSplitter::handle:hover { background: @accent; }
QToolTip { background: @field; color: @text; border: 1px solid @border; padding: 4px 6px; }

QMenuBar { background: @window; color: @text; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @hover; }
QMenu { background: @panel; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 26px; border-radius: 4px; }
QMenu::item:selected { background: @accentSoft; }
QMenu::item:disabled { color: @dim; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QToolBar { background: @panel; border: none; border-bottom: 1px solid @border; padding: 3px 4px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 5px 4px; }
QToolBar#ribbonToolBar { background: @window; padding: 0px; border: none; }
QToolButton { background: transparent; color: @text; border: 1px solid transparent; border-radius: 5px; padding: 3px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed { background: @pressed; }
QToolButton:checked { background: @accentSoft; border-color: @accentLine; }
QToolButton:disabled { color: @dim; }

QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; padding: 6px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:bottom { border-bottom: none; border-top: 2px solid transparent; }
QTabBar::tab:bottom:selected { border-top: 2px solid @accent; }
QTabWidget::pane { border: none; }
QTabWidget#ribbon { background: @window; }
QTabWidget#ribbon QTabBar::tab { padding: 7px 16px 6px 16px; }
QScrollArea#ribbonPage, QWidget#ribbonContent { background: @panel; border-top: 1px solid @border; border-bottom: 1px solid @border; }
QWidget#ribbonContent { border: none; }
QWidget#ribbonContent QToolButton { padding: 2px 3px; }
QLabel#ribbonPanelTitle { color: @dim; padding-top: 1px; }
QFrame#ribbonSeparator { background: @border; border: none; margin: 8px 3px 6px 3px; }

QDockWidget { color: @text; }
QDockWidget::title { background: @window; padding: 6px 8px; text-align: left; }
QDockWidget::close-button, QDockWidget::float-button { border: none; background: transparent; padding: 1px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @hover; border-radius: 3px; }

QLineEdit, QPlainTextEdit, QTextEdit { background: @field; color: @text; border: 1px solid @border; border-radius: 4px; padding: 3px 5px;
                                       selection-background-color: @accent; selection-color: #ffffff; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }

QTreeView, QListView, QTableView { background: @field; color: @text; border: 1px solid @border; border-radius: 4px;
                                   selection-background-color: @accentSoft; selection-color: @text; outline: 0; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: @accentSoft; color: @text; }
QHeaderView { background: @field; }
QHeaderView::section { background: @panel; color: @dim; border: none; border-bottom: 1px solid @border; border-right: 1px solid @border;
                       padding: 4px 6px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0px; }
QScrollBar::handle { background: @scroll; border-radius: 3px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::handle:hover { background: @dim; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QPushButton { background: @hover; color: @text; border: 1px solid @border; border-radius: 5px; padding: 5px 12px; }
QPushButton:hover { background: @pressed; }
QPushButton:pressed { background: @border; }
QPushButton:default { background: @accent; color: #ffffff; border-color: @accent; }
QPushButton:disabled { color: @dim; }

QStatusBar { background: @window; color: @dim; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel#coordinates { color: @text; padding: 0px 8px; }
QStatusBar QToolButton { color: @dim; font-weight: bold; padding: 2px 10px; border: 1px solid @border; border-radius: 10px; margin: 3px 2px; }
QStatusBar QToolButton:hover { color: @text; }
QStatusBar QToolButton:checked { color: @accent; background: @accentSoft; border-color: @accentLine; }

QWidget#commandLine { background: @panel; }
QPlainTextEdit#commandHistory { background: @panel; color: @dim; border: none; padding: 2px 4px; }
QLabel#commandPrompt { color: @accent; font-weight: bold; padding-left: 4px; }
QLineEdit#commandInput { padding: 5px 6px; }

QLabel#toolboxSection { color: @dim; }
QDockWidget QToolButton { padding: 2px 4px; }
QLabel#infoBoxTitle { color: @text; }
)";

QString colorText(const QColor& c, int alphaPercent = 100)
{
    if (alphaPercent >= 100)
        return c.name();
    return QStringLiteral("rgba(%1, %2, %3, %4%)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alphaPercent);
}

QString styleSheet(const Colors& c, Theme theme)
{
    QString sheet = QString::fromLatin1(kStyleSheet);
    // Longer names first, so @accentSoft is not read as @accent.
    const std::pair<const char*, QString> tokens[] = {
        {"@accentSoft", colorText(c.accent, theme == Theme::Dark ? 24 : 16)},
        {"@accentLine", colorText(c.accent, 55)},
        {"@accent", colorText(c.accent)},
        {"@window", colorText(c.window)},
        {"@panel", colorText(c.panel)},
        {"@field", colorText(c.field)},
        {"@hover", colorText(c.hover)},
        {"@pressed", colorText(c.pressed)},
        {"@border", colorText(c.border)},
        {"@text", colorText(c.text)},
        {"@dim", colorText(c.dim)},
        {"@scroll", colorText(c.scroll)},
    };
    for (const auto& [token, value] : tokens)
        sheet.replace(QLatin1String(token), value);
    return sheet;
}

Theme currentTheme = Theme::Dark;

} // namespace

void apply(Theme theme)
{
    currentTheme = theme;
    const Colors c = colors(theme);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(palette(c));
    qApp->setStyleSheet(styleSheet(c, theme));
}

Theme current()
{
    return currentTheme;
}

QString name(Theme theme)
{
    return theme == Theme::Light ? QStringLiteral("chiaro") : QStringLiteral("scuro");
}

bool fromName(const QString& name, Theme* theme)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("scuro") || n == QLatin1String("dark"))
        *theme = Theme::Dark;
    else if (n == QLatin1String("chiaro") || n == QLatin1String("light"))
        *theme = Theme::Light;
    else
        return false;
    return true;
}

Theme saved()
{
    Theme theme = Theme::Dark;
    fromName(QSettings().value(QStringLiteral("interfaccia/tema"), name(Theme::Dark)).toString(), &theme);
    return theme;
}

void save(Theme theme)
{
    QSettings().setValue(QStringLiteral("interfaccia/tema"), name(theme));
}

QColor accent()
{
    return colors(currentTheme).accent;
}

} // namespace theme
