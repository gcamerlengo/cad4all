// SPDX-License-Identifier: GPL-3.0-or-later
// The look of the whole application: a dark theme like recent CAD programs
// and a light one, each a palette for the Fusion style plus a style sheet.
#pragma once

#include <QColor>
#include <QString>

namespace theme {

enum class Theme { Dark, Light };

// Applies the theme to the running application.
void apply(Theme theme);
Theme current();

// "scuro" or "chiaro", as saved in the settings and given to --tema.
QString name(Theme theme);
bool fromName(const QString& name, Theme* theme);

// The theme saved in the settings (dark when none is).
Theme saved();
void save(Theme theme);

// Highlight colour of the theme, for checked tools and the current tab.
QColor accent();

} // namespace theme
