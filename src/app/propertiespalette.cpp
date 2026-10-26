// SPDX-License-Identifier: GPL-3.0-or-later
#include "propertiespalette.h"

#include "commandengine.h"
#include "objectproperties.h"
#include "shapes.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

using namespace cad;

namespace {

const char* const kVaries = "*VARIE*";

QString shown(const ObjectProperty& p, int decimals)
{
    if (p.type == ObjectProperty::Type::Text) {
        QString s = p.text;
        // Paragraph breaks as in AutoCAD's Properties.
        return s.replace(QLatin1Char('\n'), QLatin1String("\\P"));
    }
    if (p.unit == ObjectProperty::Unit::Count)
        return QString::number(std::lround(p.number));
    const int places = p.unit == ObjectProperty::Unit::Angle ? 2 : p.unit == ObjectProperty::Unit::Factor ? 3 : decimals;
    QString s = QString::fromStdString(formatMeasurement(p.number, places));
    if (p.unit == ObjectProperty::Unit::Angle)
        s += QStringLiteral("°");
    return s;
}

QString signature(const QString& title, const std::vector<ObjectProperty>& props)
{
    QString s = title;
    for (const ObjectProperty& p : props)
        s += QLatin1Char('|') + QString::fromStdString(p.key) + QLatin1Char(':') + p.choices.join(QLatin1Char(','));
    return s;
}

} // namespace

PropertiesPalette::PropertiesPalette(CommandEngine& engine, QWidget* parent)
    : QWidget(parent), engine_(engine)
{
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("propertiesTitle"));
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(title_);
    layout->addWidget(scroll_, 1);
    connect(&engine_, &CommandEngine::changed, this, &PropertiesPalette::refresh);
    connect(&engine_, &CommandEngine::selectionChanged, this, &PropertiesPalette::refresh);
    refresh();
}

void PropertiesPalette::refresh()
{
    const Document& doc = engine_.document();
    ids_.assign(engine_.selection().begin(), engine_.selection().end());
    const std::vector<ObjectProperty> props = objectProperties(doc, ids_);
    const QString title = selectionTitle(doc, ids_);
    const QString sig = signature(title, props);

    if (sig != signature_ || !fields_) {
        signature_ = sig;
        title_->setText(title);
        // The old fields may be the sender of the change that got here.
        if (fields_) {
            scroll_->takeWidget();
            fields_->hide();
            fields_->deleteLater();
        }
        fields_ = new QWidget;
        auto* form = new QFormLayout(fields_);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        QString group;
        for (const ObjectProperty& p : props) {
            if (p.group != group) {
                group = p.group;
                auto* header = new QLabel(group, fields_);
                header->setObjectName(QStringLiteral("propertiesGroup"));
                form->addRow(header);
            }
            const QString key = QString::fromStdString(p.key);
            QWidget* field = nullptr;
            if (p.readOnly) {
                auto* label = new QLabel(fields_);
                label->setTextInteractionFlags(Qt::TextSelectableByMouse);
                field = label;
            } else if (p.type == ObjectProperty::Type::Choice) {
                auto* combo = new QComboBox(fields_);
                combo->addItems(p.choices);
                combo->setPlaceholderText(QString::fromLatin1(kVaries));
                combo->setFocusPolicy(Qt::StrongFocus);
                connect(combo, &QComboBox::activated, this, [this, combo, key = p.key](int index) {
                    if (index != combo->property("shown").toInt())
                        apply(key, QString::number(index));
                });
                field = combo;
            } else if (p.type == ObjectProperty::Type::Text && !p.choices.isEmpty()) {
                auto* combo = new QComboBox(fields_);
                combo->setEditable(true);
                combo->addItems(p.choices);
                combo->setInsertPolicy(QComboBox::NoInsert);
                combo->lineEdit()->setPlaceholderText(QString::fromLatin1(kVaries));
                connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, combo, key = p.key] {
                    if (combo->currentText() != combo->property("shown").toString())
                        apply(key, combo->currentText());
                });
                connect(combo, &QComboBox::activated, this, [this, combo, key = p.key](int) {
                    if (combo->currentText() != combo->property("shown").toString())
                        apply(key, combo->currentText());
                });
                field = combo;
            } else {
                auto* edit = new QLineEdit(fields_);
                edit->setPlaceholderText(QString::fromLatin1(kVaries));
                const bool isText = p.type == ObjectProperty::Type::Text;
                connect(edit, &QLineEdit::editingFinished, this, [this, edit, key = p.key, isText] {
                    QString value = edit->text();
                    if (value == edit->property("shown").toString() || (!isText && value.trimmed().isEmpty()))
                        return;
                    if (isText)
                        value.replace(QLatin1String("\\P"), QLatin1String("\n"));
                    apply(key, value);
                });
                field = edit;
            }
            field->setObjectName(key);
            form->addRow(p.label, field);
        }
        if (props.empty()) {
            auto* hint = new QLabel(tr("Selezionare uno o più oggetti per vederne e cambiarne le proprietà."), fields_);
            hint->setWordWrap(true);
            form->addRow(hint);
        }
        scroll_->setWidget(fields_);
    }

    // The values, except in the field being typed into.
    const int decimals = doc.precision();
    for (const ObjectProperty& p : props) {
        const QString key = QString::fromStdString(p.key);
        QWidget* w = fields_->findChild<QWidget*>(key);
        if (!w || w->hasFocus())
            continue;
        const QString value = p.varies ? QString() : shown(p, decimals);
        if (auto* label = qobject_cast<QLabel*>(w)) {
            label->setText(p.varies ? QString::fromLatin1(kVaries) : value);
        } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
            const QSignalBlocker block(combo);
            if (combo->isEditable()) {
                combo->setEditText(value);
                combo->setProperty("shown", value);
            } else {
                combo->setCurrentIndex(p.varies ? -1 : static_cast<int>(std::lround(p.number)));
                combo->setProperty("shown", combo->currentIndex());
            }
        } else if (auto* edit = qobject_cast<QLineEdit*>(w)) {
            const QSignalBlocker block(edit);
            edit->setText(value);
            edit->setCursorPosition(0);
            edit->setProperty("shown", value);
        }
    }
}

void PropertiesPalette::apply(const std::string& key, const QString& value)
{
    if (setObjectProperty(engine_.document(), ids_, key, value) == 0)
        engine_.message(tr("Valore non valido: %1").arg(value));
    // The field shows the value the objects now have, the bad one included.
    if (QWidget* focus = fields_->focusWidget())
        focus->clearFocus();
    engine_.documentChanged();
}
