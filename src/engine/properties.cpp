// SPDX-License-Identifier: GPL-3.0-or-later
#include "properties.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>
#include <tuple>

namespace cad {

namespace {

QString t(const char* text) { return QCoreApplication::translate("Properties", text); }

const double kAuto = std::numeric_limits<double>::quiet_NaN();

Property length(const char* key, const QString& label, double value, double minimum = 1.0)
{
    Property p;
    p.key = key;
    p.label = label;
    p.value = value;
    p.minimum = minimum;
    return p;
}

Property choice(const char* key, const QString& label, int index, const QStringList& choices)
{
    Property p;
    p.key = key;
    p.label = label;
    p.type = Property::Type::Choice;
    p.value = index;
    p.choices = choices;
    return p;
}

QStringList justifications() { return {t("Centro"), t("Sinistra"), t("Destra")}; }
QStringList roofStyles() { return {t("Padiglione"), t("Capanna"), t("Falda"), t("Piano")}; }
QStringList stairKinds() { return {t("Dritta"), t("L"), t("U")}; }
QStringList turns() { return {t("Sinistra"), t("Destra")}; }

int index(double v) { return static_cast<int>(std::lround(v)); }

Property pitch(double degrees)
{
    Property p = length("pitch", t("Pendenza"), degrees, 0.0);
    p.type = Property::Type::Angle;
    p.maximum = 89.0;
    return p;
}

// The style a roof was made with, if its gables still match one.
std::optional<RoofStyle> roofStyleOf(const Roof& r)
{
    const auto g = roofGeometry(r);
    if (!g)
        return std::nullopt;
    std::vector<bool> gables(g->footprint.size());
    for (size_t i = 0; i < gables.size(); ++i)
        gables[i] = r.gable(i);
    for (RoofStyle s : {RoofStyle::Hip, RoofStyle::Gable, RoofStyle::Shed, RoofStyle::Flat})
        if (roofGables(g->footprint, s) == gables)
            return s;
    return std::nullopt;
}

} // namespace

ToolSettings& toolSettings()
{
    static ToolSettings settings;
    return settings;
}

int stairRisers(double height) { return std::max(2, static_cast<int>(std::ceil(height / kMaxRiser - 1e-9))); }

bool hasToolProperties(const QString& c)
{
    return c == QLatin1String("WALL") || c == QLatin1String("DOOR") || c == QLatin1String("WINDOW")
        || c == QLatin1String("SLAB") || c == QLatin1String("ROOF") || c == QLatin1String("STAIR");
}

QString toolTitle(const QString& c)
{
    if (c == QLatin1String("WALL"))
        return t("Muro");
    if (c == QLatin1String("DOOR"))
        return t("Porta");
    if (c == QLatin1String("WINDOW"))
        return t("Finestra");
    if (c == QLatin1String("SLAB"))
        return t("Solaio");
    if (c == QLatin1String("ROOF"))
        return t("Tetto");
    if (c == QLatin1String("STAIR"))
        return t("Scala");
    return {};
}

std::vector<Property> toolProperties(const QString& c)
{
    const ToolSettings& s = toolSettings();
    std::vector<Property> out;
    if (c == QLatin1String("WALL")) {
        out.push_back(length("thickness", t("Spessore"), s.wallThickness));
        out.push_back(length("height", t("Altezza"), s.wallHeight));
        out.push_back(choice("justification", t("Giustificazione"), static_cast<int>(s.wallJustification), justifications()));
    } else if (c == QLatin1String("DOOR") || c == QLatin1String("WINDOW")) {
        const Opening& o = c == QLatin1String("DOOR") ? s.door : s.window;
        out.push_back(length("width", t("Larghezza"), o.width));
        out.push_back(length("height", t("Altezza"), o.height));
        if (c == QLatin1String("WINDOW"))
            out.push_back(length("sill", t("Davanzale"), o.sill, 0.0));
    } else if (c == QLatin1String("SLAB")) {
        out.push_back(length("thickness", t("Spessore"), s.slabThickness));
        out.push_back(length("offset", t("Quota"), s.slabOffset, -1e9));
    } else if (c == QLatin1String("ROOF")) {
        out.push_back(choice("style", t("Tipo"), static_cast<int>(s.roofStyle), roofStyles()));
        out.push_back(pitch(s.roofPitch));
        out.push_back(length("overhang", t("Sporto"), s.roofOverhang, 0.0));
        out.push_back(length("thickness", t("Spessore"), s.roofThickness));
        Property base = length("base", t("Imposta"), s.roofBase.value_or(kAuto), -1e9);
        base.automatic = t("in cima ai muri");
        out.push_back(base);
    } else if (c == QLatin1String("STAIR")) {
        out.push_back(choice("kind", t("Tipo"), static_cast<int>(s.stairKind), stairKinds()));
        out.push_back(choice("turn", t("Verso"), s.stairTurnRight ? 1 : 0, turns()));
        out.push_back(length("width", t("Larghezza"), s.stairWidth));
        out.push_back(length("going", t("Pedata"), s.stairGoing));
        Property height = length("height", t("Altezza"), s.stairHeight.value_or(kAuto));
        height.automatic = t("altezza del piano");
        out.push_back(height);
    }
    return out;
}

bool setToolProperty(const QString& c, const std::string& key, double v)
{
    ToolSettings& s = toolSettings();
    const bool automatic = std::isnan(v);
    if (!automatic && key != "offset" && key != "base" && v < 0.0)
        return false;
    if (c == QLatin1String("WALL")) {
        if (key == "thickness" && v > 0.0)
            s.wallThickness = v;
        else if (key == "height" && v > 0.0)
            s.wallHeight = v;
        else if (key == "justification" && index(v) >= 0 && index(v) <= 2)
            s.wallJustification = static_cast<Wall::Justification>(index(v));
        else
            return false;
        return true;
    }
    if (c == QLatin1String("DOOR") || c == QLatin1String("WINDOW")) {
        Opening& o = c == QLatin1String("DOOR") ? s.door : s.window;
        if (key == "width" && v > 0.0)
            o.width = v;
        else if (key == "height" && v > 0.0)
            o.height = v;
        else if (key == "sill")
            o.sill = v;
        else
            return false;
        return true;
    }
    if (c == QLatin1String("SLAB")) {
        if (key == "thickness" && v > 0.0)
            s.slabThickness = v;
        else if (key == "offset" && !automatic)
            s.slabOffset = v;
        else
            return false;
        return true;
    }
    if (c == QLatin1String("ROOF")) {
        if (key == "style" && index(v) >= 0 && index(v) <= 3)
            s.roofStyle = static_cast<RoofStyle>(index(v));
        else if (key == "pitch" && v < 80.0)
            s.roofPitch = v;
        else if (key == "overhang")
            s.roofOverhang = v;
        else if (key == "thickness" && v > 0.0)
            s.roofThickness = v;
        else if (key == "base")
            s.roofBase = automatic ? std::nullopt : std::optional<double>(v);
        else
            return false;
        return true;
    }
    if (c == QLatin1String("STAIR")) {
        if (key == "kind" && index(v) >= 0 && index(v) <= 2)
            s.stairKind = static_cast<Stair::Kind>(index(v));
        else if (key == "turn")
            s.stairTurnRight = index(v) == 1;
        else if (key == "width" && v > 0.0)
            s.stairWidth = v;
        else if (key == "going" && v > 0.0)
            s.stairGoing = v;
        else if (key == "height" && (automatic || v > 0.0))
            s.stairHeight = automatic ? std::nullopt : std::optional<double>(v);
        else
            return false;
        return true;
    }
    return false;
}

QString shapeTitle(const Shape& shape)
{
    if (std::holds_alternative<Wall>(shape))
        return t("Muro");
    if (std::holds_alternative<Slab>(shape))
        return t("Solaio");
    if (std::holds_alternative<Roof>(shape))
        return t("Tetto");
    if (std::holds_alternative<Stair>(shape))
        return t("Scala");
    if (const auto* ref = std::get_if<Insert>(&shape))
        return ref->block ? t("Blocco %1").arg(QString::fromStdString(ref->block->name)) : t("Blocco");
    return {};
}

std::vector<Property> shapeProperties(const Shape& shape)
{
    std::vector<Property> out;
    if (const auto* w = std::get_if<Wall>(&shape)) {
        out.push_back(length("thickness", t("Spessore"), w->thickness));
        out.push_back(length("height", t("Altezza"), w->height));
        out.push_back(choice("justification", t("Giustificazione"), static_cast<int>(w->justification), justifications()));
    } else if (const auto* sl = std::get_if<Slab>(&shape)) {
        out.push_back(length("thickness", t("Spessore"), sl->thickness));
        out.push_back(length("offset", t("Quota"), sl->offset, -1e9));
    } else if (const auto* r = std::get_if<Roof>(&shape)) {
        const auto style = roofStyleOf(*r);
        QStringList styles = roofStyles();
        if (!style)
            styles << t("Personalizzato");
        out.push_back(choice("style", t("Tipo"), style ? static_cast<int>(*style) : 4, styles));
        out.push_back(pitch(radToDeg(r->pitch)));
        out.push_back(length("overhang", t("Sporto"), r->overhang, 0.0));
        out.push_back(length("thickness", t("Spessore"), r->thickness));
        out.push_back(length("base", t("Imposta"), r->base, -1e9));
    } else if (const auto* st = std::get_if<Stair>(&shape)) {
        out.push_back(choice("kind", t("Tipo"), static_cast<int>(st->kind), stairKinds()));
        out.push_back(choice("turn", t("Verso"), st->turnRight ? 1 : 0, turns()));
        out.push_back(length("width", t("Larghezza"), st->width));
        out.push_back(length("going", t("Pedata"), st->going));
        out.push_back(length("height", t("Altezza"), st->height));
    } else if (const auto* ref = std::get_if<Insert>(&shape)) {
        // A negative scale mirrors the block.
        for (const auto& [key, label, value] : {std::tuple{"scaleX", t("Scala X"), ref->scaleX}, std::tuple{"scaleY", t("Scala Y"), ref->scaleY}}) {
            Property p = length(key, label, value, -1e4);
            p.type = Property::Type::Factor;
            p.maximum = 1e4;
            out.push_back(p);
        }
        Property rotation = length("rotation", t("Rotazione"), radToDeg(normalizeAngle(ref->rotation)), 0.0);
        rotation.type = Property::Type::Angle;
        rotation.maximum = 360.0;
        out.push_back(rotation);
    }
    return out;
}

std::optional<Shape> withProperty(const Shape& shape, const std::string& key, double v)
{
    if (std::isnan(v))
        return std::nullopt;
    if (const auto* w = std::get_if<Wall>(&shape)) {
        Wall r = *w;
        if (key == "thickness" && v > 0.0)
            r.thickness = v;
        else if (key == "height" && v > 0.0)
            r.height = v;
        else if (key == "justification" && index(v) >= 0 && index(v) <= 2)
            r.justification = static_cast<Wall::Justification>(index(v));
        else
            return std::nullopt;
        return r;
    }
    if (const auto* sl = std::get_if<Slab>(&shape)) {
        Slab r = *sl;
        if (key == "thickness" && v > 0.0)
            r.thickness = v;
        else if (key == "offset")
            r.offset = v;
        else
            return std::nullopt;
        return r;
    }
    if (const auto* roof = std::get_if<Roof>(&shape)) {
        Roof r = *roof;
        if (key == "style" && index(v) >= 0 && index(v) <= 3) {
            const auto g = roofGeometry(r);
            if (!g)
                return std::nullopt;
            r.gables = roofGables(g->footprint, static_cast<RoofStyle>(index(v)));
        } else if (key == "pitch" && v >= 0.0 && v < 80.0) {
            r.pitch = degToRad(v);
        } else if (key == "overhang" && v >= 0.0) {
            r.overhang = v;
        } else if (key == "thickness" && v > 0.0) {
            r.thickness = v;
        } else if (key == "base") {
            r.base = v;
        } else {
            return std::nullopt;
        }
        return r;
    }
    if (const auto* st = std::get_if<Stair>(&shape)) {
        Stair r = *st;
        if (key == "kind" && index(v) >= 0 && index(v) <= 2)
            r.kind = static_cast<Stair::Kind>(index(v));
        else if (key == "turn")
            r.turnRight = index(v) == 1;
        else if (key == "width" && v > 0.0)
            r.width = v;
        else if (key == "going" && v > 0.0)
            r.going = v;
        else if (key == "height" && v > 0.0) {
            r.height = v;
            r.risers = stairRisers(v);
        } else
            return std::nullopt;
        return r;
    }
    if (const auto* ref = std::get_if<Insert>(&shape)) {
        Insert r = *ref;
        if (key == "scaleX" && std::abs(v) > 1e-9)
            r.scaleX = v;
        else if (key == "scaleY" && std::abs(v) > 1e-9)
            r.scaleY = v;
        else if (key == "rotation")
            r.rotation = degToRad(v);
        else
            return std::nullopt;
        return r;
    }
    return std::nullopt;
}

} // namespace cad
