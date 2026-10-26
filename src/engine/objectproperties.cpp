// SPDX-License-Identifier: GPL-3.0-or-later
#include "objectproperties.h"

#include "coordinput.h"
#include "editing.h"
#include "hatch.h"
#include "properties.h"
#include "stylenames.h"

#include <QCoreApplication>

#include <cmath>
#include <algorithm>
#include <functional>
#include <optional>

namespace cad {

namespace {

QString t(const char* text) { return QCoreApplication::translate("ObjectProperties", text); }

QString general() { return t("Generale"); }
QString geometry() { return t("Geometria"); }
QString textGroup() { return t("Testo"); }

// A value read from a field: a number, a choice's index or a text.
struct Input {
    QString text;
    std::optional<double> number;
};

// One field of one object: how to read it into the palette and how to
// write a new value back into the shape.
struct Field {
    ObjectProperty property;
    std::function<std::optional<Shape>(const Shape&, const Input&)> set;
};

ObjectProperty number(const char* key, const QString& group, const QString& label, double value,
                      ObjectProperty::Unit unit = ObjectProperty::Unit::Length, bool readOnly = false)
{
    ObjectProperty p;
    p.key = key;
    p.group = group;
    p.label = label;
    p.number = value;
    p.unit = unit;
    p.readOnly = readOnly;
    return p;
}

ObjectProperty choice(const char* key, const QString& group, const QString& label, int index, const QStringList& choices)
{
    ObjectProperty p;
    p.key = key;
    p.group = group;
    p.label = label;
    p.type = ObjectProperty::Type::Choice;
    p.number = index;
    p.choices = choices;
    return p;
}

ObjectProperty text(const char* key, const QString& group, const QString& label, const QString& value)
{
    ObjectProperty p;
    p.key = key;
    p.group = group;
    p.label = label;
    p.type = ObjectProperty::Type::Text;
    p.text = value;
    return p;
}

QStringList alignments() { return {t("Sinistra"), t("Centro"), t("Destra")}; }

// Fields that only set one number of a shape of type S.
template <typename S>
Field numberField(ObjectProperty p, std::function<bool(S&, double)> apply)
{
    return {std::move(p), [apply](const Shape& shape, const Input& in) -> std::optional<Shape> {
                S s = std::get<S>(shape);
                if (!in.number || !apply(s, *in.number))
                    return std::nullopt;
                return Shape{s};
            }};
}

Field readOnly(ObjectProperty p)
{
    p.readOnly = true;
    return {std::move(p), nullptr};
}

std::vector<Field> fieldsOf(const Shape& shape)
{
    using U = ObjectProperty::Unit;
    std::vector<Field> f;
    const QString g = geometry();
    if (const auto* l = std::get_if<Line>(&shape)) {
        f.push_back(numberField<Line>(number("startX", g, t("Inizio X"), l->a.x), [](Line& s, double v) { s.a.x = v; return true; }));
        f.push_back(numberField<Line>(number("startY", g, t("Inizio Y"), l->a.y), [](Line& s, double v) { s.a.y = v; return true; }));
        f.push_back(numberField<Line>(number("endX", g, t("Fine X"), l->b.x), [](Line& s, double v) { s.b.x = v; return true; }));
        f.push_back(numberField<Line>(number("endY", g, t("Fine Y"), l->b.y), [](Line& s, double v) { s.b.y = v; return true; }));
        // The length keeps the start point and the direction.
        f.push_back(numberField<Line>(number("length", g, t("Lunghezza"), distance(l->a, l->b)), [](Line& s, double v) {
            const Vec2 u = normalized(s.b - s.a);
            if (v <= 0.0 || length(u) < kEps)
                return false;
            s.b = s.a + u * v;
            return true;
        }));
        f.push_back(readOnly(number("angle", g, t("Angolo"), radToDeg(normalizeAngle(angleOf(l->b - l->a))), U::Angle)));
    } else if (const auto* c = std::get_if<Circle>(&shape)) {
        f.push_back(numberField<Circle>(number("centerX", g, t("Centro X"), c->center.x), [](Circle& s, double v) { s.center.x = v; return true; }));
        f.push_back(numberField<Circle>(number("centerY", g, t("Centro Y"), c->center.y), [](Circle& s, double v) { s.center.y = v; return true; }));
        f.push_back(numberField<Circle>(number("radius", g, t("Raggio"), c->radius), [](Circle& s, double v) { s.radius = v; return v > 0.0; }));
        f.push_back(numberField<Circle>(number("diameter", g, t("Diametro"), 2.0 * c->radius), [](Circle& s, double v) { s.radius = v / 2.0; return v > 0.0; }));
        f.push_back(readOnly(number("circumference", g, t("Circonferenza"), 2.0 * kPi * c->radius)));
        f.push_back(readOnly(number("area", g, t("Area"), kPi * c->radius * c->radius, U::Area)));
    } else if (const auto* a = std::get_if<Arc>(&shape)) {
        f.push_back(numberField<Arc>(number("centerX", g, t("Centro X"), a->center.x), [](Arc& s, double v) { s.center.x = v; return true; }));
        f.push_back(numberField<Arc>(number("centerY", g, t("Centro Y"), a->center.y), [](Arc& s, double v) { s.center.y = v; return true; }));
        f.push_back(numberField<Arc>(number("radius", g, t("Raggio"), a->radius), [](Arc& s, double v) { s.radius = v; return v > 0.0; }));
        f.push_back(numberField<Arc>(number("startAngle", g, t("Angolo iniziale"), radToDeg(normalizeAngle(a->startAngle)), U::Angle),
                                     [](Arc& s, double v) { s.startAngle = degToRad(v); return true; }));
        f.push_back(numberField<Arc>(number("endAngle", g, t("Angolo finale"), radToDeg(normalizeAngle(a->endAngle)), U::Angle),
                                     [](Arc& s, double v) { s.endAngle = degToRad(v); return true; }));
        f.push_back(readOnly(number("arcLength", g, t("Lunghezza arco"), a->radius * a->sweep())));
    } else if (const auto* pl = std::get_if<Polyline>(&shape)) {
        f.push_back(readOnly(number("vertices", g, t("Vertici"), double(pl->points.size()), U::Count)));
        ObjectProperty closed = choice("closed", g, t("Chiusa"), pl->closed ? 1 : 0, {t("No"), t("Sì")});
        f.push_back({closed, [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         Polyline r = std::get<Polyline>(s);
                         if (!in.number || r.points.size() < 3)
                             return std::nullopt;
                         r.closed = *in.number > 0.5;
                         return Shape{r};
                     }});
        f.push_back(readOnly(number("length", g, t("Lunghezza"), shapeLength(shape))));
        if (pl->closed)
            f.push_back(readOnly(number("area", g, t("Area"), polylineArea(*pl), U::Area)));
    } else if (const auto* p = std::get_if<Point>(&shape)) {
        f.push_back(numberField<Point>(number("x", g, t("Posizione X"), p->position.x), [](Point& s, double v) { s.position.x = v; return true; }));
        f.push_back(numberField<Point>(number("y", g, t("Posizione Y"), p->position.y), [](Point& s, double v) { s.position.y = v; return true; }));
    } else if (const auto* tx = std::get_if<Text>(&shape)) {
        const QString tg = textGroup();
        f.push_back({text("content", tg, t("Contenuto"), QString::fromStdString(tx->content)),
                     [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         Text r = std::get<Text>(s);
                         if (in.text.isEmpty())
                             return std::nullopt;
                         r.content = in.text.toStdString();
                         return Shape{r};
                     }});
        f.push_back(numberField<Text>(number("height", tg, t("Altezza"), tx->height), [](Text& s, double v) { s.height = v; return v > 0.0; }));
        f.push_back(numberField<Text>(number("rotation", tg, t("Rotazione"), radToDeg(normalizeAngle(tx->rotation)), U::Angle),
                                      [](Text& s, double v) { s.rotation = degToRad(v); return true; }));
        f.push_back({choice("align", tg, t("Giustificazione"), static_cast<int>(tx->align), alignments()),
                     [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         Text r = std::get<Text>(s);
                         if (!in.number || *in.number < 0 || *in.number > 2)
                             return std::nullopt;
                         r.align = static_cast<Text::Align>(std::lround(*in.number));
                         return Shape{r};
                     }});
        f.push_back(numberField<Text>(number("x", g, t("Posizione X"), tx->position.x), [](Text& s, double v) { s.position.x = v; return true; }));
        f.push_back(numberField<Text>(number("y", g, t("Posizione Y"), tx->position.y), [](Text& s, double v) { s.position.y = v; return true; }));
    } else if (const auto* m = std::get_if<MText>(&shape)) {
        const QString tg = textGroup();
        f.push_back({text("content", tg, t("Contenuto"), QString::fromStdString(m->content)),
                     [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         MText r = std::get<MText>(s);
                         if (in.text.isEmpty())
                             return std::nullopt;
                         r.content = in.text.toStdString();
                         return Shape{r};
                     }});
        f.push_back(numberField<MText>(number("height", tg, t("Altezza"), m->height), [](MText& s, double v) { s.height = v; return v > 0.0; }));
        f.push_back(numberField<MText>(number("width", tg, t("Larghezza"), m->width), [](MText& s, double v) { s.width = v; return v >= 0.0; }));
        f.push_back(numberField<MText>(number("rotation", tg, t("Rotazione"), radToDeg(normalizeAngle(m->rotation)), U::Angle),
                                       [](MText& s, double v) { s.rotation = degToRad(v); return true; }));
        f.push_back(numberField<MText>(number("lineSpacing", tg, t("Interlinea"), m->lineSpacing, U::Factor),
                                       [](MText& s, double v) { s.lineSpacing = v; return v > 0.0; }));
        f.push_back(numberField<MText>(number("x", g, t("Posizione X"), m->position.x), [](MText& s, double v) { s.position.x = v; return true; }));
        f.push_back(numberField<MText>(number("y", g, t("Posizione Y"), m->position.y), [](MText& s, double v) { s.position.y = v; return true; }));
    } else if (const auto* d = std::get_if<Dimension>(&shape)) {
        const QString tg = textGroup();
        f.push_back(readOnly(number("measurement", tg, t("Misura"), d->measurement(),
                                    d->kind == Dimension::Kind::Angular ? U::Angle : U::Length)));
        // Empty text: back to the measured value.
        f.push_back({text("dimText", tg, t("Testo sostitutivo"), QString::fromStdString(d->text)),
                     [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         Dimension r = std::get<Dimension>(s);
                         r.text = in.text.toStdString();
                         return Shape{r};
                     }});
        f.push_back(numberField<Dimension>(number("textHeight", tg, t("Altezza testo"), d->textHeight),
                                           [](Dimension& s, double v) { s.textHeight = v; return v > 0.0; }));
        f.push_back(numberField<Dimension>(number("arrowSize", tg, t("Dimensione frecce"), d->arrowSize),
                                           [](Dimension& s, double v) { s.arrowSize = v; return v >= 0.0; }));
    } else if (const auto* h = std::get_if<Hatch>(&shape)) {
        const QString pg = t("Motivo");
        ObjectProperty pattern = text("pattern", pg, t("Nome"), QString::fromStdString(h->pattern));
        for (const std::string& n : hatchPatternNames())
            pattern.choices << QString::fromStdString(n);
        f.push_back({pattern, [](const Shape& s, const Input& in) -> std::optional<Shape> {
                         const Hatch& old = std::get<Hatch>(s);
                         const std::string name = in.text.trimmed().toUpper().toStdString();
                         if (!hatchPattern(name))
                             return std::nullopt;
                         Hatch r = makeHatch(old.loops, name, old.scale, old.angle);
                         r.origin = old.origin;
                         return Shape{r};
                     }});
        f.push_back(numberField<Hatch>(number("scale", pg, t("Scala"), h->scale, U::Factor), [](Hatch& s, double v) { s.scale = v; return v > 0.0; }));
        f.push_back(numberField<Hatch>(number("hatchAngle", pg, t("Angolo"), radToDeg(normalizeAngle(h->angle)), U::Angle),
                                       [](Hatch& s, double v) { s.angle = degToRad(v); return true; }));
        if (const auto area = shapeArea(shape))
            f.push_back(readOnly(number("area", g, t("Area"), *area, U::Area)));
    } else {
        // Walls, slabs, roofs, stairs and blocks: the parameters of the Info Box.
        const QString group = shapeTitle(shape);
        for (const Property& p : shapeProperties(shape)) {
            ObjectProperty op;
            op.key = p.key;
            op.group = group;
            op.label = p.label;
            op.number = p.value;
            if (p.type == Property::Type::Choice) {
                op.type = ObjectProperty::Type::Choice;
                op.choices = p.choices;
            } else {
                op.unit = p.type == Property::Type::Angle ? U::Angle : p.type == Property::Type::Factor ? U::Factor : U::Length;
            }
            const std::string key = p.key;
            f.push_back({op, [key](const Shape& s, const Input& in) -> std::optional<Shape> {
                             return in.number ? withProperty(s, key, *in.number) : std::nullopt;
                         }});
        }
        if (const auto* w = std::get_if<Wall>(&shape))
            f.push_back(readOnly(number("wallLength", group, t("Lunghezza"), wallLength(*w))));
    }
    return f;
}

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

QString kindName(const Shape& shape)
{
    return std::visit(Overloaded{
                          [](const Line&) { return t("Linea"); },
                          [](const Circle&) { return t("Cerchio"); },
                          [](const Arc&) { return t("Arco"); },
                          [](const Polyline&) { return t("Polilinea"); },
                          [](const Text&) { return t("Testo"); },
                          [](const MText&) { return t("Testo multilinea"); },
                          [](const Dimension&) { return t("Quota"); },
                          [](const Hatch&) { return t("Tratteggio"); },
                          [](const Wall&) { return t("Muro"); },
                          [](const Slab&) { return t("Solaio"); },
                          [](const Roof&) { return t("Tetto"); },
                          [](const Stair&) { return t("Scala"); },
                          [](const Insert&) { return t("Blocco"); },
                          [](const Point&) { return t("Punto"); },
                      },
                      shape);
}

std::vector<const Entity*> entitiesOf(const Document& doc, const std::vector<EntityId>& ids)
{
    std::vector<const Entity*> out;
    for (EntityId id : ids)
        if (const Entity* e = doc.entity(id))
            out.push_back(e);
    return out;
}

bool sameKind(const std::vector<const Entity*>& es)
{
    for (const Entity* e : es)
        if (e->shape.index() != es.front()->shape.index())
            return false;
    return true;
}

QStringList lineweightChoices()
{
    QStringList out{t("DaLayer"), t("Predefinito")};
    for (int w : standardLineweights())
        out << lineweightName(w);
    return out;
}

QStringList colorChoices()
{
    return {t("DaLayer"), t("Rosso"), t("Giallo"), t("Verde"), t("Ciano"), t("Blu"), t("Magenta"), t("Bianco")};
}

// The look of an entity as palette fields; set through Document::restyle.
std::vector<ObjectProperty> generalOf(const Document& doc, const Entity& e)
{
    std::vector<ObjectProperty> out;
    ObjectProperty layer = choice("layer", general(), t("Layer"), 0, {});
    for (size_t i = 0; i < doc.layers().size(); ++i) {
        layer.choices << QString::fromStdString(doc.layers()[i].name);
        if (doc.layers()[i].name == e.layer)
            layer.number = double(i);
    }
    out.push_back(layer);
    ObjectProperty color = text("color", general(), t("Colore"), colorName(e.color));
    color.choices = colorChoices();
    out.push_back(color);
    ObjectProperty linetype = choice("linetype", general(), t("Tipo di linea"), 0, {t("DaLayer")});
    for (const Linetype& lt : doc.linetypes()) {
        linetype.choices << QString::fromStdString(lt.name);
        if (linetypeKey(lt.name) == linetypeKey(e.linetype))
            linetype.number = linetype.choices.size() - 1;
    }
    out.push_back(linetype);
    ObjectProperty weight = text("lineweight", general(), t("Spessore di linea"), lineweightName(e.lineweight));
    weight.choices = lineweightChoices();
    out.push_back(weight);
    if (doc.stories().size() > 1) {
        ObjectProperty story = choice("story", general(), t("Piano"), 0, {});
        for (size_t i = 0; i < doc.stories().size(); ++i) {
            story.choices << QString::fromStdString(doc.stories()[i].name);
            if (doc.stories()[i].id == e.story)
                story.number = double(i);
        }
        out.push_back(story);
    }
    return out;
}

bool sameValue(const ObjectProperty& a, const ObjectProperty& b)
{
    if (a.type == ObjectProperty::Type::Text)
        return a.text == b.text;
    return std::abs(a.number - b.number) <= 1e-9 * std::max(1.0, std::abs(a.number));
}

// Merges the fields of several objects: those all of them have, with
// *VARIE* where they disagree.
std::vector<ObjectProperty> merge(const std::vector<std::vector<ObjectProperty>>& lists)
{
    std::vector<ObjectProperty> out = lists.front();
    for (size_t i = 1; i < lists.size(); ++i) {
        std::vector<ObjectProperty> kept;
        for (ObjectProperty p : out) {
            const auto other = std::find_if(lists[i].begin(), lists[i].end(), [&](const ObjectProperty& q) { return q.key == p.key; });
            if (other == lists[i].end())
                continue;
            if (!sameValue(p, *other))
                p.varies = true;
            kept.push_back(p);
        }
        out = std::move(kept);
    }
    return out;
}

Input parseInput(const QString& value)
{
    Input in;
    in.text = value;
    QString n = value.trimmed();
    n.replace(QLatin1Char(','), QLatin1Char('.'));
    if (n.endsWith(QStringLiteral("°")))
        n.chop(1);
    in.number = parseNumber(n.toStdString());
    return in;
}

} // namespace

QString selectionTitle(const Document& doc, const std::vector<EntityId>& ids)
{
    const auto es = entitiesOf(doc, ids);
    if (es.empty())
        return t("Nessuna selezione");
    if (es.size() == 1)
        return kindName(es.front()->shape);
    if (sameKind(es))
        return t("%1 (%2)").arg(kindName(es.front()->shape)).arg(es.size());
    return t("Tutti (%1)").arg(es.size());
}

std::vector<ObjectProperty> objectProperties(const Document& doc, const std::vector<EntityId>& ids)
{
    const auto es = entitiesOf(doc, ids);
    if (es.empty())
        return {};
    std::vector<std::vector<ObjectProperty>> lists;
    const bool geometryToo = sameKind(es);
    for (const Entity* e : es) {
        std::vector<ObjectProperty> list = generalOf(doc, *e);
        if (geometryToo)
            for (const Field& f : fieldsOf(e->shape))
                list.push_back(f.property);
        lists.push_back(std::move(list));
    }
    return merge(lists);
}

int setObjectProperty(Document& doc, const std::vector<EntityId>& ids, const std::string& key, const QString& value)
{
    const auto es = entitiesOf(doc, ids);
    const Input in = parseInput(value);
    // Work out every change before making any, so a bad value changes nothing.
    std::vector<std::pair<EntityId, Shape>> shapes;
    std::vector<std::tuple<EntityId, std::string, EntityStyle>> styles;
    std::vector<std::pair<EntityId, StoryId>> stories;
    for (const Entity* e : es) {
        EntityStyle style = styleOf(*e);
        std::string layer = e->layer;
        if (key == "layer") {
            if (!in.number || *in.number < 0 || *in.number >= double(doc.layers().size()))
                return 0;
            layer = doc.layers()[size_t(std::lround(*in.number))].name;
        } else if (key == "color") {
            const auto c = parseColor(value);
            if (!c)
                return 0;
            style.color = *c;
        } else if (key == "linetype") {
            if (!in.number || *in.number < 0 || *in.number > double(doc.linetypes().size()))
                return 0;
            const size_t i = size_t(std::lround(*in.number));
            style.linetype = i == 0 ? std::string(kByLayer) : doc.linetypes()[i - 1].name;
        } else if (key == "lineweight") {
            const auto w = parseLineweight(value);
            if (!w)
                return 0;
            style.lineweight = *w;
        } else if (key == "story") {
            if (!in.number || *in.number < 0 || *in.number >= double(doc.stories().size()))
                return 0;
            const StoryId story = doc.stories()[size_t(std::lround(*in.number))].id;
            if (story != e->story)
                stories.emplace_back(e->id, story);
            continue;
        } else {
            for (const Field& f : fieldsOf(e->shape)) {
                if (f.property.key != key)
                    continue;
                if (!f.set)
                    return 0;
                const auto shape = f.set(e->shape, in);
                if (!shape)
                    return 0;
                shapes.emplace_back(e->id, *shape);
            }
            continue;
        }
        if (layer != e->layer || !(style == styleOf(*e)))
            styles.emplace_back(e->id, layer, style);
    }
    const int changed = int(shapes.size() + styles.size() + stories.size());
    if (changed == 0)
        return 0;
    doc.beginTransaction(t("Proprietà").toStdString());
    for (const auto& [id, shape] : shapes)
        doc.replace(id, shape);
    for (const auto& [id, layer, style] : styles)
        doc.restyle(id, layer, style);
    for (const auto& [id, story] : stories) {
        Entity moved = *doc.entity(id);
        moved.story = story;
        doc.remove(id);
        doc.add(moved);
    }
    doc.commitTransaction();
    return changed;
}

} // namespace cad
