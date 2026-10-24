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
        o[QStringLiteral("type")] = QStringLiteral("polyline");
        QJsonArray pts;
        for (Vec2 v : p->points)
            pts.append(toJson(v));
        o[QStringLiteral("points")] = pts;
        o[QStringLiteral("closed")] = p->closed;
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
        Polyline p;
        p.closed = o[QStringLiteral("closed")].toBool();
        for (const QJsonValue& v : o[QStringLiteral("points")].toArray()) {
            Vec2 pt;
            if (!fromJson(v, pt))
                return std::nullopt;
            p.points.push_back(pt);
        }
        if (p.points.size() >= 2)
            return p;
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
