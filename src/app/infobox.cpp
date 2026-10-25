// SPDX-License-Identifier: GPL-3.0-or-later
#include "infobox.h"

#include "commandengine.h"
#include "icons.h"
#include "properties.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>

#include <cmath>
#include <limits>

using namespace cad;

namespace {

const double kAuto = std::numeric_limits<double>::quiet_NaN();
const char* const kAutoSuffix = "#auto";

QString iconName(const QString& title)
{
    // The titles come from properties.cpp, the same for tools and objects.
    if (title == QLatin1String("Muro"))
        return QStringLiteral("wall");
    if (title == QLatin1String("Porta"))
        return QStringLiteral("door");
    if (title == QLatin1String("Finestra"))
        return QStringLiteral("window");
    if (title == QLatin1String("Solaio"))
        return QStringLiteral("slab");
    if (title == QLatin1String("Tetto"))
        return QStringLiteral("roof");
    return QStringLiteral("stair");
}

QString signature(const QString& title, const std::vector<Property>& props)
{
    QString s = title;
    for (const Property& p : props)
        s += QLatin1Char('|') + QString::fromStdString(p.key) + QLatin1Char(':') + p.choices.join(QLatin1Char(','));
    return s;
}

} // namespace

InfoBox::InfoBox(CommandEngine& engine, QWidget* parent)
    : QWidget(parent)
    , engine_(engine)
{
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(6, 2, 6, 2);
    icon_ = new QLabel(this);
    title_ = new QLabel(this);
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    title_->setMinimumWidth(130);
    layout_->addWidget(icon_);
    layout_->addWidget(title_);
    layout_->addStretch(1);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(&engine_, &CommandEngine::changed, this, &InfoBox::refresh);
    refresh();
}

void InfoBox::setTool(const QString& command)
{
    if (hasToolProperties(command)) {
        tool_ = command;
        refresh();
    }
}

void InfoBox::refresh()
{
    const QString active = engine_.activeCommandName();
    if (hasToolProperties(active))
        tool_ = active;
    // A single selected object, while no command runs: its own values.
    object_.reset();
    const auto& selection = engine_.selection();
    if (!engine_.isActive() && selection.size() == 1) {
        const Entity* e = engine_.document().entity(*selection.begin());
        if (e && !shapeProperties(e->shape).empty())
            object_ = e->id;
    }
    rebuild();
}

void InfoBox::rebuild()
{
    const Entity* e = object_ ? engine_.document().entity(*object_) : nullptr;
    const QString name = e ? shapeTitle(e->shape) : toolTitle(tool_);
    const std::vector<Property> props = e ? shapeProperties(e->shape) : toolProperties(tool_);
    const QString title = e ? tr("Selezione: %1").arg(name) : tr("Strumento: %1").arg(name);
    const QString sig = signature(title, props);

    if (sig != signature_) {
        signature_ = sig;
        title_->setText(title);
        icon_->setPixmap(icons::make(iconName(name)).pixmap(24, 24));
        // The old fields may be the sender of the change that got here: they
        // go at once from the box, and later from memory.
        if (fields_) {
            fields_->hide();
            fields_->setParent(nullptr);
            fields_->deleteLater();
        }
        fields_ = new QWidget(this);
        auto* row = new QHBoxLayout(fields_);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        for (const Property& p : props) {
            auto* label = new QLabel(p.label + QLatin1Char(':'), fields_);
            row->addSpacing(8);
            row->addWidget(label);
            const QString key = QString::fromStdString(p.key);
            if (p.type == Property::Type::Choice) {
                auto* combo = new QComboBox(fields_);
                combo->setObjectName(key);
                combo->addItems(p.choices);
                combo->setFocusPolicy(Qt::ClickFocus);
                row->addWidget(combo);
                connect(combo, &QComboBox::activated, this, [this, combo, key = p.key](int index) {
                    if (combo->parent() == fields_ && index != combo->property("shown").toInt())
                        apply(key, index);
                });
                continue;
            }
            auto* spin = new QDoubleSpinBox(fields_);
            spin->setObjectName(key);
            spin->setKeyboardTracking(false);
            spin->setFocusPolicy(Qt::ClickFocus);
            if (p.type == Property::Type::Angle) {
                spin->setRange(p.minimum, p.maximum);
                spin->setDecimals(1);
                spin->setSuffix(QStringLiteral("°"));
            } else if (p.type == Property::Type::Factor) {
                spin->setRange(p.minimum, p.maximum);
                spin->setDecimals(3);
                spin->setSingleStep(0.1);
            } else {
                spin->setRange(p.minimum, 1e7);
                spin->setDecimals(0);
                spin->setSingleStep(10);
            }
            row->addWidget(spin);
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, key = p.key] {
                // Leaving the field fires too, with the value it showed.
                if (spin->parent() == fields_ && spin->isEnabled() && spin->value() != spin->property("shown").toDouble())
                    apply(key, spin->value());
            });
            if (!p.automatic.isEmpty()) {
                auto* automatic = new QCheckBox(p.automatic, fields_);
                automatic->setObjectName(key + QLatin1String(kAutoSuffix));
                automatic->setFocusPolicy(Qt::NoFocus);
                row->addWidget(automatic);
                connect(automatic, &QCheckBox::clicked, this, [this, spin, key = p.key](bool on) {
                    if (spin->parent() == fields_)
                        apply(key, on ? kAuto : spin->value());
                });
            }
        }
        layout_->insertWidget(2, fields_);
    }

    // The values, except in the field being typed into.
    for (const Property& p : props) {
        const QString key = QString::fromStdString(p.key);
        if (auto* combo = fields_->findChild<QComboBox*>(key)) {
            const QSignalBlocker block(combo);
            combo->setCurrentIndex(static_cast<int>(std::lround(p.value)));
            combo->setProperty("shown", combo->currentIndex());
        } else if (auto* spin = fields_->findChild<QDoubleSpinBox*>(key)) {
            const bool automatic = std::isnan(p.value);
            if (auto* box = fields_->findChild<QCheckBox*>(key + QLatin1String(kAutoSuffix)))
                box->setChecked(automatic);
            spin->setEnabled(!automatic);
            if (spin->hasFocus())
                continue;
            const QSignalBlocker block(spin);
            if (!automatic) {
                spin->setValue(p.value);
                spin->setProperty("shown", spin->value());
            } else {
                // Something to start from when the box is unticked.
                const Story* story = engine_.document().story(engine_.document().currentStory());
                spin->setValue(p.key == "base" ? toolSettings().wallHeight : story ? story->height : 3000.0);
            }
        }
    }
}

void InfoBox::apply(const std::string& key, double value)
{
    Document& doc = engine_.document();
    const Entity* e = object_ ? doc.entity(*object_) : nullptr;
    if (e) {
        const auto shape = withProperty(e->shape, key, value);
        if (!shape)
            return;
        doc.beginTransaction(tr("Modifica %1").arg(shapeTitle(e->shape).toLower()).toStdString());
        doc.replace(e->id, *shape);
        doc.commitTransaction();
        engine_.documentChanged();
        return;
    }
    if (setToolProperty(tool_, key, value))
        rebuild();
}
