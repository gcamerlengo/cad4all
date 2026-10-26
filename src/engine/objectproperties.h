// SPDX-License-Identifier: GPL-3.0-or-later
// The Properties palette (AutoCAD's PROPERTIES, Ctrl+1) without its window:
// the layer, look and geometry of the selected objects as a list of fields,
// and the change of one field on all of them as one undo step. With several
// objects a field shows their common value or "*VARIE*"; geometry is shown
// only when they are all of one kind.
#pragma once

#include "document.h"

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace cad {

struct ObjectProperty {
    enum class Type { Number, Choice, Text };
    enum class Unit { Length, Angle, Factor, Count, Area };

    std::string key;
    QString group;  // "Generale", "Geometria", "Testo", ...
    QString label;
    Type type = Type::Number;
    Unit unit = Unit::Length;
    bool readOnly = false;
    bool varies = false;  // the objects disagree: shown as *VARIE*
    double number = 0.0;  // Number: the value (degrees for angles); Choice: the index
    QString text;         // Text: the value
    QStringList choices;  // Choice: the options; Text: suggestions (colours, patterns)
};

// The kind of the selected objects ("Linea", "Tutti (5)"...).
QString selectionTitle(const Document& doc, const std::vector<EntityId>& ids);
std::vector<ObjectProperty> objectProperties(const Document& doc, const std::vector<EntityId>& ids);

// Sets `key` to `value` (a number in the field's unit, a choice's index or
// a text) on every object that has it, in one transaction named `label`.
// Returns how many objects changed; 0 if the value is not valid.
int setObjectProperty(Document& doc, const std::vector<EntityId>& ids, const std::string& key, const QString& value);

} // namespace cad
