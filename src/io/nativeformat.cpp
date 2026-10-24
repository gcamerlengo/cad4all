// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativeformat.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <variant>

namespace cad::io {

namespace {

QJsonArray toJson(Vec2 p)
{
    return {p.x, p.y};
}

bool fromJson(const QJsonValue& v, Vec2& out)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble())
        return false;
    out = {a[0].toDouble(), a[1].toDouble()};
    return true;
}

QString colorToString(Color c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'));
}

Color colorFromString(const QString& s)
{
    bool ok = false;
    const uint v = s.mid(1).toUInt(&ok, 16);
    if (!s.startsWith(QLatin1Char('#')) || s.size() != 7 || !ok)
        return {};
    return {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
}

const char* alignName(Text::Align a)
{
    switch (a) {
    case Text::Align::Left: return "left";
    case Text::Align::Center: return "center";
    case Text::Align::Right: return "right";
    }
    return "left";
}

Text::Align alignFromName(const QString& s)
{
    if (s == QLatin1String("center"))
        return Text::Align::Center;
    if (s == QLatin1String("right"))
        return Text::Align::Right;
    return Text::Align::Left;
}

const char* kindName(Dimension::Kind k)
{
    switch (k) {
    case Dimension::Kind::Linear: return "linear";
    case Dimension::Kind::Aligned: return "aligned";
    case Dimension::Kind::Radius: return "radius";
    case Dimension::Kind::Diameter: return "diameter";
    }
    return "linear";
}

std::optional<Dimension::Kind> kindFromName(const QString& s)
{
    for (auto k : {Dimension::Kind::Linear, Dimension::Kind::Aligned, Dimension::Kind::Radius, Dimension::Kind::Diameter})
        if (s == QLatin1String(kindName(k)))
            return k;
    return std::nullopt;
}

QJsonObject polylineToJson(const Polyline& p)
{
    QJsonObject o;
    QJsonArray pts;
    for (Vec2 v : p.points)
        pts.append(toJson(v));
    o[QStringLiteral("points")] = pts;
    o[QStringLiteral("closed")] = p.closed;
    if (p.hasArcs()) {
        QJsonArray bulges;
        for (size_t i = 0; i < p.points.size(); ++i)
            bulges.append(p.bulge(i));
        o[QStringLiteral("bulges")] = bulges;
    }
    return o;
}

std::optional<Polyline> polylineFromJson(const QJsonObject& o)
{
    Polyline p;
    p.closed = o[QStringLiteral("closed")].toBool();
    for (const QJsonValue& v : o[QStringLiteral("points")].toArray()) {
        Vec2 pt;
        if (!fromJson(v, pt))
            return std::nullopt;
        p.points.push_back(pt);
    }
    for (const QJsonValue& v : o[QStringLiteral("bulges")].toArray())
        p.bulges.push_back(v.toDouble());
    p.bulges.resize(std::min(p.bulges.size(), p.points.size()));
    if (p.points.size() < 2)
        return std::nullopt;
    return p;
}

QJsonObject shapeToJson(const Shape& shape)
{
    QJsonObject o;
    if (const auto* l = std::get_if<Line>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("line");
        o[QStringLiteral("a")] = toJson(l->a);
        o[QStringLiteral("b")] = toJson(l->b);
    } else if (const auto* c = std::get_if<Circle>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("circle");
        o[QStringLiteral("center")] = toJson(c->center);
        o[QStringLiteral("radius")] = c->radius;
    } else if (const auto* a = std::get_if<Arc>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("arc");
        o[QStringLiteral("center")] = toJson(a->center);
        o[QStringLiteral("radius")] = a->radius;
        o[QStringLiteral("startAngle")] = a->startAngle;
        o[QStringLiteral("endAngle")] = a->endAngle;
    } else if (const auto* p = std::get_if<Polyline>(&shape)) {
        o = polylineToJson(*p);
        o[QStringLiteral("type")] = QStringLiteral("polyline");
    } else if (const auto* t = std::get_if<Text>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("text");
        o[QStringLiteral("position")] = toJson(t->position);
        o[QStringLiteral("height")] = t->height;
        o[QStringLiteral("rotation")] = t->rotation;
        o[QStringLiteral("align")] = QLatin1String(alignName(t->align));
        o[QStringLiteral("content")] = QString::fromStdString(t->content);
    } else if (const auto* d = std::get_if<Dimension>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("dimension");
        o[QStringLiteral("kind")] = QLatin1String(kindName(d->kind));
        o[QStringLiteral("p1")] = toJson(d->p1);
        o[QStringLiteral("p2")] = toJson(d->p2);
        o[QStringLiteral("location")] = toJson(d->location);
        o[QStringLiteral("angle")] = d->angle;
        o[QStringLiteral("textHeight")] = d->textHeight;
        o[QStringLiteral("arrowSize")] = d->arrowSize;
        if (!d->text.empty())
            o[QStringLiteral("text")] = QString::fromStdString(d->text);
    } else if (const auto* h = std::get_if<Hatch>(&shape)) {
        o[QStringLiteral("type")] = QStringLiteral("hatch");
        o[QStringLiteral("pattern")] = QString::fromStdString(h->pattern);
        o[QStringLiteral("scale")] = h->scale;
        o[QStringLiteral("angle")] = h->angle;
        o[QStringLiteral("origin")] = toJson(h->origin);
        QJsonArray loops;
        for (const Polyline& loop : h->loops)
            loops.append(polylineToJson(loop));
        o[QStringLiteral("loops")] = loops;
        // The definition is kept, so patterns read from other files survive.
        QJsonArray lines;
        for (const HatchPatternLine& l : h->lines) {
            QJsonObject lo;
            lo[QStringLiteral("angle")] = l.angle;
            lo[QStringLiteral("origin")] = toJson(l.origin);
            lo[QStringLiteral("offset")] = toJson(l.offset);
            QJsonArray dashes;
            for (double d : l.dashes)
                dashes.append(d);
            if (!dashes.isEmpty())
                lo[QStringLiteral("dashes")] = dashes;
            lines.append(lo);
        }
        o[QStringLiteral("lines")] = lines;
    }
    return o;
}

std::optional<Shape> shapeFromJson(const QJsonObject& o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("line")) {
        Line l;
        if (fromJson(o[QStringLiteral("a")], l.a) && fromJson(o[QStringLiteral("b")], l.b))
            return l;
    } else if (type == QLatin1String("circle")) {
        Circle c;
        c.radius = o[QStringLiteral("radius")].toDouble(-1.0);
        if (fromJson(o[QStringLiteral("center")], c.center) && c.radius > 0.0)
            return c;
    } else if (type == QLatin1String("arc")) {
        Arc a;
        a.radius = o[QStringLiteral("radius")].toDouble(-1.0);
        a.startAngle = o[QStringLiteral("startAngle")].toDouble();
        a.endAngle = o[QStringLiteral("endAngle")].toDouble();
        if (fromJson(o[QStringLiteral("center")], a.center) && a.radius > 0.0)
            return a;
    } else if (type == QLatin1String("polyline")) {
        return polylineFromJson(o);
    } else if (type == QLatin1String("text")) {
        Text t;
        t.height = o[QStringLiteral("height")].toDouble(-1.0);
        t.rotation = o[QStringLiteral("rotation")].toDouble();
        t.align = alignFromName(o[QStringLiteral("align")].toString());
        t.content = o[QStringLiteral("content")].toString().toStdString();
        if (fromJson(o[QStringLiteral("position")], t.position) && t.height > 0.0)
            return t;
    } else if (type == QLatin1String("dimension")) {
        Dimension d;
        const auto kind = kindFromName(o[QStringLiteral("kind")].toString());
        d.angle = o[QStringLiteral("angle")].toDouble();
        d.textHeight = o[QStringLiteral("textHeight")].toDouble(-1.0);
        d.arrowSize = o[QStringLiteral("arrowSize")].toDouble(-1.0);
        d.text = o[QStringLiteral("text")].toString().toStdString();
        if (kind && fromJson(o[QStringLiteral("p1")], d.p1) && fromJson(o[QStringLiteral("p2")], d.p2)
            && fromJson(o[QStringLiteral("location")], d.location) && d.textHeight > 0.0 && d.arrowSize >= 0.0) {
            d.kind = *kind;
            return d;
        }
    } else if (type == QLatin1String("hatch")) {
        Hatch h;
        h.pattern = o[QStringLiteral("pattern")].toString().toStdString();
        h.scale = o[QStringLiteral("scale")].toDouble(-1.0);
        h.angle = o[QStringLiteral("angle")].toDouble();
        fromJson(o[QStringLiteral("origin")], h.origin);
        for (const QJsonValue& v : o[QStringLiteral("loops")].toArray()) {
            if (auto loop = polylineFromJson(v.toObject())) {
                loop->closed = true;
                h.loops.push_back(std::move(*loop));
            }
        }
        for (const QJsonValue& v : o[QStringLiteral("lines")].toArray()) {
            const QJsonObject lo = v.toObject();
            HatchPatternLine l;
            l.angle = lo[QStringLiteral("angle")].toDouble();
            if (!fromJson(lo[QStringLiteral("origin")], l.origin) || !fromJson(lo[QStringLiteral("offset")], l.offset))
                return std::nullopt;
            for (const QJsonValue& d : lo[QStringLiteral("dashes")].toArray())
                l.dashes.push_back(d.toDouble());
            h.lines.push_back(std::move(l));
        }
        if (!h.loops.empty() && h.scale > 0.0)
            return h;
    }
    return std::nullopt;
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

} // namespace

QByteArray toNativeJson(const Document& doc)
{
    QJsonArray layers;
    for (const Layer& l : doc.layers()) {
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromStdString(l.name);
        o[QStringLiteral("color")] = colorToString(l.color);
        o[QStringLiteral("visible")] = l.visible;
        o[QStringLiteral("locked")] = l.locked;
        layers.append(o);
    }

    QJsonArray entities;
    for (const auto& [id, e] : doc.entities()) {
        QJsonObject o = shapeToJson(e.shape);
        o[QStringLiteral("layer")] = QString::fromStdString(e.layer);
        entities.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("cad4all");
    root[QStringLiteral("version")] = kNativeFormatVersion;
    root[QStringLiteral("currentLayer")] = QString::fromStdString(doc.currentLayer());
    root[QStringLiteral("layers")] = layers;
    root[QStringLiteral("entities")] = entities;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromNativeJson(const QByteArray& data, Document& doc, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull()) {
        setError(error, QStringLiteral("File non valido: %1").arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("cad4all")) {
        setError(error, QStringLiteral("Il file non è un disegno CAD4All."));
        return false;
    }
    if (root.value(QStringLiteral("version")).toInt() > kNativeFormatVersion) {
        setError(error, QStringLiteral("Il disegno è stato salvato con una versione più recente di CAD4All."));
        return false;
    }

    Document loaded;
    for (const QJsonValue& v : root.value(QStringLiteral("layers")).toArray()) {
        const QJsonObject o = v.toObject();
        Layer l;
        l.name = o.value(QStringLiteral("name")).toString().toStdString();
        l.color = colorFromString(o.value(QStringLiteral("color")).toString());
        l.visible = o.value(QStringLiteral("visible")).toBool(true);
        l.locked = o.value(QStringLiteral("locked")).toBool(false);
        if (loaded.layer(l.name))
            loaded.updateLayer(l.name, l);
        else
            loaded.addLayer(l);
    }

    int skipped = 0;
    for (const QJsonValue& v : root.value(QStringLiteral("entities")).toArray()) {
        const QJsonObject o = v.toObject();
        const auto shape = shapeFromJson(o);
        if (!shape) {
            ++skipped;
            continue;
        }
        Entity e;
        e.layer = o.value(QStringLiteral("layer")).toString(QStringLiteral("0")).toStdString();
        e.shape = *shape;
        loaded.add(std::move(e));
    }
    loaded.setCurrentLayer(root.value(QStringLiteral("currentLayer")).toString(QStringLiteral("0")).toStdString());
    loaded.clearHistory();
    loaded.setModified(false);

    doc = std::move(loaded);
    if (skipped > 0)
        setError(error, QStringLiteral("%1 oggetti non riconosciuti sono stati ignorati.").arg(skipped));
    return true;
}

bool saveNative(const Document& doc, const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.write(toNativeJson(doc));
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

bool loadNative(Document& doc, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }
    return fromNativeJson(file.readAll(), doc, error);
}

} // namespace cad::io
