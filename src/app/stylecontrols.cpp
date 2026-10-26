// SPDX-License-Identifier: GPL-3.0-or-later
#include "stylecontrols.h"

#include "stylenames.h"

#include <QColorDialog>
#include <QComboBox>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include <optional>

using namespace cad;

namespace {

constexpr int kOtherColor = 1;  // item data of "Altro colore..."

QIcon swatch(const QColor& color)
{
    // Twice the size, so it stays sharp on high density screens.
    QPixmap pm(28, 28);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::gray);
    p.setBrush(color);
    p.drawRect(0, 0, 13, 13);
    return QIcon(pm);
}

QComboBox* combo(QWidget* parent, const QString& tip, int width)
{
    auto* c = new QComboBox(parent);
    c->setToolTip(tip);
    c->setFocusPolicy(Qt::ClickFocus);
    c->setMinimumWidth(width);
    c->setMaxVisibleItems(20);
    return c;
}

// The value all the selected objects share, if they do.
template <class T, class Get>
std::optional<T> common(const CommandEngine& engine, Get get)
{
    std::optional<T> value;
    for (EntityId id : engine.selection()) {
        const Entity* e = engine.document().entity(id);
        if (!e)
            continue;
        const T v = get(*e);
        if (value && !(*value == v))
            return std::nullopt;
        value = v;
    }
    return value;
}

} // namespace

StyleControls::StyleControls(CommandEngine& engine, QWidget* parent)
    : QObject(parent), engine_(engine), parent_(parent)
{
    layer_ = combo(parent, tr("Layer"), 140);
    color_ = combo(parent, tr("Colore"), 120);
    linetype_ = combo(parent, tr("Tipo di linea"), 120);
    lineweight_ = combo(parent, tr("Spessore di linea"), 120);
    connect(layer_, &QComboBox::activated, this, &StyleControls::layerChosen);
    connect(color_, &QComboBox::activated, this, &StyleControls::colorChosen);
    connect(linetype_, &QComboBox::activated, this, &StyleControls::linetypeChosen);
    connect(lineweight_, &QComboBox::activated, this, &StyleControls::lineweightChosen);
    connect(&engine_, &CommandEngine::changed, this, [this] { refresh(); });
    connect(&engine_, &CommandEngine::selectionChanged, this, [this] { refresh(); });
    refresh();
}

void StyleControls::refresh(bool force)
{
    const Document& doc = engine_.document();
    if (!force && shown_ && shownRevision_ == doc.revision() && shownSelection_ == engine_.selection())
        return;
    shown_ = true;
    shownRevision_ = doc.revision();
    shownSelection_ = engine_.selection();
    const bool selected = !engine_.selection().empty();
    const EntityStyle current = doc.currentStyle();

    // Layers.
    {
        const QSignalBlocker block(layer_);
        layer_->clear();
        for (const Layer& l : doc.layers())
            layer_->addItem(swatch(QColor(l.color.r, l.color.g, l.color.b)), QString::fromStdString(l.name), QString::fromStdString(l.name));
        const auto layer = selected ? common<std::string>(engine_, [](const Entity& e) { return e.layer; })
                                    : std::optional<std::string>(doc.currentLayer());
        layer_->setCurrentIndex(layer ? layer_->findData(QString::fromStdString(*layer)) : -1);
    }

    // Colours: DaLayer, the seven basic ones, the value shown if it is another, and the dialog.
    {
        const QSignalBlocker block(color_);
        color_->clear();
        color_->addItem(colorName(std::nullopt));
        for (int aci = 1; aci <= 7; ++aci) {
            const Color c = aciToColor(aci);
            color_->addItem(swatch(QColor(c.r, c.g, c.b)), colorName(c));
        }
        const auto color = selected ? common<std::optional<Color>>(engine_, [](const Entity& e) { return e.color; })
                                    : std::optional<std::optional<Color>>(current.color);
        int index = -1;
        if (color) {
            index = color_->findText(colorName(*color));
            if (index < 0) {
                const Color c = **color;
                color_->addItem(swatch(QColor(c.r, c.g, c.b)), colorName(c));
                index = color_->count() - 1;
            }
        }
        color_->addItem(tr("Altro colore..."), kOtherColor);
        color_->setCurrentIndex(index);
    }

    // Linetypes.
    {
        const QSignalBlocker block(linetype_);
        linetype_->clear();
        linetype_->addItem(linetypeName(kByLayer), QString::fromStdString(kByLayer));
        for (const Linetype& lt : doc.linetypes()) {
            const QString name = QString::fromStdString(lt.name);
            linetype_->addItem(name, name);
            linetype_->setItemData(linetype_->count() - 1, QString::fromStdString(lt.description), Qt::ToolTipRole);
        }
        const auto type = selected ? common<std::string>(engine_, [](const Entity& e) { return linetypeKey(e.linetype); })
                                   : std::optional<std::string>(linetypeKey(current.linetype));
        linetype_->setCurrentIndex(type ? linetype_->findData(QString::fromStdString(*type)) : -1);
    }

    // Lineweights.
    {
        const QSignalBlocker block(lineweight_);
        lineweight_->clear();
        for (int w : {kLineweightByLayer, kLineweightDefault})
            lineweight_->addItem(lineweightName(w), w);
        for (int w : standardLineweights())
            lineweight_->addItem(lineweightName(w), w);
        const auto weight = selected ? common<int>(engine_, [](const Entity& e) { return e.lineweight; })
                                     : std::optional<int>(current.lineweight);
        lineweight_->setCurrentIndex(weight ? lineweight_->findData(*weight) : -1);
    }
}

void StyleControls::change(const std::function<void(Entity&)>& entity, const std::function<void(Document&)>& current)
{
    Document& doc = engine_.document();
    if (engine_.selection().empty()) {
        current(doc);
    } else {
        doc.beginTransaction("PROPRIETA");
        for (EntityId id : engine_.selection()) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            Entity updated = *e;
            entity(updated);
            doc.restyle(id, updated.layer, styleOf(updated));
        }
        doc.commitTransaction();
    }
    engine_.documentChanged();
}

void StyleControls::layerChosen(int index)
{
    const std::string name = layer_->itemData(index).toString().toStdString();
    change([&](Entity& e) { e.layer = name; }, [&](Document& doc) { doc.setCurrentLayer(name); });
}

void StyleControls::colorChosen(int index)
{
    std::optional<Color> color;
    if (color_->itemData(index).toInt() == kOtherColor) {
        const QColor chosen = QColorDialog::getColor(Qt::white, parent_, tr("Colore"));
        if (!chosen.isValid()) {
            refresh(true);
            return;
        }
        color = Color{static_cast<std::uint8_t>(chosen.red()), static_cast<std::uint8_t>(chosen.green()),
                      static_cast<std::uint8_t>(chosen.blue())};
    } else if (const auto parsed = parseColor(color_->itemText(index))) {
        color = *parsed;
    } else {
        return;
    }
    change([&](Entity& e) { e.color = color; },
           [&](Document& doc) {
               EntityStyle s = doc.currentStyle();
               s.color = color;
               doc.setCurrentStyle(s);
           });
}

void StyleControls::linetypeChosen(int index)
{
    const std::string name = linetype_->itemData(index).toString().toStdString();
    change([&](Entity& e) { e.linetype = name; },
           [&](Document& doc) {
               EntityStyle s = doc.currentStyle();
               s.linetype = name;
               doc.setCurrentStyle(s);
           });
}

void StyleControls::lineweightChosen(int index)
{
    const int weight = lineweight_->itemData(index).toInt();
    change([&](Entity& e) { e.lineweight = weight; },
           [&](Document& doc) {
               EntityStyle s = doc.currentStyle();
               s.lineweight = weight;
               doc.setCurrentStyle(s);
           });
}
