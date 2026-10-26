// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in drawing and editing commands. Prompts and options follow the
// Italian version of AutoCAD so that its users feel at home; English option
// letters are accepted too.
#include "commands.h"

#include "commandengine.h"
#include "building.h"
#include "editing.h"
#include "hatch.h"
#include "planrecognition.h"
#include "properties.h"
#include "section.h"
#include "stylenames.h"

#include <QCoreApplication>
#include <QDate>

#include <algorithm>
#include <functional>
#include <limits>
#include <map>

namespace cad {

namespace {

QString t(const char* text)
{
    return QCoreApplication::translate("Command", text);
}

bool isAny(const QString& word, std::initializer_list<const char*> options)
{
    for (const char* o : options)
        if (word == QString::fromUtf8(o))
            return true;
    return false;
}

bool isUndoWord(const QString& w) { return isAny(w, {"A", "ANNULLA", "U", "UNDO"}); }
bool isCloseWord(const QString& w) { return isAny(w, {"C", "CHIUDI", "CLOSE"}); }

// ---------------------------------------------------------------- drawing

class LineCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto:")); }

    bool point(Vec2 p) override
    {
        if (!points_.empty()) {
            if (almostEqual(p, points_.back()))
                return false;
            segments_.push_back(engine->document().add(Line{points_.back(), p}));
        }
        points_.push_back(p);
        updatePrompt();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (isUndoWord(w) && !points_.empty()) {
            if (!segments_.empty()) {
                engine->document().remove(segments_.back());
                segments_.pop_back();
            }
            points_.pop_back();
            if (points_.empty())
                start();
            else
                updatePrompt();
            return true;
        }
        if (isCloseWord(w) && segments_.size() >= 2) {
            engine->document().add(Line{points_.back(), points_.front()});
            engine->finish();
            return true;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (!points_.empty())
            out.push_back(Line{points_.back(), cursor});
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    void updatePrompt()
    {
        engine->setPrompt(segments_.size() >= 2 ? t("Specificare punto successivo o [Chiudi/Annulla]:")
                                                : t("Specificare punto successivo o [Annulla]:"));
    }

    std::vector<Vec2> points_;
    std::vector<EntityId> segments_;
};

// POINT: one point per pick, until Enter, for survey or reference marks.
class PointCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare un punto:")); }

    bool point(Vec2 p) override
    {
        engine->document().add(Point{p});
        return true;
    }
};

// PLINE as in AutoCAD: straight segments, or arcs tangent to the previous
// segment in Arco mode. "A" is Arco here, so Annulla is "AN".
class PolylineCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare punto iniziale:")); }

    bool point(Vec2 p) override
    {
        if (!points_.empty() && almostEqual(p, points_.back()))
            return false;
        if (!points_.empty())
            bulges_.push_back(arcMode_ ? tangentBulge(p) : 0.0);
        points_.push_back(p);
        updatePrompt();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (points_.empty())
            return false;
        if (isAny(w, {"AN", "ANNULLA", "U", "UNDO"})) {
            points_.pop_back();
            if (!bulges_.empty())
                bulges_.pop_back();
            if (points_.empty())
                start();
            else
                updatePrompt();
            return true;
        }
        if (isCloseWord(w) && points_.size() >= (arcMode_ ? 2u : 3u)) {
            bulges_.push_back(arcMode_ ? tangentBulge(points_.front()) : 0.0);
            engine->document().add(Polyline{points_, true, bulges_});
            engine->finish();
            return true;
        }
        if (!arcMode_ && isAny(w, {"A", "ARCO", "ARC"})) {
            arcMode_ = true;
            updatePrompt();
            return true;
        }
        if (arcMode_ && isAny(w, {"L", "LINEA", "LINE"})) {
            arcMode_ = false;
            updatePrompt();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (points_.size() >= 2)
            engine->document().add(Polyline{points_, false, bulges_});
        engine->finish();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.empty())
            return;
        Polyline pl{points_, false, bulges_};
        pl.bulges.push_back(arcMode_ ? tangentBulge(cursor) : 0.0);
        pl.points.push_back(cursor);
        out.push_back(pl);
    }

    std::optional<Vec2> base() const override
    {
        // No ortho on arcs: the end point sets their curvature.
        if (points_.empty() || arcMode_)
            return std::nullopt;
        return points_.back();
    }

private:
    // Bulge of the arc from the last point to `end`, tangent to the previous segment.
    double tangentBulge(Vec2 end) const
    {
        const Vec2 from = points_.back();
        Vec2 dir{1.0, 0.0};
        if (points_.size() >= 2) {
            const Polyline pl{points_, false, bulges_};
            dir = segmentTangent(pl, points_.size() - 2, 1.0);
        }
        const Vec2 chord = end - from;
        if (length(chord) < kEps)
            return 0.0;
        // The arc turns twice the angle between the tangent and the chord.
        const double half = std::atan2(cross(dir, chord), dot(dir, chord));
        if (std::abs(std::abs(half) - kPi) < 1e-9)
            return 0.0;
        return bulgeOfSweep(2.0 * half);
    }

    void updatePrompt()
    {
        const bool canClose = points_.size() >= (arcMode_ ? 2u : 3u);
        if (arcMode_)
            engine->setPrompt(canClose ? t("Specificare punto finale dell'arco o [Linea/Chiudi/ANnulla]:")
                                       : t("Specificare punto finale dell'arco o [Linea/ANnulla]:"));
        else
            engine->setPrompt(canClose ? t("Specificare punto successivo o [Arco/Chiudi/ANnulla]:")
                                       : t("Specificare punto successivo o [Arco/ANnulla]:"));
    }

    std::vector<Vec2> points_;
    std::vector<double> bulges_;
    bool arcMode_ = false;
};

// CIRCLE: centre and radius or diameter, or through three points (3P) or
// the two ends of a diameter (2P).
class CircleCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare centro del cerchio o [3P/2P]:")); }

    bool point(Vec2 p) override
    {
        if (mode_ != Mode::Center) {
            if (!points_.empty() && almostEqual(p, points_.back()))
                return false;
            points_.push_back(p);
            if (points_.size() < (mode_ == Mode::ThreePoints ? 3u : 2u)) {
                engine->setPrompt(points_.size() == 1
                                      ? (mode_ == Mode::ThreePoints ? t("Specificare secondo punto sul cerchio:")
                                                                    : t("Specificare secondo estremo del diametro del cerchio:"))
                                      : t("Specificare terzo punto sul cerchio:"));
                return true;
            }
            const auto c = throughPoints(points_);
            if (!c) {
                engine->message(t("I tre punti sono allineati: nessun cerchio passa per tutti."));
                points_.pop_back();
                return false;
            }
            engine->document().add(*c);
            engine->finish();
            return true;
        }
        if (!center_) {
            center_ = p;
            engine->setPrompt(t("Specificare raggio del cerchio o [Diametro]:"));
            return true;
        }
        const double d = distance(*center_, p);
        return create(diameter_ ? d / 2.0 : d);
    }

    bool number(double value) override
    {
        if (!center_)
            return false;
        return create(diameter_ ? value / 2.0 : value);
    }

    bool keyword(const QString& w) override
    {
        if (!center_ && mode_ == Mode::Center && points_.empty()) {
            if (isAny(w, {"3P", "3"})) {
                mode_ = Mode::ThreePoints;
                engine->setPrompt(t("Specificare primo punto sul cerchio:"));
                return true;
            }
            if (isAny(w, {"2P", "2"})) {
                mode_ = Mode::TwoPoints;
                engine->setPrompt(t("Specificare primo estremo del diametro del cerchio:"));
                return true;
            }
        }
        if (center_ && !diameter_ && isAny(w, {"D", "DIAMETRO", "DIAMETER"})) {
            diameter_ = true;
            engine->setPrompt(t("Specificare diametro del cerchio:"));
            return true;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (mode_ != Mode::Center) {
            std::vector<Vec2> pts = points_;
            pts.push_back(cursor);
            if (pts.size() >= 2)
                if (const auto c = throughPoints(pts))
                    out.push_back(*c);
            return;
        }
        if (!center_)
            return;
        const double d = distance(*center_, cursor);
        if (d > kEps)
            out.push_back(Circle{*center_, diameter_ ? d / 2.0 : d});
        out.push_back(Line{*center_, cursor});
    }

    std::optional<Vec2> base() const override
    {
        if (mode_ != Mode::Center)
            return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
        return center_;
    }

private:
    enum class Mode { Center, ThreePoints, TwoPoints };

    // The circle through two points (a diameter) or three.
    static std::optional<Circle> throughPoints(const std::vector<Vec2>& pts)
    {
        if (pts.size() == 2)
            return distance(pts[0], pts[1]) > kEps ? std::optional<Circle>(Circle{midpoint(pts[0], pts[1]), distance(pts[0], pts[1]) / 2.0})
                                                   : std::nullopt;
        if (pts.size() == 3)
            if (const auto arc = arcFromThreePoints(pts[0], pts[1], pts[2]))
                return Circle{arc->center, arc->radius};
        return std::nullopt;
    }

    bool create(double radius)
    {
        if (radius <= kEps) {
            engine->message(t("Il valore deve essere positivo e diverso da zero."));
            return false;
        }
        engine->document().add(Circle{*center_, radius});
        engine->finish();
        return true;
    }

    Mode mode_ = Mode::Center;
    std::vector<Vec2> points_;
    std::optional<Vec2> center_;
    bool diameter_ = false;
};

// POLYGON: a regular polygon from its centre, inscribed in or circumscribed
// about a circle, or from one edge (Lato), as a closed polyline.
class PolygonCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare numero di lati <%1>:").arg(sSides)); }

    bool number(double value) override
    {
        if (ask_ == Ask::Sides) {
            const int n = static_cast<int>(std::lround(value));
            if (std::abs(value - n) > 1e-9 || n < 3 || n > 1024) {
                engine->message(t("Il numero di lati deve essere un intero tra 3 e 1024."));
                return false;
            }
            sSides = n;
            toCenter();
            return true;
        }
        if (ask_ == Ask::Radius && value > kEps) {
            // A typed radius gives a polygon with a horizontal bottom edge.
            const double step = kTwoPi / sSides;
            const double edgeMid = -kPi / 2.0;
            const double vertexAngle = edgeMid + step / 2.0;
            const double r = inscribed_ ? value : value / std::cos(step / 2.0);
            add(polygon(center_, r, vertexAngle));
            return true;
        }
        return false;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ == Ask::Center && isAny(w, {"L", "LATO", "E", "EDGE"})) {
            ask_ = Ask::EdgeFirst;
            engine->setPrompt(t("Specificare primo estremo del lato:"));
            return true;
        }
        if (ask_ == Ask::Kind) {
            if (isAny(w, {"I", "INSCRITTO", "INSCRIBED"}))
                inscribed_ = true;
            else if (isAny(w, {"C", "CIRCOSCRITTO", "CIRCUMSCRIBED"}))
                inscribed_ = false;
            else
                return false;
            sInscribed = inscribed_;
            toRadius();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (ask_ == Ask::Sides) {
            toCenter();
            return;
        }
        if (ask_ == Ask::Kind) {
            toRadius();
            return;
        }
        engine->finish();
    }

    bool point(Vec2 p) override
    {
        switch (ask_) {
        case Ask::Sides:
            return false;
        case Ask::Center:
            center_ = p;
            inscribed_ = sInscribed;
            ask_ = Ask::Kind;
            engine->setPrompt(t("Specificare un'opzione [Inscritto nel cerchio/Circoscritto attorno al cerchio] <%1>:")
                                  .arg(sInscribed ? t("I") : t("C")));
            return true;
        case Ask::Kind:
            return false;
        case Ask::Radius:
            if (const auto pl = fromRadiusPoint(p)) {
                add(*pl);
                return true;
            }
            return false;
        case Ask::EdgeFirst:
            edgeStart_ = p;
            ask_ = Ask::EdgeSecond;
            engine->setPrompt(t("Specificare secondo estremo del lato:"));
            return true;
        case Ask::EdgeSecond:
            if (const auto pl = fromEdge(p)) {
                add(*pl);
                return true;
            }
            return false;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        std::optional<Polyline> pl;
        if (ask_ == Ask::Radius)
            pl = fromRadiusPoint(cursor);
        else if (ask_ == Ask::EdgeSecond)
            pl = fromEdge(cursor);
        if (pl)
            out.push_back(*pl);
    }

    std::optional<Vec2> base() const override
    {
        if (ask_ == Ask::Radius)
            return center_;
        if (ask_ == Ask::EdgeSecond)
            return edgeStart_;
        return std::nullopt;
    }

private:
    enum class Ask { Sides, Center, Kind, Radius, EdgeFirst, EdgeSecond };

    static Polyline polygon(Vec2 center, double radius, double firstAngle)
    {
        Polyline pl;
        pl.closed = true;
        for (int i = 0; i < sSides; ++i)
            pl.points.push_back(center + polar(radius, firstAngle + kTwoPi * i / sSides));
        return pl;
    }

    // Inscribed: the point is a vertex. Circumscribed: the midpoint of an edge.
    std::optional<Polyline> fromRadiusPoint(Vec2 p) const
    {
        const double d = distance(center_, p);
        if (d <= kEps)
            return std::nullopt;
        const double step = kTwoPi / sSides;
        const double a = angleOf(p - center_);
        return inscribed_ ? polygon(center_, d, a) : polygon(center_, d / std::cos(step / 2.0), a + step / 2.0);
    }

    // The polygon lies on the left of the edge, going from its first end to its second.
    std::optional<Polyline> fromEdge(Vec2 b) const
    {
        const double side = distance(edgeStart_, b);
        if (side <= kEps)
            return std::nullopt;
        const double step = kTwoPi / sSides;
        const double r = side / (2.0 * std::sin(step / 2.0));
        const double apothem = r * std::cos(step / 2.0);
        const Vec2 dir = (b - edgeStart_) / side;
        const Vec2 center = midpoint(edgeStart_, b) + perpendicular(dir) * apothem;
        Polyline pl = polygon(center, r, angleOf(edgeStart_ - center));
        return pl;
    }

    void toCenter()
    {
        ask_ = Ask::Center;
        engine->setPrompt(t("Specificare centro del poligono o [Lato]:"));
    }

    void toRadius()
    {
        ask_ = Ask::Radius;
        engine->setPrompt(t("Specificare raggio del cerchio:"));
    }

    void add(const Polyline& pl)
    {
        engine->document().add(pl);
        engine->finish();
    }

    static inline int sSides = 4;
    static inline bool sInscribed = true;
    Ask ask_ = Ask::Sides;
    bool inscribed_ = true;
    Vec2 center_;
    Vec2 edgeStart_;
};

class ArcCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare punto iniziale dell'arco:")); }

    bool point(Vec2 p) override
    {
        if (!points_.empty() && almostEqual(p, points_.back()))
            return false;
        if (points_.size() < 2) {
            points_.push_back(p);
            engine->setPrompt(points_.size() == 1 ? t("Specificare secondo punto dell'arco:")
                                                  : t("Specificare punto finale dell'arco:"));
            return true;
        }
        const auto arc = arcFromThreePoints(points_[0], points_[1], p);
        if (!arc) {
            engine->message(t("I tre punti sono allineati: impossibile creare l'arco."));
            return false;
        }
        engine->document().add(*arc);
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.size() == 1) {
            out.push_back(Line{points_[0], cursor});
        } else if (points_.size() == 2) {
            if (const auto arc = arcFromThreePoints(points_[0], points_[1], cursor))
                out.push_back(*arc);
            else
                out.push_back(Line{points_[0], cursor});
        }
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    std::vector<Vec2> points_;
};

Polyline rectangle(Vec2 a, Vec2 b)
{
    return Polyline{{a, {b.x, a.y}, b, {a.x, b.y}}, true};
}

class RectangleCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto d'angolo:")); }

    bool point(Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            engine->setPrompt(t("Specificare altro punto d'angolo:"));
            return true;
        }
        if (std::abs(p.x - first_->x) < kEps || std::abs(p.y - first_->y) < kEps) {
            engine->message(t("Il rettangolo deve avere larghezza e altezza diverse da zero."));
            return false;
        }
        engine->document().add(rectangle(*first_, p));
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (first_)
            out.push_back(rectangle(*first_, cursor));
    }

    // No rubber band base: ortho would flatten the rectangle.
private:
    std::optional<Vec2> first_;
};

// ---------------------------------------------------------------- editing

// Commands that work on a set of objects. If objects are already selected
// they are used at once (noun-verb), otherwise the command asks for them.
class SelectionCommand : public Command {
public:
    void start() override
    {
        if (!engine->selection().empty()) {
            takeSelection();
            return;
        }
        selecting_ = true;
        engine->setPrompt(t("Selezionare oggetti:"));
    }

    void enter() override
    {
        if (!selecting_) {
            onEnter();
            return;
        }
        if (engine->selection().empty()) {
            engine->finish();
            return;
        }
        selecting_ = false;
        takeSelection();
    }

    bool point(Vec2 p) override { return !selecting_ && onPoint(p); }
    bool wantsSelection() const override { return selecting_; }

protected:
    virtual void afterSelection() = 0;
    virtual bool onPoint(Vec2) { return false; }
    virtual void onEnter() { done(); }

    void done()
    {
        engine->clearSelection();
        engine->finish();
    }

    void previewTransform(const Transform& tr, std::vector<Shape>& out) const
    {
        // Previewing thousands of objects would make the cursor lag.
        constexpr size_t kMaxPreview = 2000;
        for (size_t i = 0; i < ids_.size() && i < kMaxPreview; ++i)
            if (const Entity* e = engine->document().entity(ids_[i]))
                out.push_back(transformed(e->shape, tr));
    }

    void applyTransform(const Transform& tr, bool copy)
    {
        Document& doc = engine->document();
        for (EntityId id : ids_) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            if (copy) {
                Entity c = *e;
                c.id = 0;
                c.shape = transformed(e->shape, tr);
                doc.add(std::move(c));
            } else {
                doc.replace(id, transformed(e->shape, tr));
            }
        }
    }

    std::vector<EntityId> ids_;

private:
    void takeSelection()
    {
        ids_.assign(engine->selection().begin(), engine->selection().end());
        afterSelection();
    }

    bool selecting_ = false;
};

class EraseCommand : public SelectionCommand {
protected:
    void afterSelection() override
    {
        for (EntityId id : ids_)
            engine->document().remove(id);
        engine->message(QCoreApplication::translate("Command", "%n oggetti cancellati", nullptr, static_cast<int>(ids_.size())));
        done();
    }
};

class MoveCommand : public SelectionCommand {
public:
    explicit MoveCommand(bool copy) : copy_(copy) {}

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (base_) {
            previewTransform(Transform::translation(cursor - *base_), out);
            out.push_back(Line{*base_, cursor});
        }
    }

    std::optional<Vec2> base() const override { return base_; }

protected:
    void afterSelection() override { engine->setPrompt(t("Specificare punto base:")); }

    bool onPoint(Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            engine->setPrompt(copy_ ? t("Specificare secondo punto o [Esci]:") : t("Specificare secondo punto:"));
            return true;
        }
        applyTransform(Transform::translation(p - *base_), copy_);
        if (!copy_)
            done();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (copy_ && base_ && isAny(w, {"E", "ESCI", "EXIT"})) {
            done();
            return true;
        }
        return false;
    }

private:
    bool copy_;
    std::optional<Vec2> base_;
};

class RotateCommand : public SelectionCommand {
public:
    bool number(double degrees) override
    {
        if (!base_)
            return false;
        apply(degToRad(degrees));
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (base_ && distance(*base_, cursor) > kEps) {
            previewTransform(Transform::rotation(*base_, angleOf(cursor - *base_)), out);
            out.push_back(Line{*base_, cursor});
        }
    }

    std::optional<Vec2> base() const override { return base_; }

protected:
    void afterSelection() override { engine->setPrompt(t("Specificare punto base:")); }

    bool onPoint(Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            engine->setPrompt(t("Specificare angolo di rotazione:"));
            return true;
        }
        apply(angleOf(p - *base_));
        return true;
    }

private:
    void apply(double angle)
    {
        applyTransform(Transform::rotation(*base_, angle), false);
        done();
    }

    std::optional<Vec2> base_;
};

class ScaleCommand : public SelectionCommand {
public:
    bool number(double factor) override
    {
        if (!base_)
            return false;
        if (factor <= kEps) {
            engine->message(t("Il fattore di scala deve essere positivo."));
            return true;
        }
        apply(factor);
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (base_) {
            const double f = distance(*base_, cursor);
            if (f > kEps)
                previewTransform(Transform::scaling(*base_, f), out);
            out.push_back(Line{*base_, cursor});
        }
    }

    std::optional<Vec2> base() const override { return base_; }

protected:
    void afterSelection() override { engine->setPrompt(t("Specificare punto base:")); }

    bool onPoint(Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            engine->setPrompt(t("Specificare fattore di scala:"));
            return true;
        }
        // As in AutoCAD, a picked point gives the factor as its distance from the base.
        const double f = distance(*base_, p);
        if (f <= kEps)
            return false;
        apply(f);
        return true;
    }

private:
    void apply(double factor)
    {
        applyTransform(Transform::scaling(*base_, factor), false);
        done();
    }

    std::optional<Vec2> base_;
};

class MirrorCommand : public SelectionCommand {
public:
    bool keyword(const QString& w) override
    {
        if (!second_)
            return false;
        if (isAny(w, {"S", "SI", "SÌ", "Y", "YES"})) {
            apply(true);
            return true;
        }
        if (isAny(w, {"N", "NO"})) {
            apply(false);
            return true;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (first_ && !second_ && distance(*first_, cursor) > kEps) {
            previewTransform(Transform::mirror(*first_, cursor), out);
            out.push_back(Line{*first_, cursor});
        }
    }

    std::optional<Vec2> base() const override { return second_ ? std::nullopt : first_; }

protected:
    void afterSelection() override { engine->setPrompt(t("Specificare primo punto della linea di specchio:")); }

    bool onPoint(Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            engine->setPrompt(t("Specificare secondo punto della linea di specchio:"));
            return true;
        }
        if (second_ || almostEqual(p, *first_))
            return false;
        second_ = p;
        engine->setPrompt(t("Cancellare oggetti sorgente? [Sì/No] <N>:"));
        return true;
    }

    void onEnter() override
    {
        if (second_)
            apply(false);
        else
            done();
    }

private:
    void apply(bool eraseSource)
    {
        applyTransform(Transform::mirror(*first_, *second_), !eraseSource);
        done();
    }

    std::optional<Vec2> first_;
    std::optional<Vec2> second_;
};

// ------------------------------------------------- offset, trim, extend, fillet

// Visible shapes other than `except`, used as cutting and boundary edges.
std::vector<Shape> edgesExcept(const Document& doc, EntityId except)
{
    std::vector<Shape> out;
    for (const auto& [id, e] : doc.entities())
        if (id != except && doc.isVisible(e))
            out.push_back(e.shape);
    return out;
}

// Picks one editable object under the pick box, telling the user when there is none.
// Adds `shape` with the layer, style and story of `like`.
EntityId addLike(Document& doc, const Entity& like, Shape shape)
{
    Entity e = like;
    e.id = 0;
    e.shape = std::move(shape);
    return doc.add(std::move(e));
}

std::optional<EntityId> pickObject(CommandEngine* engine, Vec2 p)
{
    const auto id = engine->pick(p, engine->pickTolerance());
    if (!id)
        engine->message(t("Nessun oggetto trovato."));
    return id;
}

bool isExitWord(const QString& w) { return isAny(w, {"E", "ESCI", "EXIT", "X"}); }

// Remembered between runs, like AutoCAD's OFFSETDIST and FILLETRAD.
double gOffsetDistance = 1.0;
double gFilletRadius = 0.0;

class OffsetCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Specificare distanza di offset o [Punto] <%1>:").arg(gOffsetDistance));
    }

    bool number(double value) override
    {
        if (step_ != Step::Distance)
            return false;
        if (value <= kEps) {
            engine->message(t("La distanza deve essere positiva."));
            return true;
        }
        gOffsetDistance = value;
        toPick();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (step_ == Step::Distance && isAny(w, {"P", "PUNTO", "T", "THROUGH"})) {
            through_ = true;
            toPick();
            return true;
        }
        if (step_ != Step::Distance && isExitWord(w)) {
            engine->finish();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (step_ == Step::Distance)
            toPick();
        else
            engine->finish();
    }

    bool point(Vec2 p) override
    {
        if (step_ == Step::Pick) {
            const auto id = pickObject(engine, p);
            if (!id)
                return false;
            source_ = *id;
            step_ = Step::Side;
            engine->setPrompt(through_ ? t("Specificare punto di passaggio o [Esci]:")
                                       : t("Specificare punto sul lato di cui eseguire l'offset o [Esci]:"));
            return true;
        }
        if (step_ != Step::Side)
            return false;
        const Entity* e = engine->document().entity(source_);
        if (!e)
            return false;
        if (const auto result = offsetAt(*e, p)) {
            Entity copy = *e;
            copy.id = 0;
            copy.shape = *result;
            engine->document().add(std::move(copy));
        } else {
            engine->message(t("Impossibile eseguire l'offset dell'oggetto a questa distanza."));
        }
        toPick();
        return true;
    }

    bool wantsObjectPick() const override { return step_ == Step::Pick; }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ != Step::Side)
            return;
        if (const Entity* e = engine->document().entity(source_))
            if (const auto result = offsetAt(*e, cursor))
                out.push_back(*result);
    }

private:
    enum class Step { Distance, Pick, Side };

    std::optional<Shape> offsetAt(const Entity& e, Vec2 p) const
    {
        const double d = through_ ? distanceTo(e.shape, p) : gOffsetDistance;
        return offsetShape(e.shape, d, p);
    }

    void toPick()
    {
        step_ = Step::Pick;
        engine->setPrompt(t("Selezionare oggetto di cui eseguire l'offset o [Esci] <Esci>:"));
    }

    Step step_ = Step::Distance;
    bool through_ = false;
    EntityId source_ = 0;
};

// TRIM and EXTEND share their flow: every visible object is an edge (the
// AutoCAD "quick" mode) and each click edits the object under the pick box.
class TrimExtendCommand : public Command {
public:
    explicit TrimExtendCommand(bool extend) : extend_(extend) {}

    void start() override
    {
        engine->setPrompt(extend_ ? t("Selezionare oggetto da estendere o [Esci]:")
                                  : t("Selezionare oggetto da tagliare o [Esci]:"));
    }

    bool point(Vec2 p) override
    {
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        Document& doc = engine->document();
        const Entity entity = *doc.entity(*id);
        const auto edges = edgesExcept(doc, *id);

        if (extend_) {
            const auto result = extendShape(entity.shape, edges, p);
            if (!result) {
                engine->message(t("L'oggetto non incontra nessun contorno."));
                return false;
            }
            doc.replace(*id, *result);
            return true;
        }

        const TrimResult result = trimShape(entity.shape, edges, p);
        if (!result.changed) {
            engine->message(t("Impossibile tagliare l'oggetto in questo punto."));
            return false;
        }
        doc.remove(*id);
        for (const Shape& piece : result.pieces) {
            Entity e = entity;
            e.id = 0;
            e.shape = piece;
            doc.add(std::move(e));
        }
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (!isExitWord(w))
            return false;
        engine->finish();
        return true;
    }

    bool wantsObjectPick() const override { return true; }

private:
    bool extend_;
};

// FILLET and CHAMFER Polilinea: applies `apply` to the picked polyline.
template <class Apply>
bool cornersOfPolyline(CommandEngine* engine, Vec2 p, Apply apply)
{
    const auto id = pickObject(engine, p);
    if (!id)
        return false;
    Document& doc = engine->document();
    const auto* pl = std::get_if<Polyline>(&doc.entity(*id)->shape);
    if (!pl) {
        engine->message(t("L'oggetto selezionato non è una polilinea."));
        return false;
    }
    const PolylineCornersResult r = apply(*pl);
    if (r.changed > 0)
        doc.replace(*id, r.polyline);
    engine->message(QCoreApplication::translate("Command", "%n vertici modificati", nullptr, r.changed));
    if (r.skipped > 0)
        engine->message(QCoreApplication::translate("Command", "%n vertici saltati perché i segmenti sono troppo corti", nullptr, r.skipped));
    engine->finish();
    return true;
}

class FilletCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Impostazioni correnti: Raggio = %1").arg(gFilletRadius));
        toFirst();
    }

    bool keyword(const QString& w) override
    {
        if (step_ == Step::First && isAny(w, {"R", "RAGGIO", "RADIUS"})) {
            step_ = Step::Radius;
            engine->setPrompt(t("Specificare raggio di raccordo <%1>:").arg(gFilletRadius));
            return true;
        }
        if (step_ == Step::First && isAny(w, {"P", "POLILINEA", "POLYLINE"})) {
            step_ = Step::Polyline;
            engine->setPrompt(t("Selezionare polilinea 2D:"));
            return true;
        }
        return false;
    }

    bool number(double value) override
    {
        if (step_ != Step::Radius)
            return false;
        if (value < 0.0) {
            engine->message(t("Il raggio non può essere negativo."));
            return true;
        }
        gFilletRadius = value;
        toFirst();
        return true;
    }

    void enter() override
    {
        if (step_ == Step::Radius)
            toFirst();
        else
            engine->finish();
    }

    bool point(Vec2 p) override
    {
        if (step_ == Step::Radius)
            return false;
        if (step_ == Step::Polyline)
            return cornersOfPolyline(engine, p, [](const Polyline& pl) { return filletPolyline(pl, gFilletRadius); });
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        Document& doc = engine->document();
        const Shape& picked = doc.entity(*id)->shape;
        if (!std::holds_alternative<Line>(picked) && !std::holds_alternative<Arc>(picked) && !std::holds_alternative<Circle>(picked)) {
            engine->message(t("Il raccordo funziona tra linee, archi e cerchi."));
            return false;
        }
        if (step_ == Step::First) {
            first_ = *id;
            firstPick_ = p;
            step_ = Step::Second;
            engine->setPrompt(t("Selezionare secondo oggetto:"));
            return true;
        }
        if (*id == first_) {
            engine->message(t("Selezionare un oggetto diverso."));
            return false;
        }

        const Entity a = *doc.entity(first_);
        FilletError error = FilletError::None;
        const auto result = filletCurves(a.shape, firstPick_, picked, p, gFilletRadius, &error);
        if (!result) {
            engine->message(error == FilletError::Parallel ? t("Gli oggetti non si incontrano.")
                                                           : t("Il raggio è troppo grande per questi oggetti."));
            return false;
        }
        const EntityId secondId = *id;
        doc.replace(first_, result->first);
        doc.replace(secondId, result->second);
        if (result->arc) {
            Entity arc = a;
            arc.id = 0;
            arc.shape = *result->arc;
            doc.add(std::move(arc));
        }
        engine->finish();
        return true;
    }

    bool wantsObjectPick() const override { return step_ != Step::Radius; }

private:
    enum class Step { First, Radius, Second, Polyline };

    void toFirst()
    {
        step_ = Step::First;
        engine->setPrompt(t("Selezionare primo oggetto o [Polilinea/Raggio]:"));
    }

    Step step_ = Step::First;
    EntityId first_ = 0;
    Vec2 firstPick_;
};

// Remembered between runs, like AutoCAD's CHAMFERA, CHAMFERB, CHAMFERC,
// CHAMFERD and CHAMMODE.
double gChamferDist1 = 0.0;
double gChamferDist2 = 0.0;
double gChamferLength = 0.0;
double gChamferAngle = 0.0;  // degrees
bool gChamferByAngle = false;

class ChamferCommand : public Command {
public:
    void start() override
    {
        if (gChamferByAngle)
            engine->message(t("Impostazioni correnti: Lunghezza = %1, Angolo = %2").arg(gChamferLength).arg(gChamferAngle));
        else
            engine->message(t("Impostazioni correnti: Distanza1 = %1, Distanza2 = %2").arg(gChamferDist1).arg(gChamferDist2));
        toFirst();
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::First)
            return false;
        if (isAny(w, {"D", "DISTANZA", "DISTANCE"})) {
            step_ = Step::Dist1;
            engine->setPrompt(t("Specificare prima distanza di cima <%1>:").arg(gChamferDist1));
            return true;
        }
        if (isAny(w, {"P", "POLILINEA", "POLYLINE"})) {
            step_ = Step::Polyline;
            engine->setPrompt(t("Selezionare polilinea 2D:"));
            return true;
        }
        if (isAny(w, {"A", "ANGOLO", "ANGLE"})) {
            step_ = Step::Length;
            engine->setPrompt(t("Specificare lunghezza di cima sulla prima linea <%1>:").arg(gChamferLength));
            return true;
        }
        return false;
    }

    bool number(double value) override
    {
        if (step_ == Step::First || step_ == Step::Second)
            return false;
        if (value < 0.0) {
            engine->message(t("Il valore non può essere negativo."));
            return true;
        }
        setValue(value);
        return true;
    }

    void enter() override
    {
        switch (step_) {
        case Step::Dist1: setValue(gChamferDist1); break;
        case Step::Dist2: setValue(gChamferDist2); break;
        case Step::Length: setValue(gChamferLength); break;
        case Step::Angle: setValue(gChamferAngle); break;
        default: engine->finish(); break;
        }
    }

    bool point(Vec2 p) override
    {
        if (step_ == Step::Polyline)
            return cornersOfPolyline(engine, p, [](const Polyline& pl) {
                if (gChamferByAngle)
                    return chamferPolyline(pl, gChamferLength, 0.0, degToRad(gChamferAngle));
                return chamferPolyline(pl, gChamferDist1, gChamferDist2);
            });
        if (step_ != Step::First && step_ != Step::Second)
            return false;
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        Document& doc = engine->document();
        if (!std::holds_alternative<Line>(doc.entity(*id)->shape)) {
            engine->message(t("Per ora la cima funziona solo tra due linee."));
            return false;
        }
        if (step_ == Step::First) {
            first_ = *id;
            firstPick_ = p;
            step_ = Step::Second;
            engine->setPrompt(t("Selezionare seconda linea:"));
            return true;
        }
        if (*id == first_) {
            engine->message(t("Selezionare una linea diversa."));
            return false;
        }

        const Entity a = *doc.entity(first_);
        const Line l1 = std::get<Line>(a.shape);
        const Line l2 = std::get<Line>(doc.entity(*id)->shape);
        double d1 = gChamferDist1;
        double d2 = gChamferDist2;
        if (gChamferByAngle) {
            d1 = gChamferLength;
            const auto other = chamferDistanceFromAngle(l1, firstPick_, l2, p, d1, degToRad(gChamferAngle));
            if (!other && d1 > kEps) {
                engine->message(t("Angolo di cima non valido per queste linee."));
                return false;
            }
            d2 = other.value_or(0.0);
        }
        FilletError error = FilletError::None;
        const auto result = chamferLines(l1, firstPick_, l2, p, d1, d2, &error);
        if (!result) {
            engine->message(error == FilletError::Parallel ? t("Le linee sono parallele.")
                                                           : t("La distanza di cima è troppo grande per queste linee."));
            return false;
        }
        doc.replace(first_, result->first);
        doc.replace(*id, result->second);
        if (result->bevel) {
            Entity bevel = a;
            bevel.id = 0;
            bevel.shape = *result->bevel;
            doc.add(std::move(bevel));
        }
        engine->finish();
        return true;
    }

    bool wantsObjectPick() const override
    {
        return step_ == Step::First || step_ == Step::Second || step_ == Step::Polyline;
    }

private:
    enum class Step { First, Second, Dist1, Dist2, Length, Angle, Polyline };

    void setValue(double v)
    {
        switch (step_) {
        case Step::Dist1:
            gChamferDist1 = v;
            gChamferByAngle = false;
            step_ = Step::Dist2;
            engine->setPrompt(t("Specificare seconda distanza di cima <%1>:").arg(gChamferDist1));
            gChamferDist2 = gChamferDist1;  // AutoCAD proposes the first distance
            return;
        case Step::Dist2:
            gChamferDist2 = v;
            break;
        case Step::Length:
            gChamferLength = v;
            gChamferByAngle = true;
            step_ = Step::Angle;
            engine->setPrompt(t("Specificare angolo di cima dalla prima linea <%1>:").arg(gChamferAngle));
            return;
        case Step::Angle:
            gChamferAngle = v;
            break;
        default:
            return;
        }
        toFirst();
    }

    void toFirst()
    {
        step_ = Step::First;
        engine->setPrompt(t("Selezionare la prima linea o [Polilinea/Distanza/Angolo]:"));
    }

    Step step_ = Step::First;
    EntityId first_ = 0;
    Vec2 firstPick_;
};

// STRETCH: a crossing window picks the objects and the points to move;
// whatever lies inside moves, the rest stays attached.
class StretchCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Selezionare oggetti da stirare con una finestra interseca."));
        engine->setPrompt(t("Specificare primo angolo:"));
    }

    bool point(Vec2 p) override
    {
        if (!corner_) {
            corner_ = p;
            engine->setPrompt(t("Specificare angolo opposto:"));
            return true;
        }
        if (!window_) {
            if (almostEqual(p, *corner_))
                return false;
            const BBox box = BBox::fromCorners(*corner_, p);
            ids_ = engine->pickBox(box, true);
            if (ids_.empty()) {
                engine->message(t("Nessun oggetto selezionato."));
                corner_.reset();
                start();
                return true;
            }
            window_ = box;
            engine->message(QCoreApplication::translate("Command", "%n oggetti selezionati", nullptr, static_cast<int>(ids_.size())));
            engine->setPrompt(t("Specificare punto base:"));
            return true;
        }
        if (!base_) {
            base_ = p;
            engine->setPrompt(t("Specificare secondo punto:"));
            return true;
        }
        Document& doc = engine->document();
        for (EntityId id : ids_)
            if (const Entity* e = doc.entity(id))
                doc.replace(id, stretchShape(e->shape, *window_, p - *base_));
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (corner_ && !window_) {
            const BBox b = BBox::fromCorners(*corner_, cursor);
            out.push_back(Polyline{{b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}}, true});
            return;
        }
        if (!base_)
            return;
        for (EntityId id : ids_)
            if (const Entity* e = engine->document().entity(id))
                out.push_back(stretchShape(e->shape, *window_, cursor - *base_));
        out.push_back(Line{*base_, cursor});
    }

    std::optional<Vec2> base() const override { return base_; }

private:
    std::optional<Vec2> corner_;
    std::optional<BBox> window_;
    std::optional<Vec2> base_;
    std::vector<EntityId> ids_;
};

// ---------------------------------------------------------------- text and dimensions

// Remembered between runs, like AutoCAD's TEXTSIZE and DIMSCALE.
double gTextHeight = 2.5;
double gTextRotation = 0.0;  // degrees
Text::Align gTextAlign = Text::Align::Left;
double gDimScale = 1.0;
// Decimals of the drawing's lengths (UNITS), set by the engine before each command.
int gPrecision = 2;

constexpr double kLineSpacing = 1.667;  // DTEXT line spacing, in text heights

// TEXT works like AutoCAD's DTEXT: each line typed becomes a text object and
// the next one starts below it; an empty line ends the command.
class TextCommand : public Command {
public:
    void start() override { toStart(); }

    bool keyword(const QString& w) override
    {
        if (step_ == Step::Start && isAny(w, {"G", "GIUSTIFICA", "J", "JUSTIFY"})) {
            step_ = Step::Justify;
            engine->setPrompt(t("Immettere un'opzione [Sinistra/Centro/Destra]:"));
            return true;
        }
        if (step_ != Step::Justify)
            return false;
        if (isAny(w, {"S", "SINISTRA", "L", "LEFT"}))
            gTextAlign = Text::Align::Left;
        else if (isAny(w, {"C", "CENTRO", "CENTER"}))
            gTextAlign = Text::Align::Center;
        else if (isAny(w, {"D", "DESTRA", "R", "RIGHT"}))
            gTextAlign = Text::Align::Right;
        else
            return false;
        toStart();
        return true;
    }

    bool point(Vec2 p) override
    {
        switch (step_) {
        case Step::Start:
            position_ = p;
            step_ = Step::Height;
            engine->setPrompt(t("Specificare altezza <%1>:").arg(gTextHeight));
            return true;
        case Step::Height:
            setHeight(distance(position_, p));
            return true;
        case Step::Rotation:
            gTextRotation = radToDeg(angleOf(p - position_));
            toContent();
            return true;
        default:
            return false;
        }
    }

    bool number(double value) override
    {
        if (step_ == Step::Height) {
            setHeight(value);
            return true;
        }
        if (step_ == Step::Rotation) {
            gTextRotation = value;
            toContent();
            return true;
        }
        return false;
    }

    void enter() override
    {
        switch (step_) {
        case Step::Justify: toStart(); break;
        case Step::Height: toRotation(); break;
        case Step::Rotation: toContent(); break;
        default: engine->finish(); break;
        }
    }

    bool wantsText() const override { return step_ == Step::Content; }

    void text(const QString& s) override
    {
        Text tx;
        tx.position = position_;
        tx.height = gTextHeight;
        tx.rotation = degToRad(gTextRotation);
        tx.content = s.toStdString();
        tx.align = gTextAlign;
        engine->document().add(tx);
        position_ = position_ - perpendicular(polar(1.0, tx.rotation)) * (gTextHeight * kLineSpacing);
        engine->setPrompt(t("Immettere testo (Invio a vuoto per terminare):"));
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Height || step_ == Step::Rotation)
            out.push_back(Line{position_, cursor});
    }

    std::optional<Vec2> base() const override
    {
        if (step_ == Step::Height || step_ == Step::Rotation)
            return position_;
        return std::nullopt;
    }

private:
    enum class Step { Start, Justify, Height, Rotation, Content };

    void toStart()
    {
        step_ = Step::Start;
        engine->setPrompt(t("Specificare punto iniziale del testo o [Giustifica]:"));
    }

    void setHeight(double h)
    {
        if (h <= kEps) {
            engine->message(t("L'altezza deve essere positiva."));
            return;
        }
        gTextHeight = h;
        toRotation();
    }

    void toRotation()
    {
        step_ = Step::Rotation;
        engine->setPrompt(t("Specificare angolo di rotazione del testo <%1>:").arg(gTextRotation));
    }

    void toContent()
    {
        step_ = Step::Content;
        engine->setPrompt(t("Immettere testo:"));
    }

    Step step_ = Step::Start;
    Vec2 position_;
};

// MTEXT: a paragraph in a box. The box gives the width the lines wrap at;
// the text comes from the editor, or line by line from the command line.
class MTextCommand : public Command {
public:
    void start() override
    {
        step_ = Step::First;
        engine->setPrompt(t("Specificare primo angolo:"));
    }

    bool point(Vec2 p) override
    {
        if (step_ == Step::First) {
            first_ = p;
            toOpposite();
            return true;
        }
        if (step_ == Step::Opposite) {
            place(p);
            toContent();
            return true;
        }
        if (step_ == Step::Rotation) {
            gTextRotation = radToDeg(angleOf(p - first_));
            toOpposite();
            return true;
        }
        return false;
    }

    bool keyword(const QString& w) override
    {
        if (step_ == Step::Justify) {
            static const std::map<QString, std::pair<Text::Align, MText::Vertical>> kCodes = {
                {QStringLiteral("AS"), {Text::Align::Left, MText::Vertical::Top}},
                {QStringLiteral("AC"), {Text::Align::Center, MText::Vertical::Top}},
                {QStringLiteral("AD"), {Text::Align::Right, MText::Vertical::Top}},
                {QStringLiteral("MS"), {Text::Align::Left, MText::Vertical::Middle}},
                {QStringLiteral("MC"), {Text::Align::Center, MText::Vertical::Middle}},
                {QStringLiteral("MD"), {Text::Align::Right, MText::Vertical::Middle}},
                {QStringLiteral("BS"), {Text::Align::Left, MText::Vertical::Bottom}},
                {QStringLiteral("BC"), {Text::Align::Center, MText::Vertical::Bottom}},
                {QStringLiteral("BD"), {Text::Align::Right, MText::Vertical::Bottom}},
                {QStringLiteral("TL"), {Text::Align::Left, MText::Vertical::Top}},
                {QStringLiteral("TC"), {Text::Align::Center, MText::Vertical::Top}},
                {QStringLiteral("TR"), {Text::Align::Right, MText::Vertical::Top}},
                {QStringLiteral("ML"), {Text::Align::Left, MText::Vertical::Middle}},
                {QStringLiteral("MR"), {Text::Align::Right, MText::Vertical::Middle}},
                {QStringLiteral("BL"), {Text::Align::Left, MText::Vertical::Bottom}},
                {QStringLiteral("BR"), {Text::Align::Right, MText::Vertical::Bottom}},
            };
            const auto it = kCodes.find(w);
            if (it == kCodes.end())
                return false;
            text_.align = it->second.first;
            text_.vertical = it->second.second;
            toOpposite();
            return true;
        }
        if (step_ != Step::Opposite)
            return false;
        if (isAny(w, {"A", "ALTEZZA", "H", "HEIGHT"})) {
            step_ = Step::Height;
            engine->setPrompt(t("Specificare altezza <%1>:").arg(gTextHeight));
        } else if (isAny(w, {"G", "GIUSTIFICA", "J", "JUSTIFY"})) {
            step_ = Step::Justify;
            engine->setPrompt(t("Immettere giustificazione [AS/AC/AD/MS/MC/MD/BS/BC/BD] <AS>:"));
        } else if (isAny(w, {"R", "ROTAZIONE", "ROTATION"})) {
            step_ = Step::Rotation;
            engine->setPrompt(t("Specificare angolo di rotazione <%1>:").arg(gTextRotation));
        } else if (isAny(w, {"L", "LARGHEZZA", "W", "WIDTH"})) {
            step_ = Step::Width;
            engine->setPrompt(t("Specificare larghezza (0 = nessun a capo automatico):"));
        } else {
            return false;
        }
        return true;
    }

    bool number(double v) override
    {
        if (step_ == Step::Height && v > kEps) {
            gTextHeight = v;
            toOpposite();
            return true;
        }
        if (step_ == Step::Rotation) {
            gTextRotation = v;
            toOpposite();
            return true;
        }
        if (step_ == Step::Width && v >= 0.0) {
            setup();
            text_.position = first_;
            text_.width = v;
            toContent();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (step_ == Step::Content) {
            create();
        } else if (step_ == Step::First) {
            engine->finish();
        } else if (step_ != Step::Opposite) {
            toOpposite();
        }
    }

    bool wantsText() const override { return step_ == Step::Content; }

    void text(const QString& line) override
    {
        lines_ << line;
        engine->setPrompt(t("Immettere testo (Invio a vuoto per terminare):"));
    }

    void paragraph(const QString& s) override
    {
        lines_ = QStringList{s};
        create();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ != Step::Opposite)
            return;
        const Vec2 dir = polar(1.0, degToRad(gTextRotation));
        const Vec2 up = perpendicular(dir);
        const Vec2 d = cursor - first_;
        const Vec2 a = first_ + dir * dot(d, dir);
        out.push_back(Polyline({first_, a, cursor, first_ + up * dot(d, up)}, true));
    }

    std::optional<Vec2> base() const override
    {
        return step_ == Step::Rotation ? std::optional<Vec2>(first_) : std::nullopt;
    }

private:
    enum class Step { First, Opposite, Height, Justify, Rotation, Width, Content };

    void toOpposite()
    {
        step_ = Step::Opposite;
        engine->setPrompt(t("Specificare angolo opposto o [Altezza/Giustifica/Rotazione/Larghezza]:"));
    }

    void setup()
    {
        text_.height = gTextHeight;
        text_.rotation = degToRad(gTextRotation);
    }

    // The attachment point and width from the two corners of the box.
    void place(Vec2 opposite)
    {
        setup();
        const Vec2 dir = polar(1.0, text_.rotation);
        const Vec2 up = perpendicular(dir);
        const double u = dot(opposite - first_, dir);
        const double v = dot(opposite - first_, up);
        const double left = std::min(0.0, u);
        const double top = std::max(0.0, v);
        text_.width = std::abs(u);
        const double x = text_.align == Text::Align::Left ? left : text_.align == Text::Align::Center ? left + text_.width / 2.0 : left + text_.width;
        const double y = text_.vertical == MText::Vertical::Top ? top : text_.vertical == MText::Vertical::Middle ? top - std::abs(v) / 2.0 : top - std::abs(v);
        text_.position = first_ + dir * x + up * y;
    }

    void toContent()
    {
        step_ = Step::Content;
        engine->setPrompt(t("Immettere testo (Invio a vuoto per terminare):"));
        engine->requestTextEditor(t("Testo multilinea"), QString());
    }

    void create()
    {
        text_.content = lines_.join(QLatin1Char('\n')).toStdString();
        if (!lines_.join(QString()).trimmed().isEmpty())
            engine->document().add(text_);
        engine->finish();
    }

    Step step_ = Step::First;
    Vec2 first_;
    MText text_;
    QStringList lines_;
};

// Changes the content of texts, or the label of dimensions ("<>" = measured value).
class TextEditCommand : public Command {
public:
    void start() override { toPick(); }

    bool point(Vec2 p) override
    {
        if (editing_)
            return false;
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        const Shape& shape = engine->document().entity(*id)->shape;
        QString current;
        if (const auto* tx = std::get_if<Text>(&shape)) {
            current = QString::fromStdString(tx->content);
        } else if (const auto* m = std::get_if<MText>(&shape)) {
            current = QString::fromStdString(m->content);
            engine->requestTextEditor(t("Modifica testo multilinea"), current);
            current.replace(QLatin1Char('\n'), QLatin1Char(' '));
        } else if (const auto* d = std::get_if<Dimension>(&shape)) {
            current = d->text.empty() ? QStringLiteral("<>") : QString::fromStdString(d->text);
        } else {
            engine->message(t("L'oggetto selezionato non è un testo né una quota."));
            return false;
        }
        target_ = *id;
        editing_ = true;
        engine->setPrompt(t("Immettere nuovo testo <%1>:").arg(current));
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (editing_ || !isExitWord(w))
            return false;
        engine->finish();
        return true;
    }

    void enter() override
    {
        if (editing_)
            toPick();  // keep the current text
        else
            engine->finish();
    }

    bool wantsObjectPick() const override { return !editing_; }
    bool wantsText() const override { return editing_; }

    void paragraph(const QString& s) override { text(s); }

    void text(const QString& s) override
    {
        Document& doc = engine->document();
        if (const Entity* e = doc.entity(target_)) {
            Shape shape = e->shape;
            if (auto* tx = std::get_if<Text>(&shape))
                tx->content = s.toStdString();
            else if (auto* m = std::get_if<MText>(&shape))
                m->content = s.toStdString();
            else if (auto* d = std::get_if<Dimension>(&shape))
                d->text = s.trimmed() == QLatin1String("<>") ? std::string() : s.toStdString();
            doc.replace(target_, shape);
        }
        toPick();
    }

private:
    void toPick()
    {
        editing_ = false;
        engine->setPrompt(t("Selezionare un testo o una quota o [Esci]:"));
    }

    bool editing_ = false;
    EntityId target_ = 0;
};

// New dimensions use the ISO-25 sizes (2.5 units) times DIMSCALE.
Dimension newDimension(Dimension::Kind kind)
{
    Dimension d;
    d.kind = kind;
    d.textHeight = 2.5 * gDimScale;
    d.arrowSize = 2.5 * gDimScale;
    return d;
}

void reportDimension(CommandEngine* engine, const Dimension& d)
{
    engine->message(t("Testo di quota = %1").arg(QString::fromStdString(dimensionGeometry(d).label.content)));
}

class DimLinearCommand : public Command {
public:
    explicit DimLinearCommand(bool aligned) : aligned_(aligned) {}

    void start() override
    {
        engine->setPrompt(t("Specificare origine prima linea di estensione o <seleziona oggetto>:"));
    }

    void enter() override
    {
        if (step_ == Step::First) {
            step_ = Step::Object;
            engine->setPrompt(t("Selezionare oggetto da quotare:"));
        } else if (step_ == Step::Text || step_ == Step::Angle) {
            toLocation();
        } else {
            engine->finish();
        }
    }

    bool point(Vec2 p) override
    {
        switch (step_) {
        case Step::First:
            p1_ = p;
            step_ = Step::Second;
            engine->setPrompt(t("Specificare origine seconda linea di estensione:"));
            return true;
        case Step::Second:
            if (almostEqual(p, p1_))
                return false;
            p2_ = p;
            toLocation();
            return true;
        case Step::Object:
            return pickEnds(p);
        case Step::Location: {
            const Dimension d = build(p);
            engine->document().add(d);
            reportDimension(engine, d);
            engine->finish();
            return true;
        }
        default:
            return false;
        }
    }

    bool number(double value) override
    {
        if (step_ != Step::Angle)
            return false;
        orientation_ = Orientation::Rotated;
        rotation_ = degToRad(value);
        toLocation();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Location)
            return false;
        if (isAny(w, {"T", "TESTO", "TEXT"})) {
            step_ = Step::Text;
            engine->setPrompt(t("Immettere testo di quota (<> = valore misurato):"));
            return true;
        }
        if (aligned_)
            return false;
        if (isAny(w, {"O", "ORIZZONTALE", "H", "HORIZONTAL"})) {
            orientation_ = Orientation::Horizontal;
        } else if (isAny(w, {"V", "VERTICALE", "VERTICAL"})) {
            orientation_ = Orientation::Vertical;
        } else if (isAny(w, {"R", "RUOTATO", "ROTATED"})) {
            step_ = Step::Angle;
            engine->setPrompt(t("Specificare angolo della linea di quota <%1>:").arg(radToDeg(rotation_)));
            return true;
        } else {
            return false;
        }
        toLocation();
        return true;
    }

    bool wantsObjectPick() const override { return step_ == Step::Object; }
    bool wantsText() const override { return step_ == Step::Text; }

    void text(const QString& s) override
    {
        text_ = s.toStdString();
        toLocation();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Second)
            out.push_back(Line{p1_, cursor});
        else if (step_ == Step::Location)
            out.push_back(build(cursor));
    }

    std::optional<Vec2> base() const override
    {
        return step_ == Step::Second ? std::optional<Vec2>(p1_) : std::nullopt;
    }

private:
    enum class Step { First, Second, Object, Location, Text, Angle };
    enum class Orientation { Auto, Horizontal, Vertical, Rotated };

    void toLocation()
    {
        step_ = Step::Location;
        engine->setPrompt(aligned_ ? t("Specificare posizione della linea di quota o [Testo]:")
                                   : t("Specificare posizione della linea di quota o [Testo/Orizzontale/Verticale/Ruotato]:"));
    }

    bool pickEnds(Vec2 p)
    {
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        const Shape& shape = engine->document().entity(*id)->shape;
        if (const auto* l = std::get_if<Line>(&shape)) {
            p1_ = l->a;
            p2_ = l->b;
        } else if (const auto* pl = std::get_if<Polyline>(&shape)) {
            // The segment under the pick box.
            double best = std::numeric_limits<double>::infinity();
            const size_t n = pl->points.size();
            for (size_t i = 0; i < n; ++i) {
                if (i + 1 == n && !pl->closed)
                    break;
                const Vec2 a = pl->points[i];
                const Vec2 b = pl->points[(i + 1) % n];
                const double d = distanceToSegment(p, a, b);
                if (d < best) {
                    best = d;
                    p1_ = a;
                    p2_ = b;
                }
            }
        } else {
            engine->message(t("Selezionare una linea o un segmento di polilinea."));
            return false;
        }
        toLocation();
        return true;
    }

    Dimension build(Vec2 location) const
    {
        Dimension d = newDimension(aligned_ ? Dimension::Kind::Aligned : Dimension::Kind::Linear);
        d.p1 = p1_;
        d.p2 = p2_;
        d.location = location;
        d.text = text_;
        switch (orientation_) {
        case Orientation::Auto: d.angle = aligned_ ? angleOf(p2_ - p1_) : autoAngle(location); break;
        case Orientation::Horizontal: d.angle = 0.0; break;
        case Orientation::Vertical: d.angle = kPi / 2.0; break;
        case Orientation::Rotated: d.angle = rotation_; break;
        }
        return d;
    }

    // As in AutoCAD: a horizontal dimension when the cursor is above or
    // below the two points, a vertical one when it is beside them.
    double autoAngle(Vec2 location) const
    {
        const BBox box = BBox::fromCorners(p1_, p2_);
        const double outX = std::max({box.min.x - location.x, location.x - box.max.x, 0.0});
        const double outY = std::max({box.min.y - location.y, location.y - box.max.y, 0.0});
        return outX > outY ? kPi / 2.0 : 0.0;
    }

    bool aligned_;
    Step step_ = Step::First;
    Orientation orientation_ = Orientation::Auto;
    double rotation_ = 0.0;
    Vec2 p1_, p2_;
    std::string text_;
};

class DimRadialCommand : public Command {
public:
    explicit DimRadialCommand(bool diameter) : diameter_(diameter) {}

    void start() override { engine->setPrompt(t("Selezionare arco o cerchio:")); }

    bool point(Vec2 p) override
    {
        if (step_ == Step::Location) {
            const Dimension d = build(p);
            engine->document().add(d);
            reportDimension(engine, d);
            engine->finish();
            return true;
        }
        if (step_ != Step::Pick)
            return false;
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        const Shape& shape = engine->document().entity(*id)->shape;
        if (const auto* c = std::get_if<Circle>(&shape)) {
            center_ = c->center;
            radius_ = c->radius;
        } else if (const auto* a = std::get_if<Arc>(&shape)) {
            center_ = a->center;
            radius_ = a->radius;
        } else {
            engine->message(t("Selezionare un arco o un cerchio."));
            return false;
        }
        toLocation();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Location || !isAny(w, {"T", "TESTO", "TEXT"}))
            return false;
        step_ = Step::Text;
        engine->setPrompt(t("Immettere testo di quota (<> = valore misurato):"));
        return true;
    }

    void enter() override
    {
        if (step_ == Step::Text)
            toLocation();
        else
            engine->finish();
    }

    bool wantsObjectPick() const override { return step_ == Step::Pick; }
    bool wantsText() const override { return step_ == Step::Text; }

    void text(const QString& s) override
    {
        text_ = s.toStdString();
        toLocation();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Location)
            out.push_back(build(cursor));
    }

private:
    enum class Step { Pick, Location, Text };

    void toLocation()
    {
        step_ = Step::Location;
        engine->setPrompt(t("Specificare posizione della linea di quota o [Testo]:"));
    }

    Dimension build(Vec2 location) const
    {
        Dimension d = newDimension(diameter_ ? Dimension::Kind::Diameter : Dimension::Kind::Radius);
        Vec2 u = normalized(location - center_);
        if (length(u) < kEps)
            u = {1.0, 0.0};
        d.p1 = center_;
        d.p2 = center_ + u * radius_;
        d.location = location;
        d.text = text_;
        return d;
    }

    bool diameter_;
    Step step_ = Step::Pick;
    Vec2 center_;
    double radius_ = 0.0;
    std::string text_;
};

// DIMANGULAR: the angle between two lines, of an arc, of two points on a
// circle, or of three points (vertex and the two sides).
class DimAngularCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Selezionare arco, cerchio, linea o <specificare vertice>:")); }

    void enter() override
    {
        if (step_ == Step::Pick) {
            step_ = Step::Vertex;
            engine->setPrompt(t("Specificare vertice dell'angolo:"));
        } else if (step_ == Step::Text) {
            toLocation();
        } else {
            engine->finish();
        }
    }

    bool point(Vec2 p) override
    {
        switch (step_) {
        case Step::Pick: return pickFirst(p);
        case Step::SecondLine: {
            const auto id = pickObject(engine, p);
            if (!id)
                return false;
            const auto* l = std::get_if<Line>(&engine->document().entity(*id)->shape);
            if (!l) {
                engine->message(t("Selezionare una linea."));
                return false;
            }
            Dimension probe;
            probe.location = p;
            if (!setAngularLines(probe, first_, *l)) {
                engine->message(t("Le linee sono parallele."));
                return false;
            }
            second_ = *l;
            lines_ = true;
            toLocation();
            return true;
        }
        case Step::Vertex:
            vertex_ = p;
            step_ = Step::FirstSide;
            engine->setPrompt(t("Specificare primo estremo dell'angolo:"));
            return true;
        case Step::FirstSide:
            if (almostEqual(p, vertex_))
                return false;
            p1_ = p;
            step_ = Step::SecondSide;
            engine->setPrompt(t("Specificare secondo estremo dell'angolo:"));
            return true;
        case Step::SecondSide:
            if (almostEqual(p, vertex_))
                return false;
            p2_ = p;
            toLocation();
            return true;
        case Step::Location: {
            const Dimension d = build(p);
            engine->document().add(d);
            reportDimension(engine, d);
            engine->finish();
            return true;
        }
        default:
            return false;
        }
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Location || !isAny(w, {"T", "TESTO", "TEXT"}))
            return false;
        step_ = Step::Text;
        engine->setPrompt(t("Immettere testo di quota (<> = valore misurato):"));
        return true;
    }

    bool wantsObjectPick() const override { return step_ == Step::Pick || step_ == Step::SecondLine; }
    bool wantsText() const override { return step_ == Step::Text; }

    void text(const QString& s) override
    {
        text_ = s.toStdString();
        toLocation();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::FirstSide || step_ == Step::SecondSide)
            out.push_back(Line{vertex_, cursor});
        if (step_ == Step::SecondSide)
            out.push_back(Line{vertex_, p1_});
        else if (step_ == Step::Location)
            out.push_back(build(cursor));
    }

    std::optional<Vec2> base() const override
    {
        return step_ == Step::FirstSide || step_ == Step::SecondSide ? std::optional<Vec2>(vertex_) : std::nullopt;
    }

private:
    enum class Step { Pick, SecondLine, Vertex, FirstSide, SecondSide, Location, Text };

    bool pickFirst(Vec2 p)
    {
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        const Shape& shape = engine->document().entity(*id)->shape;
        if (const auto* l = std::get_if<Line>(&shape)) {
            first_ = *l;
            step_ = Step::SecondLine;
            engine->setPrompt(t("Selezionare seconda linea:"));
        } else if (const auto* a = std::get_if<Arc>(&shape)) {
            vertex_ = a->center;
            p1_ = a->startPoint();
            p2_ = a->endPoint();
            toLocation();
        } else if (const auto* c = std::get_if<Circle>(&shape)) {
            // The pick point is the first side, as in AutoCAD.
            vertex_ = c->center;
            p1_ = c->center + normalized(p - c->center) * c->radius;
            step_ = Step::SecondSide;
            engine->setPrompt(t("Specificare secondo estremo dell'angolo:"));
        } else {
            engine->message(t("Selezionare un arco, un cerchio o una linea."));
            return false;
        }
        return true;
    }

    void toLocation()
    {
        step_ = Step::Location;
        engine->setPrompt(t("Specificare posizione dell'arco di quota o [Testo]:"));
    }

    Dimension build(Vec2 location) const
    {
        Dimension d = newDimension(Dimension::Kind::Angular);
        d.location = location;
        d.text = text_;
        if (lines_) {
            setAngularLines(d, first_, second_);
        } else {
            d.vertex = vertex_;
            d.p1 = p1_;
            d.p2 = p2_;
        }
        return d;
    }

    Step step_ = Step::Pick;
    Line first_, second_;
    bool lines_ = false;
    Vec2 vertex_, p1_, p2_;
    std::string text_;
};

// DIMCONTINUE and DIMBASELINE: more linear dimensions on from the last one
// (or a chosen one), on the same dimension line or each a step further out.
class DimChainCommand : public Command {
public:
    explicit DimChainCommand(bool baseline) : baseline_(baseline) {}

    void start() override
    {
        const Document& doc = engine->document();
        for (auto it = doc.entities().rbegin(); it != doc.entities().rend(); ++it) {
            if (doc.isVisible(it->second) && setBase(it->second.shape))
                break;
        }
        if (hasBase_)
            toNext();
        else
            toSelect();
    }

    void enter() override
    {
        if (step_ == Step::Next && added_.empty() && !picked_)
            toSelect();
        else
            engine->finish();
    }

    bool point(Vec2 p) override
    {
        if (step_ == Step::Select) {
            const auto id = pickObject(engine, p);
            if (!id)
                return false;
            if (!setBase(engine->document().entity(*id)->shape)) {
                engine->message(t("Selezionare una quota lineare o allineata."));
                return false;
            }
            picked_ = true;
            toNext();
            return true;
        }
        if (step_ != Step::Next)
            return false;
        const Dimension d = build(p);
        if (std::abs(d.measurement()) < kEps)
            return false;
        added_.push_back({engine->document().add(d), base_});
        reportDimension(engine, d);
        base_ = d;
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Next)
            return false;
        if (isAny(w, {"S", "SELEZIONE", "SELECT"})) {
            toSelect();
            return true;
        }
        if (isAny(w, {"A", "ANNULLA", "U", "UNDO"})) {
            if (added_.empty()) {
                engine->message(t("Nessuna quota da annullare."));
                return true;
            }
            engine->document().remove(added_.back().first);
            base_ = added_.back().second;
            added_.pop_back();
            return true;
        }
        return false;
    }

    bool wantsObjectPick() const override { return step_ == Step::Select; }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Next)
            out.push_back(build(cursor));
    }

private:
    enum class Step { Select, Next };

    bool setBase(const Shape& shape)
    {
        const auto* d = std::get_if<Dimension>(&shape);
        if (!d || (d->kind != Dimension::Kind::Linear && d->kind != Dimension::Kind::Aligned))
            return false;
        base_ = *d;
        hasBase_ = true;
        return true;
    }

    void toSelect()
    {
        step_ = Step::Select;
        engine->setPrompt(baseline_ ? t("Selezionare quota di base:") : t("Selezionare quota continua:"));
    }

    void toNext()
    {
        step_ = Step::Next;
        engine->setPrompt(t("Specificare origine seconda linea di estensione o [Annulla/Selezione] <Selezione>:"));
    }

    Vec2 direction() const
    {
        Vec2 dir = base_.kind == Dimension::Kind::Aligned ? normalized(base_.p2 - base_.p1) : polar(1.0, base_.angle);
        return length(dir) < kEps ? Vec2{1.0, 0.0} : dir;
    }

    // Continued: from the base's second extension line, on its dimension
    // line. Baseline: from its first extension line, one step (DIMDLI,
    // 3.75 in ISO-25) further from the object.
    Dimension build(Vec2 p) const
    {
        const Vec2 dir = direction();
        const Vec2 n = perpendicular(dir);
        Dimension d = newDimension(Dimension::Kind::Linear);
        d.textHeight = base_.textHeight;
        d.arrowSize = base_.arrowSize;
        d.angle = angleOf(dir);
        d.p2 = p;
        if (baseline_) {
            d.p1 = base_.p1;
            const double side = dot(base_.location - base_.p1, n) < 0.0 ? -1.0 : 1.0;
            d.location = base_.location + n * (side * 1.5 * base_.arrowSize);
        } else {
            d.p1 = base_.p2;
            d.location = base_.location;
        }
        return d;
    }

    bool baseline_;
    Step step_ = Step::Next;
    Dimension base_;
    bool hasBase_ = false;
    bool picked_ = false;
    std::vector<std::pair<EntityId, Dimension>> added_;
};

class DimScaleCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Immettere nuovo valore per DIMSCALE <%1>:").arg(gDimScale));
    }

    bool number(double value) override
    {
        if (value <= kEps) {
            engine->message(t("Il valore deve essere positivo."));
            return true;
        }
        gDimScale = value;
        engine->finish();
        return true;
    }
};

// ---------------------------------------------------------------- hatch

// Remembered between runs, like AutoCAD's HPNAME, HPSCALE and HPANG.
std::string gHatchPattern = "ANSI31";
double gHatchScale = 1.0;
double gHatchAngle = 0.0;  // degrees

QString patternList()
{
    QStringList names;
    for (const std::string& n : hatchPatternNames())
        names << QString::fromStdString(n);
    return t("Motivi disponibili: %1").arg(names.join(QStringLiteral(", ")));
}

// Shared by HATCH and HATCHEDIT: the Motivo, Scala and Angolo options.
class HatchSettings {
public:
    enum class Ask { None, Pattern, Scale, Angle };

    // Starts asking for an option; false when `w` is not one of them.
    bool option(CommandEngine* engine, const QString& w, const std::string& pattern, double scale, double angleDeg)
    {
        if (isAny(w, {"M", "MOTIVO", "P", "PATTERN"})) {
            ask_ = Ask::Pattern;
            engine->setPrompt(t("Specificare nome del motivo o [?] <%1>:").arg(QString::fromStdString(pattern)));
        } else if (isAny(w, {"S", "SCALA", "SCALE"})) {
            ask_ = Ask::Scale;
            engine->setPrompt(t("Specificare scala del motivo <%1>:").arg(scale));
        } else if (isAny(w, {"AN", "ANGOLO", "ANGLE"})) {
            ask_ = Ask::Angle;
            engine->setPrompt(t("Specificare angolo del motivo <%1>:").arg(angleDeg));
        } else {
            return false;
        }
        return true;
    }

    Ask asking() const { return ask_; }
    void done() { ask_ = Ask::None; }

    // A pattern name typed while asking for it. Returns the valid name.
    std::optional<std::string> patternName(CommandEngine* engine, const QString& w)
    {
        if (w == QLatin1String("?")) {
            engine->message(patternList());
            return std::nullopt;
        }
        const std::string name = w.toStdString();
        if (!hatchPattern(name)) {
            engine->message(t("Motivo sconosciuto: %1. %2").arg(w, patternList()));
            return std::nullopt;
        }
        return name;
    }

private:
    Ask ask_ = Ask::None;
};

void reportDensity(CommandEngine* engine, const Hatch& h)
{
    const BBox box = boundingBox(h);
    const double spacing = hatchSpacing(h);
    if (spacing > 0.0 && spacing * 1000.0 < std::max(box.width(), box.height()))
        engine->message(t("Il motivo è molto fitto per questo disegno e appare come una campitura: aumentare la Scala "
                          "(per esempio %1).").arg(std::max(box.width(), box.height()) / 50.0 / hatchSpacing(makeHatch({}, h.pattern, 1, 0)), 0, 'g', 3));
}

class HatchCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Motivo corrente: %1, scala %2, angolo %3")
                            .arg(QString::fromStdString(gHatchPattern))
                            .arg(gHatchScale)
                            .arg(gHatchAngle));
        toPoint();
    }

    bool keyword(const QString& w) override
    {
        if (settings_.asking() == HatchSettings::Ask::Pattern) {
            if (const auto name = settings_.patternName(engine, w)) {
                gHatchPattern = *name;
                toPoint();
            }
            return true;
        }
        if (settings_.asking() != HatchSettings::Ask::None)
            return false;
        if (settings_.option(engine, w, gHatchPattern, gHatchScale, gHatchAngle))
            return true;
        if (!objects_ && isAny(w, {"O", "OGGETTI", "SELEZIONA", "SE", "SELECT"})) {
            objects_ = true;
            toPoint();
            return true;
        }
        if (objects_ && isAny(w, {"P", "PUNTO", "K", "PICK"})) {
            objects_ = false;
            toPoint();
            return true;
        }
        if (isAny(w, {"ANN", "ANNULLA", "U", "UNDO"}) && !regions_.empty()) {
            regions_.pop_back();
            toPoint();
            return true;
        }
        return false;
    }

    bool number(double value) override
    {
        switch (settings_.asking()) {
        case HatchSettings::Ask::Scale:
            if (value <= 0.0) {
                engine->message(t("La scala deve essere positiva."));
                return true;
            }
            gHatchScale = value;
            break;
        case HatchSettings::Ask::Angle:
            gHatchAngle = value;
            break;
        default:
            return false;
        }
        toPoint();
        return true;
    }

    void enter() override
    {
        if (settings_.asking() != HatchSettings::Ask::None) {
            toPoint();
            return;
        }
        if (!regions_.empty()) {
            const Hatch h = current();
            engine->document().add(h);
            reportDensity(engine, h);
        }
        engine->finish();
    }

    bool point(Vec2 p) override
    {
        if (settings_.asking() != HatchSettings::Ask::None)
            return false;
        if (objects_)
            return pickObject(p);
        std::vector<Shape> shapes;
        const Document& doc = engine->document();
        // Walls bound rooms by their joined outline, as for SLAB.
        const auto walls = joinedWalls(doc);
        for (const auto& [id, e] : doc.entities()) {
            if (!doc.isVisible(e))
                continue;
            if (std::holds_alternative<Wall>(e.shape))
                shapes.push_back(wallOutline(walls.at(id)));
            else
                shapes.push_back(e.shape);
        }
        auto loops = findHatchBoundary(shapes, p);
        if (loops.empty()) {
            engine->message(t("Impossibile trovare un contorno chiuso attorno al punto."));
            return false;
        }
        regions_.push_back(std::move(loops));
        toPoint();
        return true;
    }

    bool wantsObjectPick() const override { return settings_.asking() == HatchSettings::Ask::None; }

    void preview(Vec2, std::vector<Shape>& out) const override
    {
        if (!regions_.empty())
            out.push_back(current());
    }

private:
    bool pickObject(Vec2 p)
    {
        const auto id = cad::pickObject(engine, p);
        if (!id)
            return false;
        const Shape& s = engine->document().entity(*id)->shape;
        Polyline loop;
        if (const auto* pl = std::get_if<Polyline>(&s); pl && (pl->closed || almostEqual(pl->points.front(), pl->points.back()))) {
            loop = *pl;
            if (!pl->closed) {
                loop.points.pop_back();
                loop.bulges.resize(std::min(loop.bulges.size(), loop.points.size()));
            }
        } else if (const auto* c = std::get_if<Circle>(&s)) {
            loop = Polyline{{c->center + Vec2{c->radius, 0}, c->center - Vec2{c->radius, 0}}, true, {1.0, 1.0}};
        } else {
            engine->message(t("Selezionare una polilinea chiusa o un cerchio."));
            return false;
        }
        regions_.push_back({loop});
        toPoint();
        return true;
    }

    Hatch current() const
    {
        std::vector<Polyline> loops;
        for (const auto& region : regions_)
            loops.insert(loops.end(), region.begin(), region.end());
        return makeHatch(std::move(loops), gHatchPattern, gHatchScale, degToRad(gHatchAngle));
    }

    void toPoint()
    {
        settings_.done();
        const QString undo = regions_.empty() ? QString() : t("/ANNulla");
        if (objects_)
            engine->setPrompt(t("Selezionare oggetti o [Punto/Motivo/Scala/ANgolo%1]:").arg(undo));
        else
            engine->setPrompt(t("Specificare punto interno o [Oggetti/Motivo/Scala/ANgolo%1]:").arg(undo));
    }

    HatchSettings settings_;
    bool objects_ = false;
    std::vector<std::vector<Polyline>> regions_;
};

class HatchEditCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Selezionare tratteggio:")); }

    bool point(Vec2 p) override
    {
        if (id_)
            return false;
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        if (!std::holds_alternative<Hatch>(engine->document().entity(*id)->shape)) {
            engine->message(t("L'oggetto selezionato non è un tratteggio."));
            return false;
        }
        id_ = *id;
        toOption();
        return true;
    }

    bool wantsObjectPick() const override { return !id_; }

    bool keyword(const QString& w) override
    {
        if (!id_)
            return false;
        Hatch h = hatch();
        if (settings_.asking() == HatchSettings::Ask::Pattern) {
            if (const auto name = settings_.patternName(engine, w)) {
                const Hatch changed = makeHatch(h.loops, *name, h.scale, h.angle);
                h.pattern = changed.pattern;
                h.lines = changed.lines;
                update(h);
            }
            return true;
        }
        if (settings_.asking() != HatchSettings::Ask::None)
            return false;
        return settings_.option(engine, w, h.pattern, h.scale, radToDeg(h.angle));
    }

    bool number(double value) override
    {
        if (!id_)
            return false;
        Hatch h = hatch();
        if (settings_.asking() == HatchSettings::Ask::Scale && value > 0.0)
            h.scale = value;
        else if (settings_.asking() == HatchSettings::Ask::Angle)
            h.angle = degToRad(value);
        else
            return false;
        update(h);
        return true;
    }

    void enter() override
    {
        if (settings_.asking() != HatchSettings::Ask::None)
            toOption();
        else
            engine->finish();
    }

private:
    Hatch hatch() const { return std::get<Hatch>(engine->document().entity(*id_)->shape); }

    void update(const Hatch& h)
    {
        engine->document().replace(*id_, h);
        reportDensity(engine, h);
        toOption();
    }

    void toOption()
    {
        settings_.done();
        engine->setPrompt(t("Specificare opzione [Motivo/Scala/ANgolo]:"));
    }

    HatchSettings settings_;
    std::optional<EntityId> id_;
};

// ---------------------------------------------------------------- walls, doors, windows

// Remembered between runs. Sizes in drawing units (millimetres by default).
// The tool settings are shared with the windows (see properties.h).
double& gWallThickness = toolSettings().wallThickness;
double& gWallHeight = toolSettings().wallHeight;
Wall::Justification& gWallJustification = toolSettings().wallJustification;

QString justificationName(Wall::Justification j)
{
    switch (j) {
    case Wall::Justification::Left:
        return t("Sinistra");
    case Wall::Justification::Right:
        return t("Destra");
    case Wall::Justification::Center:
        break;
    }
    return t("Centro");
}

std::optional<Wall::Justification> justificationWord(const QString& w)
{
    if (isAny(w, {"C", "CENTRO", "CENTER"}))
        return Wall::Justification::Center;
    if (isAny(w, {"S", "SINISTRA", "L", "LEFT"}))
        return Wall::Justification::Left;
    if (isAny(w, {"D", "DESTRA", "R", "RIGHT"}))
        return Wall::Justification::Right;
    return std::nullopt;
}

// Shared by WALL and WALLEDIT: the Spessore, Altezza and Giustificazione options.
class WallSettings {
public:
    enum class Ask { None, Thickness, Height, Justification };

    bool option(CommandEngine* engine, const QString& w, double thickness, double height, Wall::Justification j)
    {
        if (isAny(w, {"S", "SPESSORE", "W", "WIDTH", "THICKNESS"})) {
            ask_ = Ask::Thickness;
            engine->setPrompt(t("Specificare spessore del muro <%1>:").arg(thickness));
        } else if (isAny(w, {"A", "ALTEZZA", "H", "HEIGHT"})) {
            ask_ = Ask::Height;
            engine->setPrompt(t("Specificare altezza del muro <%1>:").arg(height));
        } else if (isAny(w, {"G", "GIUSTIFICAZIONE", "J", "JUSTIFY"})) {
            ask_ = Ask::Justification;
            engine->setPrompt(t("Specificare giustificazione [Centro/Sinistra/Destra] <%1>:").arg(justificationName(j)));
        } else {
            return false;
        }
        return true;
    }

    Ask asking() const { return ask_; }
    void done() { ask_ = Ask::None; }

private:
    Ask ask_ = Ask::None;
};

// WALL: a chain of walls, like LINE. Each segment is a wall of its own, and
// consecutive walls are joined at their corners.
class WallCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Muro corrente: spessore %1, altezza %2, giustificazione %3")
                            .arg(gWallThickness)
                            .arg(gWallHeight)
                            .arg(justificationName(gWallJustification)));
        updatePrompt();
    }

    bool point(Vec2 p) override
    {
        if (settings_.asking() != WallSettings::Ask::None)
            return false;
        if (!points_.empty()) {
            if (almostEqual(p, points_.back()))
                return false;
            walls_.push_back(engine->document().add(makeWall(points_.back(), p)));
        }
        points_.push_back(p);
        updatePrompt();
        return true;
    }

    bool number(double value) override
    {
        if (settings_.asking() == WallSettings::Ask::Thickness && value > 0.0)
            gWallThickness = value;
        else if (settings_.asking() == WallSettings::Ask::Height && value > 0.0)
            gWallHeight = value;
        else
            return false;
        settings_.done();
        updatePrompt();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (settings_.asking() == WallSettings::Ask::Justification) {
            const auto j = justificationWord(w);
            if (!j)
                return false;
            gWallJustification = *j;
            settings_.done();
            updatePrompt();
            return true;
        }
        if (settings_.asking() != WallSettings::Ask::None)
            return false;
        if (settings_.option(engine, w, gWallThickness, gWallHeight, gWallJustification))
            return true;
        if (isAny(w, {"AN", "ANNULLA", "U", "UNDO"}) && !points_.empty()) {
            if (!walls_.empty()) {
                engine->document().remove(walls_.back());
                walls_.pop_back();
            }
            points_.pop_back();
            updatePrompt();
            return true;
        }
        if (isCloseWord(w) && walls_.size() >= 2) {
            engine->document().add(makeWall(points_.back(), points_.front()));
            engine->finish();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (settings_.asking() != WallSettings::Ask::None) {
            settings_.done();
            updatePrompt();
            return;
        }
        engine->finish();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (!points_.empty() && settings_.asking() == WallSettings::Ask::None && !almostEqual(cursor, points_.back()))
            out.push_back(makeWall(points_.back(), cursor));
    }

    std::optional<Vec2> base() const override
    {
        if (points_.empty() || settings_.asking() != WallSettings::Ask::None)
            return std::nullopt;
        return points_.back();
    }

private:
    static Wall makeWall(Vec2 a, Vec2 b)
    {
        Wall w;
        w.start = a;
        w.end = b;
        w.thickness = gWallThickness;
        w.height = gWallHeight;
        w.justification = gWallJustification;
        return w;
    }

    void updatePrompt()
    {
        if (points_.empty())
            engine->setPrompt(t("Specificare punto iniziale o [Spessore/Altezza/Giustificazione]:"));
        else if (walls_.size() >= 2)
            engine->setPrompt(t("Specificare punto successivo o [Spessore/Altezza/Giustificazione/Chiudi/ANnulla]:"));
        else
            engine->setPrompt(t("Specificare punto successivo o [Spessore/Altezza/Giustificazione/ANnulla]:"));
    }

    WallSettings settings_;
    std::vector<Vec2> points_;
    std::vector<EntityId> walls_;
};

// The wall under `p`, if any.
std::optional<EntityId> pickWall(CommandEngine* engine, Vec2 p)
{
    std::optional<EntityId> best;
    double bestDist = engine->pickTolerance();
    const Document& doc = engine->document();
    for (const auto& [id, e] : doc.entities()) {
        if (!std::holds_alternative<Wall>(e.shape) || !doc.isVisible(e))
            continue;
        const double d = distanceTo(e.shape, p);
        if (d <= bestDist) {
            bestDist = d;
            best = id;
        }
    }
    return best;
}

// Remembered between runs, separately for doors and windows.
Opening& gDoor = toolSettings().door;
Opening& gWindow = toolSettings().window;

// DOOR and WINDOW: places openings in walls, centred on the picked point.
// A door then asks the side it opens towards.
class OpeningCommand : public Command {
public:
    explicit OpeningCommand(Opening::Kind kind) : kind_(kind) {}

    void start() override
    {
        const Opening& o = defaults();
        if (door())
            engine->message(t("Porta corrente: larghezza %1, altezza %2").arg(o.width).arg(o.height));
        else
            engine->message(t("Finestra corrente: larghezza %1, altezza %2, davanzale %3").arg(o.width).arg(o.height).arg(o.sill));
        toPoint();
    }

    bool point(Vec2 p) override
    {
        if (ask_ != Ask::None)
            return false;
        if (placed_) {
            // The side the door opens to: the leaf lies on the jamb nearer to it.
            Opening o = *placed_;
            orient(o, p);
            add(o);
            return true;
        }
        const auto id = pickWall(engine, p);
        if (!id) {
            engine->message(t("Nessun muro trovato in questo punto."));
            return false;
        }
        const Wall& w = std::get<Wall>(engine->document().entity(*id)->shape);
        Opening o = defaults();
        o.position = wallParam(w, p);
        const auto fitted = fitOpening(w, o);
        if (!fitted) {
            engine->message(door() ? t("La porta non entra nel muro in questo punto.")
                                   : t("La finestra non entra nel muro in questo punto."));
            return false;
        }
        wall_ = *id;
        if (!door()) {
            add(*fitted);
            return true;
        }
        placed_ = fitted;
        engine->setPrompt(t("Specificare lato di apertura:"));
        return true;
    }

    bool number(double value) override
    {
        Opening& o = defaults();
        if (value <= 0.0 && ask_ != Ask::Sill)
            return false;
        switch (ask_) {
        case Ask::Width:
            o.width = value;
            break;
        case Ask::Height:
            o.height = value;
            break;
        case Ask::Sill:
            if (value < 0.0)
                return false;
            o.sill = value;
            break;
        case Ask::None:
            return false;
        }
        toPoint();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ != Ask::None || placed_)
            return false;
        const Opening& o = defaults();
        if (isAny(w, {"L", "LARGHEZZA", "W", "WIDTH"})) {
            ask_ = Ask::Width;
            engine->setPrompt(t("Specificare larghezza <%1>:").arg(o.width));
        } else if (isAny(w, {"A", "ALTEZZA", "H", "HEIGHT"})) {
            ask_ = Ask::Height;
            engine->setPrompt(t("Specificare altezza <%1>:").arg(o.height));
        } else if (!door() && isAny(w, {"D", "DAVANZALE", "S", "SILL"})) {
            ask_ = Ask::Sill;
            engine->setPrompt(t("Specificare altezza del davanzale <%1>:").arg(o.sill));
        } else {
            return false;
        }
        return true;
    }

    void enter() override
    {
        if (placed_) {
            add(*placed_);
            return;
        }
        if (ask_ != Ask::None) {
            toPoint();
            return;
        }
        engine->finish();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (placed_) {
            Opening o = *placed_;
            orient(o, cursor);
            if (const auto w = wallWith(*wall_, o))
                out.push_back(*w);
            return;
        }
        if (ask_ != Ask::None)
            return;
        if (const auto id = pickWall(engine, cursor)) {
            const Wall& w = std::get<Wall>(engine->document().entity(*id)->shape);
            Opening o = defaults();
            o.position = wallParam(w, cursor);
            if (const auto fitted = fitOpening(w, o))
                if (const auto shown = wallWith(*id, *fitted))
                    out.push_back(*shown);
        }
    }

private:
    enum class Ask { None, Width, Height, Sill };

    bool door() const { return kind_ == Opening::Kind::Door; }
    Opening& defaults() const { return door() ? gDoor : gWindow; }

    void orient(Opening& o, Vec2 p) const
    {
        const Wall& w = std::get<Wall>(engine->document().entity(*wall_)->shape);
        const Vec2 c = wallPoint(w, o.position);
        o.swingRight = dot(p - c, wallNormal(w)) < 0.0;
        o.hingeAtEnd = wallParam(w, p) > o.position;
    }

    std::optional<Wall> wallWith(EntityId id, const Opening& o) const
    {
        const Entity* e = engine->document().entity(id);
        if (!e || !std::holds_alternative<Wall>(e->shape))
            return std::nullopt;
        Wall w = std::get<Wall>(e->shape);
        w.openings.push_back(o);
        return w;
    }

    void add(const Opening& o)
    {
        if (const auto w = wallWith(*wall_, o))
            engine->document().replace(*wall_, *w);
        placed_.reset();
        wall_.reset();
        toPoint();
    }

    void toPoint()
    {
        ask_ = Ask::None;
        engine->setPrompt(door() ? t("Specificare punto sul muro o [Larghezza/Altezza]:")
                                 : t("Specificare punto sul muro o [Larghezza/Altezza/Davanzale]:"));
    }

    Opening::Kind kind_;
    Ask ask_ = Ask::None;
    std::optional<EntityId> wall_;
    std::optional<Opening> placed_;
};

// WALLEDIT: changes thickness, height and justification of a wall, or
// removes one of its doors and windows.
class WallEditCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Selezionare muro:")); }

    bool point(Vec2 p) override
    {
        if (!id_) {
            id_ = pickWall(engine, p);
            if (!id_) {
                engine->message(t("Nessun muro trovato in questo punto."));
                return false;
            }
            toOption();
            return true;
        }
        if (!removing_)
            return false;
        Wall w = wall();
        const double along = wallParam(w, p);
        const auto it = std::find_if(w.openings.begin(), w.openings.end(),
                                     [&](const Opening& o) { return along >= openingStart(o) && along <= openingEnd(o); });
        if (it == w.openings.end()) {
            engine->message(t("Nessuna porta o finestra in questo punto."));
            return false;
        }
        w.openings.erase(it);
        engine->document().replace(*id_, w);
        toOption();
        return true;
    }

    bool number(double value) override
    {
        if (!id_ || value <= 0.0)
            return false;
        Wall w = wall();
        if (settings_.asking() == WallSettings::Ask::Thickness)
            w.thickness = value;
        else if (settings_.asking() == WallSettings::Ask::Height)
            w.height = value;
        else
            return false;
        engine->document().replace(*id_, w);
        toOption();
        return true;
    }

    bool keyword(const QString& word) override
    {
        if (!id_)
            return false;
        Wall w = wall();
        if (settings_.asking() == WallSettings::Ask::Justification) {
            const auto j = justificationWord(word);
            if (!j)
                return false;
            w.justification = *j;
            engine->document().replace(*id_, w);
            toOption();
            return true;
        }
        if (settings_.asking() != WallSettings::Ask::None || removing_)
            return false;
        if (isAny(word, {"E", "ELIMINA", "R", "REMOVE"})) {
            if (w.openings.empty()) {
                engine->message(t("Il muro non ha porte né finestre."));
                return true;
            }
            removing_ = true;
            engine->setPrompt(t("Selezionare la porta o la finestra da eliminare:"));
            return true;
        }
        return settings_.option(engine, word, w.thickness, w.height, w.justification);
    }

    void enter() override
    {
        if (id_ && (settings_.asking() != WallSettings::Ask::None || removing_))
            toOption();
        else
            engine->finish();
    }

private:
    Wall wall() const { return std::get<Wall>(engine->document().entity(*id_)->shape); }

    void toOption()
    {
        settings_.done();
        removing_ = false;
        engine->setPrompt(t("Specificare opzione [Spessore/Altezza/Giustificazione/Elimina apertura]:"));
    }

    WallSettings settings_;
    std::optional<EntityId> id_;
    bool removing_ = false;
};

// ---------------------------------------------------------------- stories, slabs, roofs

QString storyList(const Document& doc)
{
    QStringList items;
    for (auto it = doc.stories().rbegin(); it != doc.stories().rend(); ++it)
        items << t("%1 (quota %2, altezza %3)%4")
                     .arg(QString::fromStdString(it->name))
                     .arg(it->elevation)
                     .arg(it->height)
                     .arg(it->id == doc.currentStory() ? t(", corrente") : QString());
    return t("Piani: %1").arg(items.join(QStringLiteral("; ")));
}

// STORY: the floors of the building. The current one is the one drawn in plan.
class StoryCommand : public Command {
public:
    void start() override
    {
        engine->message(storyList(engine->document()));
        toOption();
    }

    bool keyword(const QString& w) override
    {
        if (ask_ != Ask::None)
            return false;
        Document& doc = engine->document();
        if (isAny(w, {"N", "NUOVO", "NEW"})) {
            ask_ = Ask::NewName;
            engine->setPrompt(t("Specificare nome del nuovo piano <%1>:").arg(defaultName()));
        } else if (isAny(w, {"C", "CORRENTE", "SET"})) {
            ask_ = Ask::Current;
            engine->setPrompt(t("Specificare nome del piano da rendere corrente:"));
        } else if (isAny(w, {"S", "SU", "UP"})) {
            if (const Story* up = doc.storyAbove(doc.currentStory()))
                setCurrent(up->id);
            else
                engine->message(t("Non c'è un piano sopra."));
        } else if (isAny(w, {"G", "GIU", "GIÙ", "DOWN"})) {
            if (const Story* down = doc.storyBelow(doc.currentStory()))
                setCurrent(down->id);
            else
                engine->message(t("Non c'è un piano sotto."));
        } else if (isAny(w, {"R", "RINOMINA", "RENAME"})) {
            ask_ = Ask::Rename;
            engine->setPrompt(t("Specificare nuovo nome del piano:"));
        } else if (isAny(w, {"Q", "QUOTA", "ELEVATION"})) {
            ask_ = Ask::Elevation;
            engine->setPrompt(t("Specificare quota del piano <%1>:").arg(current().elevation));
        } else if (isAny(w, {"A", "ALTEZZA", "HEIGHT"})) {
            ask_ = Ask::Height;
            engine->setPrompt(t("Specificare altezza del piano <%1>:").arg(current().height));
        } else if (isAny(w, {"E", "ELIMINA", "DELETE"})) {
            const Story s = current();
            if (doc.removeStory(s.id)) {
                engine->clearSelection();
                engine->message(t("Piano %1 eliminato.").arg(QString::fromStdString(s.name)));
            } else {
                engine->message(t("Si può eliminare solo un piano vuoto, e non l'ultimo."));
            }
            toOption();
        } else if (w == QLatin1String("?")) {
            engine->message(storyList(doc));
        } else {
            return false;
        }
        return true;
    }

    bool number(double value) override
    {
        Story s = current();
        if (ask_ == Ask::Elevation)
            s.elevation = value;
        else if (ask_ == Ask::Height && value > 0.0)
            s.height = value;
        else
            return false;
        engine->document().updateStory(s.id, s);
        toOption();
        return true;
    }

    bool wantsText() const override { return ask_ == Ask::NewName || ask_ == Ask::Current || ask_ == Ask::Rename; }

    void text(const QString& raw) override
    {
        const std::string name = raw.trimmed().toStdString();
        Document& doc = engine->document();
        switch (ask_) {
        case Ask::NewName:
            addStory(name);
            break;
        case Ask::Current:
            if (const Story* s = findStory(raw.trimmed()))
                setCurrent(s->id);
            else
                engine->message(t("Piano non trovato: %1. %2").arg(raw.trimmed(), storyList(doc)));
            break;
        case Ask::Rename: {
            Story s = current();
            s.name = name;
            if (!doc.updateStory(s.id, s))
                engine->message(t("Nome non valido o già usato."));
            break;
        }
        default:
            return;
        }
        toOption();
    }

    void enter() override
    {
        if (ask_ == Ask::NewName) {
            addStory(defaultName().toStdString());
            toOption();
        } else if (ask_ != Ask::None) {
            toOption();
        } else {
            engine->finish();
        }
    }

private:
    enum class Ask { None, NewName, Current, Rename, Elevation, Height };

    Story current() const { return *engine->document().story(engine->document().currentStory()); }

    // By name, ignoring case.
    const Story* findStory(const QString& name) const
    {
        for (const Story& s : engine->document().stories())
            if (QString::fromStdString(s.name).compare(name, Qt::CaseInsensitive) == 0)
                return &s;
        return nullptr;
    }

    QString defaultName() const
    {
        const Document& doc = engine->document();
        for (int k = static_cast<int>(doc.stories().size());; ++k) {
            const QString name = t("Piano %1").arg(k);
            if (!doc.storyNamed(name.toStdString()))
                return name;
        }
    }

    // New stories go on top of the highest one.
    void addStory(const std::string& name)
    {
        Document& doc = engine->document();
        const Story& top = doc.stories().back();
        const StoryId id = doc.addStory({0, name, top.elevation + top.height, top.height});
        if (!id) {
            engine->message(t("Nome non valido o già usato."));
            return;
        }
        setCurrent(id);
    }

    void setCurrent(StoryId id)
    {
        engine->document().setCurrentStory(id);
        engine->clearSelection();
        const Story s = current();
        engine->message(t("Piano corrente: %1 (quota %2)").arg(QString::fromStdString(s.name)).arg(s.elevation));
    }

    void toOption()
    {
        ask_ = Ask::None;
        engine->setPrompt(t("Specificare opzione [Nuovo/Corrente/Su/Giù/Rinomina/Quota/Altezza/Elimina/?]:"));
    }

    Ask ask_ = Ask::None;
};

// What closes rooms on the current story for SLAB and ROOF: the whole
// footprints of the walls (doors do not open them) and the other curves.
std::vector<Shape> roomBoundaries(CommandEngine* engine)
{
    const Document& doc = engine->document();
    const auto walls = joinedWalls(doc);
    std::vector<Shape> out;
    for (const auto& [id, e] : doc.entities()) {
        if (!doc.isVisible(e))
            continue;
        if (std::holds_alternative<Wall>(e.shape))
            out.push_back(wallOutline(walls.at(id)));
        else if (std::holds_alternative<Line>(e.shape) || std::holds_alternative<Arc>(e.shape)
                 || std::holds_alternative<Circle>(e.shape) || std::holds_alternative<Polyline>(e.shape))
            out.push_back(e.shape);
    }
    return out;
}

// The outer outline of the walls of the current story (one per separate building).
std::vector<Polyline> wallsOutline(CommandEngine* engine)
{
    std::vector<Shape> walls;
    const Document& doc = engine->document();
    const auto joined = joinedWalls(doc);
    BBox box;
    for (const auto& [id, e] : doc.entities()) {
        if (std::holds_alternative<Wall>(e.shape) && doc.isVisible(e)) {
            walls.push_back(wallOutline(joined.at(id)));
            box.add(boundingBox(walls.back()));
        }
    }
    if (walls.empty())
        return {};
    // A frame around the walls: from inside it, the walls are its islands.
    const double m = std::max(box.width(), box.height()) * 0.1 + 1.0;
    const BBox frame = box.inflated(m);
    walls.push_back(Polyline{{frame.min, {frame.max.x, frame.min.y}, frame.max, {frame.min.x, frame.max.y}}, true});
    auto loops = findHatchBoundary(walls, frame.min + Vec2{m / 2.0, m / 2.0});
    if (!loops.empty())
        loops.erase(loops.begin());
    return loops;
}

// A closed outline picked by the user: closed polyline or circle.
std::optional<Polyline> pickClosedOutline(CommandEngine* engine, Vec2 p)
{
    const auto id = pickObject(engine, p);
    if (!id)
        return std::nullopt;
    const Shape& s = engine->document().entity(*id)->shape;
    if (const auto* pl = std::get_if<Polyline>(&s); pl && pl->closed && pl->points.size() >= 3)
        return *pl;
    if (const auto* c = std::get_if<Circle>(&s))
        return Polyline{{c->center + Vec2{c->radius, 0}, c->center - Vec2{c->radius, 0}}, true, {1.0, 1.0}};
    engine->message(t("L'oggetto selezionato non è una polilinea chiusa o un cerchio."));
    return std::nullopt;
}

// Shared by SLAB and ROOF: the outline is picked inside a room, taken from
// the walls, chosen among closed objects or drawn point by point.
class OutlineInput {
public:
    enum class Mode { Default, Objects, Drawing };

    Mode mode() const { return mode_; }
    const std::vector<Vec2>& points() const { return points_; }

    bool option(const QString& w)
    {
        if (isAny(w, {"O", "OGGETTI", "OBJECTS"})) {
            mode_ = Mode::Objects;
            return true;
        }
        if (isAny(w, {"D", "DISEGNA", "DRAW"})) {
            mode_ = Mode::Drawing;
            points_.clear();
            return true;
        }
        return false;
    }

    // A point of the drawn outline. True when accepted.
    bool addPoint(Vec2 p)
    {
        if (!points_.empty() && almostEqual(p, points_.back()))
            return false;
        points_.push_back(p);
        return true;
    }

    void undoPoint()
    {
        if (!points_.empty())
            points_.pop_back();
    }

    std::optional<Polyline> drawn() const
    {
        if (points_.size() < 3)
            return std::nullopt;
        return Polyline{points_, true};
    }

    void reset()
    {
        mode_ = Mode::Default;
        points_.clear();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const
    {
        if (mode_ != Mode::Drawing || points_.empty())
            return;
        Polyline pl{points_, points_.size() >= 2};
        pl.points.push_back(cursor);
        out.push_back(pl);
    }

    std::optional<Vec2> base() const
    {
        if (mode_ != Mode::Drawing || points_.empty())
            return std::nullopt;
        return points_.back();
    }

private:
    Mode mode_ = Mode::Default;
    std::vector<Vec2> points_;
};

double& gSlabThickness = toolSettings().slabThickness;
double& gSlabOffset = toolSettings().slabOffset;

// SLAB: a floor slab inside a room, under all the walls, along a closed
// object or drawn.
class SlabCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Solaio corrente: spessore %1, quota superiore %2 rispetto al piano").arg(gSlabThickness).arg(gSlabOffset));
        toDefault();
    }

    bool point(Vec2 p) override
    {
        if (ask_ == Ask::HoleSlab) {
            slab_ = pickSlab(p);
            if (!slab_) {
                engine->message(t("Nessun solaio trovato."));
                return false;
            }
            ask_ = Ask::HoleOutline;
            engine->setPrompt(t("Selezionare polilinea chiusa o cerchio per il foro:"));
            return true;
        }
        if (ask_ == Ask::HoleOutline) {
            const auto outline = pickClosedOutline(engine, p);
            if (!outline)
                return false;
            Slab sl = std::get<Slab>(engine->document().entity(*slab_)->shape);
            sl.loops.push_back(*outline);
            engine->document().replace(*slab_, sl);
            toDefault();
            return true;
        }
        if (ask_ != Ask::None)
            return false;
        switch (input_.mode()) {
        case OutlineInput::Mode::Drawing:
            if (!input_.addPoint(p))
                return false;
            updateDrawPrompt();
            return true;
        case OutlineInput::Mode::Objects:
            if (const auto outline = pickClosedOutline(engine, p)) {
                add({*outline});
                return true;
            }
            return false;
        case OutlineInput::Mode::Default:
            break;
        }
        auto loops = findHatchBoundary(roomBoundaries(engine), p);
        if (loops.empty()) {
            engine->message(t("Nessun contorno chiuso trovato attorno al punto."));
            return false;
        }
        add(std::move(loops));
        return true;
    }

    bool number(double value) override
    {
        if (ask_ == Ask::Thickness && value > 0.0)
            gSlabThickness = value;
        else if (ask_ == Ask::Offset)
            gSlabOffset = value;
        else
            return false;
        toDefault();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ != Ask::None)
            return false;
        if (input_.mode() == OutlineInput::Mode::Drawing) {
            if (isAny(w, {"AN", "ANNULLA", "U", "UNDO"})) {
                input_.undoPoint();
                updateDrawPrompt();
                return true;
            }
            if (isCloseWord(w)) {
                closeDrawn();
                return true;
            }
            return false;
        }
        if (isAny(w, {"M", "MURI", "WALLS"})) {
            const auto loops = wallsOutline(engine);
            if (loops.empty())
                engine->message(t("Nessun muro su questo piano."));
            for (const Polyline& loop : loops)
                add({loop});
            return true;
        }
        if (isAny(w, {"S", "SPESSORE", "THICKNESS"})) {
            ask_ = Ask::Thickness;
            engine->setPrompt(t("Specificare spessore del solaio <%1>:").arg(gSlabThickness));
            return true;
        }
        if (isAny(w, {"F", "FORO", "HOLE"})) {
            ask_ = Ask::HoleSlab;
            engine->setPrompt(t("Selezionare solaio da forare:"));
            return true;
        }
        if (isAny(w, {"Q", "QUOTA", "ELEVATION"})) {
            ask_ = Ask::Offset;
            engine->setPrompt(t("Specificare quota superiore del solaio rispetto al piano <%1>:").arg(gSlabOffset));
            return true;
        }
        if (input_.option(w)) {
            if (input_.mode() == OutlineInput::Mode::Objects)
                engine->setPrompt(t("Selezionare polilinea chiusa o cerchio:"));
            else
                updateDrawPrompt();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (ask_ != Ask::None || input_.mode() == OutlineInput::Mode::Objects) {
            toDefault();
            return;
        }
        if (input_.mode() == OutlineInput::Mode::Drawing) {
            closeDrawn();
            return;
        }
        engine->finish();
    }

    bool wantsObjectPick() const override
    {
        return ask_ == Ask::HoleSlab || ask_ == Ask::HoleOutline
            || (ask_ == Ask::None && input_.mode() == OutlineInput::Mode::Objects);
    }
    void preview(Vec2 cursor, std::vector<Shape>& out) const override { input_.preview(cursor, out); }
    std::optional<Vec2> base() const override { return input_.base(); }

private:
    enum class Ask { None, Thickness, Offset, HoleSlab, HoleOutline };

    // Only slabs: under the walls their edges are where the walls are.
    std::optional<EntityId> pickSlab(Vec2 p) const
    {
        const Document& doc = engine->document();
        std::optional<EntityId> best;
        double bestDist = engine->pickTolerance();
        for (const auto& [id, e] : doc.entities()) {
            if (!std::holds_alternative<Slab>(e.shape) || !doc.isEditable(e))
                continue;
            const double d = distanceTo(e.shape, p);
            if (d <= bestDist) {
                bestDist = d;
                best = id;
            }
        }
        return best;
    }

    void add(std::vector<Polyline> loops)
    {
        Slab sl;
        sl.loops = std::move(loops);
        sl.thickness = gSlabThickness;
        sl.offset = gSlabOffset;
        engine->document().add(sl);
        toDefault();
    }

    void closeDrawn()
    {
        if (const auto outline = input_.drawn())
            add({*outline});
        else
            toDefault();
    }

    void updateDrawPrompt()
    {
        if (input_.points().empty())
            engine->setPrompt(t("Specificare primo punto del contorno:"));
        else if (input_.points().size() >= 3)
            engine->setPrompt(t("Specificare punto successivo o [Chiudi/ANnulla]:"));
        else
            engine->setPrompt(t("Specificare punto successivo o [ANnulla]:"));
    }

    void toDefault()
    {
        ask_ = Ask::None;
        input_.reset();
        engine->setPrompt(t("Specificare punto interno o [Muri/Oggetti/Disegna/Foro/Spessore/Quota]:"));
    }

    OutlineInput input_;
    Ask ask_ = Ask::None;
    std::optional<EntityId> slab_;
};

RoofStyle& gRoofType = toolSettings().roofStyle;
double& gRoofPitch = toolSettings().roofPitch;  // degrees
double& gRoofOverhang = toolSettings().roofOverhang;
double& gRoofThickness = toolSettings().roofThickness;
std::optional<double>& gRoofBase = toolSettings().roofBase;  // unset: the top of the walls

QString roofTypeName(RoofStyle type)
{
    switch (type) {
    case RoofStyle::Gable:
        return t("Capanna");
    case RoofStyle::Shed:
        return t("Falda");
    case RoofStyle::Flat:
        return t("Piano");
    case RoofStyle::Hip:
        break;
    }
    return t("Padiglione");
}

// ROOF: hip, gable, shed or flat roof over the walls, a closed polyline or a
// drawn outline.
class RoofCommand : public Command {
public:
    void start() override
    {
        engine->message(t("Tetto corrente: %1, pendenza %2°, sporto %3, spessore %4, imposta %5")
                            .arg(roofTypeName(gRoofType))
                            .arg(gRoofPitch)
                            .arg(gRoofOverhang)
                            .arg(gRoofThickness)
                            .arg(gRoofBase ? QString::number(*gRoofBase) : t("in cima ai muri")));
        toDefault();
    }

    bool point(Vec2 p) override
    {
        if (ask_ != Ask::None)
            return false;
        if (input_.mode() == OutlineInput::Mode::Objects) {
            if (const auto outline = pickClosedOutline(engine, p)) {
                add(*outline);
                return true;
            }
            return false;
        }
        // Without an option, the points draw the outline.
        if (input_.mode() == OutlineInput::Mode::Default)
            input_.option(QStringLiteral("D"));
        if (!input_.addPoint(p))
            return false;
        updateDrawPrompt();
        return true;
    }

    bool number(double value) override
    {
        switch (ask_) {
        case Ask::Pitch:
            if (value < 0.0 || value >= 80.0)
                return false;
            gRoofPitch = value;
            break;
        case Ask::Overhang:
            if (value < 0.0)
                return false;
            gRoofOverhang = value;
            break;
        case Ask::Thickness:
            if (value <= 0.0)
                return false;
            gRoofThickness = value;
            break;
        case Ask::Base:
            gRoofBase = value;
            break;
        default:
            return false;
        }
        toDefault();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ == Ask::Type) {
            if (isAny(w, {"P", "PADIGLIONE", "HIP"}))
                gRoofType = RoofStyle::Hip;
            else if (isAny(w, {"C", "CAPANNA", "GABLE"}))
                gRoofType = RoofStyle::Gable;
            else if (isAny(w, {"F", "FALDA", "SHED"}))
                gRoofType = RoofStyle::Shed;
            else if (isAny(w, {"PI", "PIANO", "FLAT"}))
                gRoofType = RoofStyle::Flat;
            else
                return false;
            toDefault();
            return true;
        }
        if (ask_ != Ask::None)
            return false;
        if (input_.mode() == OutlineInput::Mode::Drawing) {
            if (isAny(w, {"AN", "ANNULLA", "U", "UNDO"})) {
                input_.undoPoint();
                updateDrawPrompt();
                return true;
            }
            if (isCloseWord(w)) {
                closeDrawn();
                return true;
            }
            return false;
        }
        if (isAny(w, {"M", "MURI", "WALLS"})) {
            const auto loops = wallsOutline(engine);
            if (loops.empty())
                engine->message(t("Nessun muro su questo piano."));
            for (const Polyline& loop : loops)
                add(loop);
            return true;
        }
        if (isAny(w, {"T", "TIPO", "TYPE"})) {
            ask_ = Ask::Type;
            engine->setPrompt(t("Specificare tipo di tetto [Padiglione/Capanna/Falda/PIano] <%1>:").arg(roofTypeName(gRoofType)));
        } else if (isAny(w, {"P", "PENDENZA", "PITCH"})) {
            ask_ = Ask::Pitch;
            engine->setPrompt(t("Specificare pendenza in gradi <%1>:").arg(gRoofPitch));
        } else if (isAny(w, {"S", "SPORTO", "OVERHANG"})) {
            ask_ = Ask::Overhang;
            engine->setPrompt(t("Specificare sporto <%1>:").arg(gRoofOverhang));
        } else if (isAny(w, {"SP", "SPESSORE", "THICKNESS"})) {
            ask_ = Ask::Thickness;
            engine->setPrompt(t("Specificare spessore del tetto <%1>:").arg(gRoofThickness));
        } else if (isAny(w, {"I", "IMPOSTA", "BASE"})) {
            ask_ = Ask::Base;
            engine->setPrompt(t("Specificare altezza d'imposta sopra il piano <%1>:").arg(roofBase()));
        } else if (input_.option(w)) {
            if (input_.mode() == OutlineInput::Mode::Objects)
                engine->setPrompt(t("Selezionare polilinea chiusa:"));
            else
                updateDrawPrompt();
        } else {
            return false;
        }
        return true;
    }

    void enter() override
    {
        if (ask_ != Ask::None || input_.mode() == OutlineInput::Mode::Objects) {
            toDefault();
            return;
        }
        if (input_.mode() == OutlineInput::Mode::Drawing) {
            closeDrawn();
            return;
        }
        engine->finish();
    }

    bool wantsObjectPick() const override { return ask_ == Ask::None && input_.mode() == OutlineInput::Mode::Objects; }
    void preview(Vec2 cursor, std::vector<Shape>& out) const override { input_.preview(cursor, out); }
    std::optional<Vec2> base() const override { return input_.base(); }

private:
    enum class Ask { None, Type, Pitch, Overhang, Thickness, Base };

    // The slopes start at the top of the highest wall of the story, unless set.
    double roofBase() const
    {
        if (gRoofBase)
            return *gRoofBase;
        double top = 0.0;
        const Document& doc = engine->document();
        for (const auto& [id, e] : doc.entities())
            if (const auto* w = std::get_if<Wall>(&e.shape); w && doc.isVisible(e))
                top = std::max(top, w->height);
        return top > 0.0 ? top : 2700.0;
    }

    void add(const Polyline& outline)
    {
        Roof r;
        r.footprint = outline;
        r.footprint.closed = true;
        r.footprint.bulges.clear();
        r.pitch = degToRad(gRoofPitch);
        r.overhang = gRoofOverhang;
        r.thickness = gRoofThickness;
        r.base = roofBase();
        if (outline.hasArcs() || !roofGeometry(r)) {
            engine->message(t("Il contorno del tetto deve essere convesso (rettangolo, trapezio...) "
                              "oppure fatto solo di angoli retti (pianta a L, a T, a U...)."));
            toDefault();
            return;
        }
        r.gables = roofGables(roofGeometry(r)->footprint, gRoofType);
        engine->document().add(r);
        toDefault();
    }

    void closeDrawn()
    {
        if (const auto outline = input_.drawn())
            add(*outline);
        else
            toDefault();
    }

    void updateDrawPrompt()
    {
        if (input_.points().empty())
            engine->setPrompt(t("Specificare primo punto del contorno:"));
        else if (input_.points().size() >= 3)
            engine->setPrompt(t("Specificare punto successivo o [Chiudi/ANnulla]:"));
        else
            engine->setPrompt(t("Specificare punto successivo o [ANnulla]:"));
    }

    void toDefault()
    {
        ask_ = Ask::None;
        input_.reset();
        engine->setPrompt(t("Specificare primo punto del contorno o [Muri/Oggetti/Tipo/Pendenza/Sporto/SPessore/Imposta]:"));
    }

    OutlineInput input_;
    Ask ask_ = Ask::None;
};

Stair::Kind& gStairKind = toolSettings().stairKind;
bool& gStairTurnRight = toolSettings().stairTurnRight;
double& gStairWidth = toolSettings().stairWidth;
double& gStairGoing = toolSettings().stairGoing;
std::optional<double>& gStairHeight = toolSettings().stairHeight;  // unset: the height of the story

QString stairKindName(Stair::Kind kind)
{
    switch (kind) {
    case Stair::Kind::L:
        return t("L");
    case Stair::Kind::U:
        return t("U");
    case Stair::Kind::Straight:
        break;
    }
    return t("Dritta");
}

// STAIR: a straight, L or U stair up to the next story.
class StairCommand : public Command {
public:
    void start() override
    {
        const int n = risers();
        engine->message(t("Scala corrente: %1, larghezza %2, pedata %3, %4 alzate da %5 per salire %6")
                            .arg(stairKindName(gStairKind))
                            .arg(gStairWidth)
                            .arg(gStairGoing)
                            .arg(n)
                            .arg(QString::fromStdString(formatMeasurement(height() / n)))
                            .arg(height()));
        toStart();
    }

    bool point(Vec2 p) override
    {
        if (ask_ != Ask::None)
            return false;
        if (!start_) {
            start_ = p;
            engine->setPrompt(t("Specificare direzione della prima rampa:"));
            return true;
        }
        if (almostEqual(p, *start_))
            return false;
        engine->document().add(stairTo(p));
        engine->finish();
        return true;
    }

    bool number(double value) override
    {
        switch (ask_) {
        case Ask::Width:
            if (value <= 0.0)
                return false;
            gStairWidth = value;
            break;
        case Ask::Height:
            if (value <= 0.0)
                return false;
            gStairHeight = value;
            break;
        case Ask::Going:
            if (value <= 0.0)
                return false;
            gStairGoing = value;
            break;
        default:
            return false;
        }
        toStart();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ == Ask::Kind) {
            if (isAny(w, {"D", "DRITTA", "STRAIGHT"}))
                gStairKind = Stair::Kind::Straight;
            else if (isAny(w, {"L"}))
                gStairKind = Stair::Kind::L;
            else if (isAny(w, {"U"}))
                gStairKind = Stair::Kind::U;
            else
                return false;
            toStart();
            return true;
        }
        if (ask_ == Ask::Turn) {
            if (isAny(w, {"S", "SINISTRA", "LEFT"}))
                gStairTurnRight = false;
            else if (isAny(w, {"D", "DESTRA", "RIGHT"}))
                gStairTurnRight = true;
            else
                return false;
            toStart();
            return true;
        }
        if (ask_ != Ask::None || start_)
            return false;
        if (isAny(w, {"T", "TIPO", "TYPE"})) {
            ask_ = Ask::Kind;
            engine->setPrompt(t("Specificare tipo di scala [Dritta/L/U] <%1>:").arg(stairKindName(gStairKind)));
        } else if (isAny(w, {"V", "VERSO", "TURN"})) {
            ask_ = Ask::Turn;
            engine->setPrompt(t("Specificare verso della svolta [Sinistra/Destra] <%1>:")
                                  .arg(gStairTurnRight ? t("Destra") : t("Sinistra")));
        } else if (isAny(w, {"L", "LARGHEZZA", "WIDTH"})) {
            ask_ = Ask::Width;
            engine->setPrompt(t("Specificare larghezza della scala <%1>:").arg(gStairWidth));
        } else if (isAny(w, {"A", "ALTEZZA", "HEIGHT"})) {
            ask_ = Ask::Height;
            engine->setPrompt(t("Specificare altezza da salire <%1>:").arg(height()));
        } else if (isAny(w, {"P", "PEDATA", "GOING"})) {
            ask_ = Ask::Going;
            engine->setPrompt(t("Specificare pedata <%1>:").arg(gStairGoing));
        } else {
            return false;
        }
        return true;
    }

    void enter() override
    {
        if (ask_ != Ask::None)
            toStart();
        else
            engine->finish();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (start_ && !almostEqual(cursor, *start_))
            out.push_back(stairTo(cursor));
    }

    std::optional<Vec2> base() const override { return start_; }

private:
    enum class Ask { None, Kind, Turn, Width, Height, Going };

    // Up to the next story, unless set.
    double height() const
    {
        if (gStairHeight)
            return *gStairHeight;
        const Document& doc = engine->document();
        const Story* s = doc.story(doc.currentStory());
        return s ? s->height : 3000.0;
    }

    int risers() const { return stairRisers(height()); }

    Stair stairTo(Vec2 direction) const
    {
        Stair s;
        s.start = *start_;
        s.angle = angleOf(direction - *start_);
        s.kind = gStairKind;
        s.turnRight = gStairTurnRight;
        s.width = gStairWidth;
        s.height = height();
        s.risers = risers();
        s.going = gStairGoing;
        return s;
    }

    void toStart()
    {
        ask_ = Ask::None;
        engine->setPrompt(t("Specificare punto iniziale (centro del primo gradino) o [Tipo/Verso/Larghezza/Altezza/Pedata]:"));
    }

    std::optional<Vec2> start_;
    Ask ask_ = Ask::None;
};

class View3dCommand : public Command {
public:
    void start() override
    {
        engine->requestView3d();
        engine->finish();
    }
};

// ---------------------------------------------------------------- view

// BREAK: removes the part of an object between two points. The point that
// picks the object is the first one, unless Primo punto asks for another;
// the same point twice (@ for the second) splits the object there.
class BreakCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Selezionare oggetto:")); }

    bool wantsObjectPick() const override { return ask_ == Ask::Object; }

    bool point(Vec2 p) override
    {
        switch (ask_) {
        case Ask::Object: {
            const auto id = pickObject(engine, p);
            if (!id)
                return false;
            const Shape& s = engine->document().entity(*id)->shape;
            if (!std::holds_alternative<Line>(s) && !std::holds_alternative<Arc>(s) && !std::holds_alternative<Circle>(s)
                && !std::holds_alternative<Polyline>(s)) {
                engine->message(t("Questo oggetto non si può spezzare."));
                return false;
            }
            id_ = *id;
            first_ = nearestPoint(s, p);
            ask_ = Ask::Second;
            engine->setPrompt(t("Specificare secondo punto di interruzione o [Primo punto]:"));
            return true;
        }
        case Ask::First:
            first_ = p;
            ask_ = Ask::Second;
            engine->setPrompt(t("Specificare secondo punto di interruzione:"));
            return true;
        case Ask::Second:
            return breakAt(p);
        }
        return false;
    }

    bool keyword(const QString& w) override
    {
        if (ask_ == Ask::Second && isAny(w, {"P", "PRIMO", "F", "FIRST"})) {
            ask_ = Ask::First;
            engine->setPrompt(t("Specificare primo punto di interruzione:"));
            return true;
        }
        return false;
    }

    std::optional<Vec2> base() const override
    {
        return ask_ == Ask::Second ? std::optional<Vec2>(first_) : std::nullopt;
    }

private:
    enum class Ask { Object, First, Second };

    bool breakAt(Vec2 second)
    {
        Document& doc = engine->document();
        const Entity* e = doc.entity(id_);
        if (!e)
            return false;
        const auto pieces = breakShape(e->shape, first_, second);
        if (!pieces) {
            engine->message(t("Impossibile spezzare l'oggetto in questi punti."));
            return false;
        }
        const Entity original = *e;
        doc.remove(id_);
        for (const Shape& piece : *pieces)
            addLike(doc, original, piece);
        engine->finish();
        return true;
    }

    Ask ask_ = Ask::Object;
    EntityId id_ = 0;
    Vec2 first_;
};

// ZOOM: extents, a window picked by two corners, the previous view, or a
// factor ("2" doubles the magnification).
class ZoomCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Specificare angolo della finestra, fattore di scala o [Estensioni/Finestra/Precedente] <Estensioni>:"));
    }

    bool keyword(const QString& w) override
    {
        if (corner_)
            return false;
        if (isAny(w, {"E", "ESTENSIONI", "EXTENTS", "T", "TUTTO", "ALL"})) {
            enter();
            return true;
        }
        if (isAny(w, {"F", "FINESTRA", "W", "WINDOW"})) {
            engine->setPrompt(t("Specificare primo angolo:"));
            return true;
        }
        if (isAny(w, {"P", "PRECEDENTE", "PREVIOUS"})) {
            engine->requestZoomPrevious();
            engine->finish();
            return true;
        }
        // 2X, as in AutoCAD, is the same as 2.
        if (w.endsWith(QLatin1Char('X')) && w.size() > 1) {
            bool ok = false;
            const double f = w.chopped(1).toDouble(&ok);
            return ok && number(f);
        }
        return false;
    }

    bool number(double value) override
    {
        if (corner_ || value <= 0.0)
            return false;
        engine->requestZoomFactor(value);
        engine->finish();
        return true;
    }

    bool point(Vec2 p) override
    {
        if (!corner_) {
            corner_ = p;
            engine->setPrompt(t("Specificare angolo opposto:"));
            return true;
        }
        const BBox box = BBox::fromCorners(*corner_, p);
        if (box.width() <= kEps && box.height() <= kEps)
            return false;
        engine->requestZoomWindow(box);
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (corner_)
            out.push_back(Polyline{{*corner_, {cursor.x, corner_->y}, cursor, {corner_->x, cursor.y}}, true});
    }

    void enter() override
    {
        engine->requestZoomExtents();
        engine->finish();
    }

private:
    std::optional<Vec2> corner_;
};

class PlotCommand : public Command {
public:
    void start() override
    {
        engine->finish();
        engine->requestPlot();
    }
};

class LayerCommand : public Command {
public:
    void start() override
    {
        engine->requestLayerManager();
        engine->finish();
    }
};

// UNITS (command line version): the drawing unit, written to DXF as
// $INSUNITS, and the decimals shown for lengths and coordinates.
class UnitsCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Specificare unità del disegno [Millimetri/Centimetri/METri/Pollici/PIEdi/Senza unità] <%1>:")
                              .arg(QString::fromUtf8(unitName(engine->document().units()))));
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Unit)
            return false;
        int units = -1;
        if (isAny(w, {"M", "MI", "MILLIMETRI", "MM"}))
            units = 4;
        else if (isAny(w, {"C", "CENTIMETRI", "CM"}))
            units = 5;
        else if (isAny(w, {"MET", "METRI"}))
            units = 6;
        else if (isAny(w, {"P", "POLLICI", "INCHES"}))
            units = 1;
        else if (isAny(w, {"PIE", "PIEDI", "FEET"}))
            units = 2;
        else if (isAny(w, {"S", "SENZA", "NESSUNA"}))
            units = 0;
        if (units < 0)
            return false;
        engine->document().setUnits(units);
        toPrecision();
        return true;
    }

    bool number(double v) override
    {
        if (step_ != Step::Precision || v < 0.0 || v > 8.0 || v != std::floor(v))
            return false;
        engine->document().setPrecision(int(v));
        done();
        return true;
    }

    void enter() override
    {
        if (step_ == Step::Unit)
            toPrecision();
        else
            done();
    }

private:
    enum class Step { Unit, Precision };

    void toPrecision()
    {
        step_ = Step::Precision;
        engine->setPrompt(t("Specificare numero di decimali (0-8) <%1>:").arg(engine->document().precision()));
    }

    void done()
    {
        const Document& doc = engine->document();
        engine->message(t("Unità: %1, %2 decimali.").arg(QString::fromUtf8(unitName(doc.units()))).arg(doc.precision()));
        gPrecision = doc.precision();
        engine->finish();
    }

    Step step_ = Step::Unit;
};

// -LAYER: layers from the command line, as in scripts written for AutoCAD.
class LayerLineCommand : public Command {
public:
    void start() override { toOption(); }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Option)
            return false;
        if (w == QLatin1String("?")) {
            list();
            toOption();
            return true;
        }
        if (isAny(w, {"C", "CREA", "M", "MAKE"}))
            ask(Op::Make, t("Immettere nome del layer da creare e rendere corrente:"));
        else if (isAny(w, {"N", "NUOVO", "NEW"}))
            ask(Op::New, t("Immettere nomi dei nuovi layer (separati da virgola):"));
        else if (isAny(w, {"I", "IMPOSTA", "S", "SET"}))
            ask(Op::Set, t("Immettere nome del layer da rendere corrente:"));
        else if (isAny(w, {"ON"}))
            ask(Op::On, t("Immettere nomi dei layer da attivare (* = tutti):"));
        else if (isAny(w, {"OFF"}))
            ask(Op::Off, t("Immettere nomi dei layer da disattivare (* = tutti):"));
        else if (isAny(w, {"CO", "COLORE", "COLOR"}))
            ask(Op::Color, t("Specificare colore (1-255, nome o R,G,B):"));
        else if (isAny(w, {"B", "BLOCCA", "LO", "LOCK"}))
            ask(Op::Lock, t("Immettere nomi dei layer da bloccare (* = tutti):"));
        else if (isAny(w, {"SB", "SBLOCCA", "U", "UNLOCK"}))
            ask(Op::Unlock, t("Immettere nomi dei layer da sbloccare (* = tutti):"));
        else
            return false;
        return true;
    }

    bool wantsText() const override { return step_ == Step::Names; }

    void text(const QString& s) override
    {
        if (op_ == Op::Color && !color_) {
            const auto c = parseColor(s);
            if (!c || !*c) {
                engine->message(t("Colore non valido: usare un numero da 1 a 255, un nome (rosso, giallo, verde, ciano, blu, magenta, bianco) o R,G,B."));
                return;
            }
            color_ = **c;
            engine->setPrompt(t("Immettere nomi dei layer per il colore %1 (* = tutti):").arg(colorName(**c)));
            return;
        }
        apply(s);
        engine->documentChanged();
        toOption();
    }

    void enter() override
    {
        if (step_ == Step::Names)
            toOption();
        else
            engine->finish();
    }

private:
    enum class Step { Option, Names };
    enum class Op { Make, New, Set, On, Off, Color, Lock, Unlock };

    void toOption()
    {
        step_ = Step::Option;
        color_.reset();
        engine->setPrompt(t("Layer corrente: \"%1\"\nImmettere un'opzione [?/Crea/Nuovo/Imposta/ON/OFF/COlore/Blocca/SBlocca]:")
                              .arg(QString::fromStdString(engine->document().currentLayer())));
    }

    void ask(Op op, const QString& prompt)
    {
        op_ = op;
        step_ = Step::Names;
        engine->setPrompt(prompt);
    }

    void list()
    {
        QStringList lines;
        for (const Layer& l : engine->document().layers()) {
            lines << t("%1: %2, %3, colore %4").arg(QString::fromStdString(l.name), l.visible ? t("attivo") : t("disattivato"),
                                                   l.locked ? t("bloccato") : t("sbloccato"), colorName(l.color));
        }
        engine->message(lines.join(QLatin1Char('\n')));
    }

    // The layers named in `s` (comma separated, "*" for all) that exist.
    std::vector<std::string> named(const QString& s) const
    {
        std::vector<std::string> out;
        for (QString name : s.split(QLatin1Char(','))) {
            name = name.trimmed();
            if (name == QLatin1String("*")) {
                for (const Layer& l : engine->document().layers())
                    out.push_back(l.name);
            } else if (const Layer* l = layerNamed(name)) {
                out.push_back(l->name);
            } else if (!name.isEmpty()) {
                engine->message(t("Layer \"%1\" non trovato.").arg(name));
            }
        }
        return out;
    }

    // Layer names are matched without regard to case, as in AutoCAD.
    const Layer* layerNamed(const QString& name) const
    {
        for (const Layer& l : engine->document().layers())
            if (QString::fromStdString(l.name).compare(name, Qt::CaseInsensitive) == 0)
                return &l;
        return nullptr;
    }

    void apply(const QString& s)
    {
        Document& doc = engine->document();
        switch (op_) {
        case Op::Make:
        case Op::New:
            for (QString name : op_ == Op::Make ? QStringList{s} : s.split(QLatin1Char(','))) {
                name = name.trimmed();
                if (name.isEmpty())
                    continue;
                if (!layerNamed(name) && !doc.addLayer(Layer::named(name.toStdString()))) {
                    engine->message(t("Nome di layer non valido: \"%1\".").arg(name));
                    continue;
                }
                if (op_ == Op::Make)
                    doc.setCurrentLayer(layerNamed(name)->name);
            }
            return;
        case Op::Set:
            if (const Layer* l = layerNamed(s.trimmed())) {
                if (!l->visible)
                    engine->message(t("Attenzione: il layer \"%1\" è disattivato.").arg(QString::fromStdString(l->name)));
                doc.setCurrentLayer(l->name);
            } else {
                engine->message(t("Layer \"%1\" non trovato.").arg(s.trimmed()));
            }
            return;
        default:
            break;
        }
        for (const std::string& name : named(s)) {
            Layer l = *doc.layer(name);
            switch (op_) {
            case Op::On: l.visible = true; break;
            case Op::Off:
                if (name == doc.currentLayer())
                    engine->message(t("Il layer corrente \"%1\" viene disattivato.").arg(QString::fromStdString(name)));
                l.visible = false;
                break;
            case Op::Color: l.color = *color_; break;
            case Op::Lock: l.locked = true; break;
            case Op::Unlock: l.locked = false; break;
            default: break;
            }
            doc.updateLayer(name, l);
        }
    }

    Step step_ = Step::Option;
    Op op_ = Op::Make;
    std::optional<Color> color_;
};

// LAYOUT: the sheets for printing. A new sheet shows the current story,
// fitted or at the scale given, with a title block.
class LayoutCommand : public Command {
public:
    void start() override
    {
        engine->message(layoutList());
        toOption();
    }

    bool keyword(const QString& w) override
    {
        if (step_ == Step::Option) {
            if (isAny(w, {"N", "NUOVA", "NEW"})) {
                toName();
            } else if (isAny(w, {"E", "ELIMINA", "DELETE"})) {
                step_ = Step::Remove;
                engine->setPrompt(t("Specificare nome della tavola da eliminare:"));
            } else if (w == QLatin1String("?")) {
                engine->message(layoutList());
            } else {
                return false;
            }
            return true;
        }
        if (step_ == Step::Paper) {
            if (!paperSize(w.toStdString()))
                return false;
            paper_ = w.toStdString();
            toScale();
            return true;
        }
        if (step_ == Step::Scale) {
            if (isAny(w, {"A", "ADATTA", "F", "FIT"})) {
                create(0.0);
                return true;
            }
            // 1:50
            const qsizetype colon = w.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                const double a = w.left(colon).toDouble();
                const double b = w.mid(colon + 1).toDouble();
                if (a > 0.0 && b > 0.0) {
                    create(b / a);
                    return true;
                }
            }
        }
        return false;
    }

    bool number(double v) override
    {
        if (step_ != Step::Scale || v <= 0.0)
            return false;
        create(v);
        return true;
    }

    bool wantsText() const override { return step_ == Step::Name || step_ == Step::Remove; }

    void text(const QString& raw) override
    {
        const QString name = raw.trimmed();
        Document& doc = engine->document();
        if (step_ == Step::Remove) {
            if (doc.removeLayout(name.toStdString()))
                engine->message(t("Tavola %1 eliminata.").arg(name));
            else
                engine->message(t("La tavola %1 non esiste.").arg(name));
            engine->finish();
            return;
        }
        if (doc.layout(name.toStdString())) {
            engine->message(t("Esiste già una tavola %1.").arg(name));
            return;
        }
        name_ = name;
        toPaper();
    }

    void enter() override
    {
        switch (step_) {
        case Step::Option: toName(); break;
        case Step::Name: name_ = defaultName(); toPaper(); break;
        case Step::Paper: toScale(); break;
        case Step::Scale: create(0.0); break;
        case Step::Remove: engine->finish(); break;
        }
    }

private:
    enum class Step { Option, Name, Paper, Scale, Remove };

    QString layoutList() const
    {
        QStringList names;
        for (const Layout& l : engine->document().layouts())
            names << QStringLiteral("%1 (%2)").arg(QString::fromStdString(l.name), QString::fromStdString(l.paper));
        return names.isEmpty() ? t("Il disegno non ha tavole.") : t("Tavole: %1").arg(names.join(QStringLiteral(", ")));
    }

    QString defaultName() const
    {
        for (int i = 1;; ++i) {
            const QString name = t("Tavola %1").arg(i);
            if (!engine->document().layout(name.toStdString()))
                return name;
        }
    }

    void toOption()
    {
        step_ = Step::Option;
        engine->setPrompt(t("Immettere un'opzione [Nuova/Elimina/?] <Nuova>:"));
    }

    void toName()
    {
        step_ = Step::Name;
        engine->setPrompt(t("Specificare nome della tavola <%1>:").arg(defaultName()));
    }

    void toPaper()
    {
        step_ = Step::Paper;
        engine->setPrompt(t("Specificare formato [A4/A3/A2/A1/A0] <%1>:").arg(QString::fromStdString(paper_)));
    }

    void toScale()
    {
        step_ = Step::Scale;
        engine->setPrompt(t("Specificare scala 1:N o [Adatta] <Adatta>:"));
    }

    void create(double scale)
    {
        Document& doc = engine->document();
        const BBox area = doc.extents();
        const bool landscape = !area.valid() || area.width() >= area.height();
        Layout layout = makeLayout(name_.toStdString(), paper_, landscape, area, scale, 1.0, doc.currentStory());
        layout.titleBlock.date = QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")).toStdString();
        layout.titleBlock.number = std::to_string(doc.layouts().size() + 1);
        doc.addLayout(layout);
        engine->message(t("Tavola %1 creata: %2, scala 1:%3.")
                            .arg(name_, QString::fromStdString(layout.paper),
                                 QString::fromStdString(formatMeasurement(layout.viewports.front().scale))));
        engine->requestLayouts();
        engine->finish();
    }

    Step step_ = Step::Option;
    QString name_;
    std::string paper_ = "A3";
};

// ---------------------------------------------------------------- properties

QString linetypeList(const Document& doc)
{
    QStringList names;
    for (const Linetype& lt : doc.linetypes())
        names << QStringLiteral("%1 (%2)").arg(QString::fromStdString(lt.name), QString::fromStdString(lt.description));
    return t("Tipi di linea: %1").arg(names.join(QStringLiteral(", ")));
}

// What COLOR, LINETYPE, LWEIGHT and CHPROP ask: one property of the style,
// typed by the user.
class StyleQuestion {
public:
    enum class Kind { Color, Linetype, Lineweight };

    explicit StyleQuestion(Kind kind) : kind_(kind) {}

    QString prompt(const EntityStyle& current, bool forCurrent) const
    {
        switch (kind_) {
        case Kind::Color:
            return (forCurrent ? t("Specificare colore corrente (1-255, nome o R,G,B) o [DaLayer] <%1>:")
                               : t("Specificare nuovo colore (1-255, nome o R,G,B) o [DaLayer] <%1>:"))
                .arg(colorName(current.color));
        case Kind::Linetype:
            return (forCurrent ? t("Specificare tipo di linea corrente o [?/DaLayer] <%1>:")
                               : t("Specificare nuovo tipo di linea o [?/DaLayer] <%1>:"))
                .arg(linetypeName(current.linetype));
        case Kind::Lineweight:
            return (forCurrent ? t("Specificare spessore di linea corrente in mm o [DaLayer/Predefinito] <%1>:")
                               : t("Specificare nuovo spessore di linea in mm o [DaLayer/Predefinito] <%1>:"))
                .arg(lineweightName(current.lineweight));
        }
        return {};
    }

    // Applies the answer to `style`; false (with a message) when it is not valid.
    bool answer(CommandEngine* engine, const QString& text, EntityStyle& style) const
    {
        switch (kind_) {
        case Kind::Color:
            if (const auto c = parseColor(text)) {
                style.color = *c;
                return true;
            }
            engine->message(t("Colore non valido: usare un numero da 1 a 255, un nome (rosso, giallo, verde, ciano, blu, magenta, bianco) o R,G,B."));
            return false;
        case Kind::Linetype: {
            const QString upper = text.trimmed().toUpper();
            if (upper == QLatin1String("?")) {
                engine->message(linetypeList(engine->document()));
                return false;
            }
            if (isAny(upper, {"DALAYER", "DL", "BYLAYER"})) {
                style.linetype = kByLayer;
                return true;
            }
            if (const Linetype* lt = engine->document().linetype(upper.toStdString())) {
                style.linetype = lt->name;
                return true;
            }
            engine->message(t("Tipo di linea \"%1\" non trovato. %2").arg(text.trimmed(), linetypeList(engine->document())));
            return false;
        }
        case Kind::Lineweight:
            if (const auto w = parseLineweight(text)) {
                style.lineweight = *w;
                return true;
            }
            engine->message(t("Spessore non valido: un valore in mm da 0 a 2.11, DaLayer o Predefinito."));
            return false;
        }
        return false;
    }

private:
    Kind kind_;
};

// COLOR, LINETYPE and LWEIGHT: what new objects get.
class CurrentStyleCommand : public Command {
public:
    explicit CurrentStyleCommand(StyleQuestion::Kind kind) : question_(kind) {}

    void start() override { engine->setPrompt(question_.prompt(engine->document().currentStyle(), true)); }

    bool keyword(const QString& w) override
    {
        EntityStyle style = engine->document().currentStyle();
        if (question_.answer(engine, w, style)) {
            engine->document().setCurrentStyle(style);
            engine->finish();
        }
        return true;
    }

private:
    StyleQuestion question_;
};

class LinetypeScaleCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Immettere nuovo fattore di scala del tipo di linea <%1>:").arg(engine->document().linetypeScale()));
    }

    bool number(double value) override
    {
        if (value <= kEps) {
            engine->message(t("Il valore deve essere positivo."));
            return true;
        }
        engine->document().setLinetypeScale(value);
        engine->finish();
        return true;
    }
};

// CHPROP: changes the colour, layer, linetype or lineweight of the selected objects.
class ChangePropertiesCommand : public SelectionCommand {
public:
    bool keyword(const QString& w) override
    {
        if (selectingNow())
            return false;
        if (question_) {
            EntityStyle style = sample();
            if (question_->answer(engine, w, style))
                apply([&](Entity& e) {
                    switch (asked_) {
                    case StyleQuestion::Kind::Color: e.color = style.color; break;
                    case StyleQuestion::Kind::Linetype: e.linetype = style.linetype; break;
                    case StyleQuestion::Kind::Lineweight: e.lineweight = style.lineweight; break;
                    }
                });
            return true;
        }
        if (askingLayer_) {
            const std::string name = w.trimmed().toStdString();
            const Document& doc = engine->document();
            auto it = std::find_if(doc.layers().begin(), doc.layers().end(),
                                   [&](const Layer& l) { return QString::fromStdString(l.name).toUpper() == w.trimmed(); });
            if (it == doc.layers().end()) {
                engine->message(t("Layer \"%1\" non trovato.").arg(QString::fromStdString(name)));
                return true;
            }
            const std::string layer = it->name;
            apply([&](Entity& e) { e.layer = layer; });
            return true;
        }
        if (isAny(w, {"C", "COLORE", "COLOR"}))
            ask(StyleQuestion::Kind::Color);
        else if (isAny(w, {"TI", "TIPOLINEA", "LT", "LTYPE"}))
            ask(StyleQuestion::Kind::Linetype);
        else if (isAny(w, {"SP", "SPESSORE", "LW", "LWEIGHT"}))
            ask(StyleQuestion::Kind::Lineweight);
        else if (isAny(w, {"LA", "LAYER"})) {
            askingLayer_ = true;
            engine->setPrompt(t("Immettere nome del nuovo layer <%1>:").arg(QString::fromStdString(sampleLayer())));
        } else
            return false;
        return true;
    }

protected:
    void afterSelection() override { askProperty(); }

    void onEnter() override
    {
        if (question_ || askingLayer_)
            askProperty();  // keep the value
        else
            done();
    }

private:
    bool selectingNow() const { return wantsSelection(); }

    void askProperty()
    {
        question_.reset();
        askingLayer_ = false;
        engine->setPrompt(t("Immettere proprietà da modificare [Colore/LAyer/TIpolinea/SPessore]:"));
    }

    void ask(StyleQuestion::Kind kind)
    {
        asked_ = kind;
        question_.emplace(kind);
        engine->setPrompt(question_->prompt(sample(), false));
    }

    // The values shown as default: those of the first object.
    EntityStyle sample() const
    {
        for (EntityId id : ids_)
            if (const Entity* e = engine->document().entity(id))
                return styleOf(*e);
        return {};
    }

    std::string sampleLayer() const
    {
        for (EntityId id : ids_)
            if (const Entity* e = engine->document().entity(id))
                return e->layer;
        return "0";
    }

    template <class F>
    void apply(F change)
    {
        Document& doc = engine->document();
        for (EntityId id : ids_) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            Entity updated = *e;
            change(updated);
            doc.restyle(id, updated.layer, styleOf(updated));
        }
        askProperty();
    }

    std::optional<StyleQuestion> question_;
    StyleQuestion::Kind asked_ = StyleQuestion::Kind::Color;
    bool askingLayer_ = false;
};

// MATCHPROP: gives the layer, colour, linetype and lineweight of one object to others.
class MatchPropertiesCommand : public Command {
public:
    void start() override
    {
        engine->clearSelection();
        engine->setPrompt(t("Selezionare oggetto di origine:"));
    }

    bool point(Vec2 p) override
    {
        if (source_)
            return false;
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        const Entity* e = engine->document().entity(*id);
        source_ = *id;
        layer_ = e->layer;
        style_ = styleOf(*e);
        engine->message(t("Proprietà: layer %1, colore %2, tipo di linea %3, spessore %4")
                            .arg(QString::fromStdString(layer_), colorName(style_.color), linetypeName(style_.linetype),
                                 lineweightName(style_.lineweight)));
        engine->setPrompt(t("Selezionare oggetti di destinazione:"));
        return true;
    }

    void enter() override
    {
        if (source_) {
            int changed = 0;
            for (EntityId id : engine->selection()) {
                if (id == *source_)
                    continue;
                engine->document().restyle(id, layer_, style_);
                ++changed;
            }
            if (changed > 0)
                engine->message(QCoreApplication::translate("Command", "%n oggetti modificati", nullptr, changed));
            engine->clearSelection();
        }
        engine->finish();
    }

    bool wantsObjectPick() const override { return !source_; }
    bool wantsSelection() const override { return source_.has_value(); }

private:
    std::optional<EntityId> source_;
    std::string layer_;
    EntityStyle style_;
};

// ---------------------------------------------------------------- grips

// Grip editing, as AutoCAD's ** STIRA ** mode: every selected object with a
// grip on `base` has it moved to the point picked. The selection stays.
class GripCommand : public Command {
public:
    explicit GripCommand(Vec2 base) : base_(base) {}

    void start() override
    {
        const Document& doc = engine->document();
        for (EntityId id : engine->selection()) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            const std::vector<Vec2> grips = gripPoints(e->shape);
            for (size_t i = 0; i < grips.size(); ++i)
                if (almostEqual(grips[i], base_, 1e-6 * std::max(1.0, length(base_)))) {
                    targets_.push_back({id, i});
                    break;
                }
        }
        if (targets_.empty()) {
            engine->finish();
            return;
        }
        engine->setPrompt(t("** STIRA ** Specificare punto di stiramento o [Esci]:"));
    }

    bool point(Vec2 p) override
    {
        Document& doc = engine->document();
        for (const auto& [id, grip] : targets_)
            if (const Entity* e = doc.entity(id))
                if (const auto moved = moveGrip(e->shape, grip, p))
                    doc.replace(id, *moved);
        engine->finish();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (!isExitWord(w))
            return false;
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        for (const auto& [id, grip] : targets_)
            if (const Entity* e = engine->document().entity(id))
                if (const auto moved = moveGrip(e->shape, grip, cursor))
                    out.push_back(*moved);
        out.push_back(Line{base_, cursor});
    }

    std::optional<Vec2> base() const override { return base_; }

private:
    Vec2 base_;
    std::vector<std::pair<EntityId, size_t>> targets_;
};

// ---------------------------------------------------------------- 2D extras

// ARRAY, as AutoCAD's -ARRAY: rectangular (rows and columns) or polar
// (copies around a centre).
class ArrayCommand : public SelectionCommand {
public:
    bool keyword(const QString& w) override
    {
        if (step_ == Step::Kind) {
            if (isAny(w, {"R", "RETTANGOLARE", "RECTANGULAR"}))
                toRows();
            else if (isAny(w, {"PO", "POLARE", "P", "POLAR"}))
                toCenter();
            else
                return false;
            return true;
        }
        if (step_ == Step::Rotate) {
            if (isAny(w, {"S", "SI", "SÌ", "Y", "YES"}))
                polar(true);
            else if (isAny(w, {"N", "NO"}))
                polar(false);
            else
                return false;
            return true;
        }
        return false;
    }

    bool number(double v) override
    {
        switch (step_) {
        case Step::Rows:
        case Step::Columns:
        case Step::Count: {
            const int n = static_cast<int>(std::lround(v));
            if (n < 1 || std::abs(v - n) > 1e-9 || (step_ == Step::Count && n < 2) || n > 1000) {
                engine->message(t("Immettere un numero intero da %1 a 1000.").arg(step_ == Step::Count ? 2 : 1));
                return true;
            }
            if (step_ == Step::Rows) {
                rows_ = n;
                toColumns();
            } else if (step_ == Step::Columns) {
                columns_ = n;
                toRowDistance();
            } else {
                count_ = n;
                step_ = Step::Fill;
                engine->setPrompt(t("Specificare l'angolo da riempire (+=antiorario, -=orario) <360>:"));
            }
            return true;
        }
        case Step::RowDistance:
            rowDistance_ = v;
            toColumnDistance();
            return true;
        case Step::ColumnDistance:
            columnDistance_ = v;
            rectangular();
            return true;
        case Step::Fill:
            if (std::abs(v) < kEps || std::abs(v) > 360.0 + kEps) {
                engine->message(t("L'angolo deve essere tra -360 e 360, diverso da 0."));
                return true;
            }
            fill_ = v;
            toRotate();
            return true;
        default:
            return false;
        }
    }

    std::optional<Vec2> base() const override
    {
        if (step_ == Step::Count || step_ == Step::Fill)
            return center_;
        return std::nullopt;
    }

protected:
    void afterSelection() override
    {
        step_ = Step::Kind;
        engine->setPrompt(t("Immettere tipo di serie [Rettangolare/POlare] <R>:"));
    }

    bool onPoint(Vec2 p) override
    {
        if (step_ != Step::Center)
            return false;
        center_ = p;
        step_ = Step::Count;
        engine->setPrompt(t("Immettere il numero di elementi nella serie:"));
        return true;
    }

    void onEnter() override
    {
        switch (step_) {
        case Step::Kind: toRows(); break;
        case Step::Rows: toColumns(); break;
        case Step::Columns: toRowDistance(); break;
        case Step::Fill: toRotate(); break;
        case Step::Rotate: polar(true); break;
        default: done(); break;
        }
    }

private:
    enum class Step { Kind, Rows, Columns, RowDistance, ColumnDistance, Center, Count, Fill, Rotate };

    void toRows()
    {
        step_ = Step::Rows;
        engine->setPrompt(t("Immettere il numero di righe (---) <%1>:").arg(rows_));
    }
    void toColumns()
    {
        step_ = Step::Columns;
        engine->setPrompt(t("Immettere il numero di colonne (|||) <%1>:").arg(columns_));
    }
    void toRowDistance()
    {
        if (rows_ == 1) {
            toColumnDistance();
            return;
        }
        step_ = Step::RowDistance;
        engine->setPrompt(t("Immettere la distanza tra le righe (---):"));
    }
    void toColumnDistance()
    {
        if (columns_ == 1) {
            rectangular();
            return;
        }
        step_ = Step::ColumnDistance;
        engine->setPrompt(t("Specificare la distanza tra le colonne (|||):"));
    }
    void toCenter()
    {
        step_ = Step::Center;
        engine->setPrompt(t("Specificare il punto centrale della serie:"));
    }
    void toRotate()
    {
        step_ = Step::Rotate;
        engine->setPrompt(t("Ruotare gli oggetti nella serie? [Sì/No] <S>:"));
    }

    void rectangular()
    {
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < columns_; ++c)
                if (r != 0 || c != 0)
                    applyTransform(Transform::translation({c * columnDistance_, r * rowDistance_}), true);
        done();
    }

    void polar(bool rotate)
    {
        // A full turn spreads the copies evenly; a part of it puts the last one at its end.
        const double step = degToRad(std::abs(std::abs(fill_) - 360.0) < kEps ? fill_ / count_ : fill_ / (count_ - 1));
        BBox box;
        for (EntityId id : ids_)
            if (const Entity* e = engine->document().entity(id))
                box.add(boundingBox(e->shape));
        const Vec2 reference = box.valid() ? box.center() : center_;
        for (int i = 1; i < count_; ++i) {
            const Transform turn = Transform::rotation(center_, step * i);
            applyTransform(rotate ? turn : Transform::translation(turn.apply(reference) - reference), true);
        }
        done();
    }

    Step step_ = Step::Kind;
    int rows_ = 1;
    int columns_ = 1;
    double rowDistance_ = 0.0;
    double columnDistance_ = 0.0;
    Vec2 center_;
    int count_ = 2;
    double fill_ = 360.0;
};

// The objects of a block reference, one level down, each with its own look:
// those on layer 0 go to the layer of the reference and BYBLOCK takes the
// colour, linetype and lineweight of the reference.
void explodeInsert(Document& doc, const Entity& ref, const Insert& insert)
{
    const Transform t = insertTransform(insert);
    for (const BlockItem& item : insert.block->items) {
        Entity e = ref;
        e.id = 0;
        e.shape = transformed(item.shape, t);
        e.layer = item.layer == "0" ? ref.layer : item.layer;
        e.color = item.colorByBlock ? ref.color : item.color;
        e.linetype = linetypeKey(item.linetype) == kByBlock ? ref.linetype : item.linetype;
        e.lineweight = item.lineweight == kLineweightByBlock ? ref.lineweight : item.lineweight;
        if (!doc.linetype(e.linetype))
            e.linetype = kByLayer;
        doc.add(std::move(e));
    }
}

class ExplodeCommand : public SelectionCommand {
protected:
    void afterSelection() override
    {
        Document& doc = engine->document();
        int exploded = 0;
        int kept = 0;
        for (EntityId id : ids_) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            const Entity original = *e;
            if (const auto* ref = std::get_if<Insert>(&original.shape); ref && ref->block) {
                doc.remove(id);
                explodeInsert(doc, original, *ref);
                ++exploded;
                continue;
            }
            const std::vector<Shape> parts = explodeShape(original.shape);
            if (parts.empty()) {
                ++kept;
                continue;
            }
            doc.remove(id);
            for (const Shape& part : parts)
                addLike(doc, original, part);
            ++exploded;
        }
        engine->message(QCoreApplication::translate("Command", "%n oggetti esplosi", nullptr, exploded));
        if (kept > 0)
            engine->message(QCoreApplication::translate("Command", "%n oggetti non si possono esplodere", nullptr, kept));
        done();
    }
};

class JoinCommand : public SelectionCommand {
protected:
    void afterSelection() override
    {
        Document& doc = engine->document();
        std::vector<Entity> entities;
        std::vector<Shape> shapes;
        for (EntityId id : ids_)
            if (const Entity* e = doc.entity(id)) {
                entities.push_back(*e);
                shapes.push_back(e->shape);
            }
        int joined = 0;
        for (const JoinedShape& j : joinShapes(shapes, 1e-6)) {
            for (size_t i : j.sources)
                doc.remove(entities[i].id);
            addLike(doc, entities[j.sources.front()], j.shape);
            joined += static_cast<int>(j.sources.size());
        }
        if (joined == 0)
            engine->message(t("Nessun oggetto unito: le estremità devono coincidere."));
        else
            engine->message(QCoreApplication::translate("Command", "%n oggetti uniti", nullptr, joined));
        done();
    }
};

class EllipseCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare punto finale asse dell'ellisse o [Centro]:")); }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::First || !isAny(w, {"C", "CENTRO", "CENTER"}))
            return false;
        step_ = Step::Center;
        engine->setPrompt(t("Specificare centro dell'ellisse:"));
        return true;
    }

    bool point(Vec2 p) override
    {
        switch (step_) {
        case Step::First:
            first_ = p;
            step_ = Step::Second;
            engine->setPrompt(t("Specificare altro punto finale dell'asse:"));
            return true;
        case Step::Center:
            center_ = p;
            step_ = Step::AxisEnd;
            engine->setPrompt(t("Specificare punto finale dell'asse:"));
            return true;
        case Step::Second:
        case Step::AxisEnd: {
            if (step_ == Step::Second)
                center_ = (first_ + p) * 0.5;
            major_ = p - center_;
            if (length(major_) < kEps)
                return false;
            step_ = Step::Other;
            engine->setPrompt(t("Specificare distanza dall'altro asse:"));
            return true;
        }
        case Step::Other:
            return create(distance(center_, p));
        }
        return false;
    }

    bool number(double v) override { return step_ == Step::Other && create(v); }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Other) {
            if (const auto pl = ellipse(distance(center_, cursor)))
                out.push_back(*pl);
            out.push_back(Line{center_, cursor});
        } else if (step_ == Step::Second || step_ == Step::AxisEnd) {
            out.push_back(Line{step_ == Step::Second ? first_ : center_, cursor});
        }
    }

    std::optional<Vec2> base() const override
    {
        switch (step_) {
        case Step::Second: return first_;
        case Step::AxisEnd:
        case Step::Other: return center_;
        default: return std::nullopt;
        }
    }

private:
    enum class Step { First, Second, Center, AxisEnd, Other };

    std::optional<Polyline> ellipse(double other) const
    {
        const double half = length(major_);
        if (other < kEps)
            return std::nullopt;
        // The longer half-axis is the major one.
        if (other > half)
            return ellipsePolyline(center_, normalized(perpendicular(major_)) * other, half / other);
        return ellipsePolyline(center_, major_, other / half);
    }

    bool create(double other)
    {
        const auto pl = ellipse(std::abs(other));
        if (!pl)
            return false;
        engine->document().add(*pl);
        engine->finish();
        return true;
    }

    Step step_ = Step::First;
    Vec2 first_, center_, major_;
};

class SplineCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto:")); }

    bool point(Vec2 p) override
    {
        if (!points_.empty() && almostEqual(points_.back(), p))
            return false;
        points_.push_back(p);
        engine->setPrompt(points_.size() < 3 ? t("Specificare punto successivo:")
                                             : t("Specificare punto successivo o [Chiudi]:"));
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (points_.size() < 3 || !isCloseWord(w))
            return false;
        finish(true);
        return true;
    }

    void enter() override { finish(false); }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.empty())
            return;
        std::vector<Vec2> pts = points_;
        pts.push_back(cursor);
        out.push_back(splinePolyline(pts, false));
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    void finish(bool closed)
    {
        if (points_.size() >= 2)
            engine->document().add(splinePolyline(points_, closed));
        engine->finish();
    }

    std::vector<Vec2> points_;
};

// LEADER: a line from an arrowhead to a note.
class LeaderCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare punto iniziale della direttrice:")); }

    bool point(Vec2 p) override
    {
        if (writing_ || (!points_.empty() && almostEqual(points_.back(), p)))
            return false;
        points_.push_back(p);
        engine->setPrompt(t("Specificare punto successivo:"));
        return true;
    }

    void enter() override
    {
        if (writing_ || points_.size() < 2) {
            if (writing_)
                create(QString());
            else
                engine->finish();
            return;
        }
        writing_ = true;
        engine->setPrompt(t("Immettere testo dell'annotazione:"));
    }

    bool wantsText() const override { return writing_; }
    void text(const QString& s) override { create(s); }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.empty() || writing_)
            return;
        std::vector<Vec2> pts = points_;
        pts.push_back(cursor);
        out.push_back(Polyline(pts));
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() || writing_ ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    void create(const QString& note)
    {
        Document& doc = engine->document();
        doc.add(Polyline(points_));
        // Arrowhead and text as big as those of the dimensions.
        const double arrow = 2.5 * gDimScale;
        const Vec2 dir = normalized(points_[1] - points_[0]);
        const Vec2 side = perpendicular(dir) * (arrow / 6.0);
        Hatch head;
        head.pattern = "SOLID";
        head.loops.push_back(Polyline({points_[0], points_[0] + dir * arrow + side, points_[0] + dir * arrow - side}, true));
        doc.add(head);
        if (!note.trimmed().isEmpty()) {
            const Vec2 end = points_.back();
            const bool right = (end - points_[points_.size() - 2]).x >= 0.0;
            Text tx;
            tx.height = 2.5 * gDimScale;
            tx.content = note.toStdString();
            tx.align = right ? Text::Align::Left : Text::Align::Right;
            tx.position = end + Vec2{right ? tx.height * 0.5 : -tx.height * 0.5, -tx.height / 2.0};
            doc.add(tx);
        }
        engine->finish();
    }

    std::vector<Vec2> points_;
    bool writing_ = false;
};

QString fmt(double v) { return QString::fromStdString(formatMeasurement(v, gPrecision)); }

QString xy(Vec2 p) { return QStringLiteral("X=%1 Y=%2").arg(fmt(p.x), fmt(p.y)); }

class DistanceCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto:")); }

    bool point(Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            engine->setPrompt(t("Specificare secondo punto:"));
            return true;
        }
        const Vec2 d = p - *first_;
        engine->message(t("Distanza = %1,  Angolo nel piano XY = %2°,  Delta X = %3,  Delta Y = %4")
                            .arg(fmt(length(d)), fmt(radToDeg(normalizeAngle(angleOf(d)))), fmt(d.x), fmt(d.y)));
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (first_)
            out.push_back(Line{*first_, cursor});
    }

    std::optional<Vec2> base() const override { return first_; }

private:
    std::optional<Vec2> first_;
};

class AreaCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto d'angolo o [Oggetto] <Oggetto>:")); }

    bool keyword(const QString& w) override
    {
        if (!points_.empty() || picking_ || !isAny(w, {"O", "OGGETTO", "OBJECT"}))
            return false;
        toObject();
        return true;
    }

    bool point(Vec2 p) override
    {
        if (picking_) {
            const auto id = pickObject(engine, p);
            if (!id)
                return false;
            const Shape& shape = engine->document().entity(*id)->shape;
            if (const auto area = shapeArea(shape))
                engine->message(t("Area = %1,  Perimetro = %2").arg(fmt(*area), fmt(shapeLength(shape))));
            else
                engine->message(t("L'oggetto non è chiuso: lunghezza = %1").arg(fmt(shapeLength(shape))));
            engine->finish();
            return true;
        }
        points_.push_back(p);
        engine->setPrompt(t("Specificare punto successivo o premere INVIO per il totale:"));
        return true;
    }

    void enter() override
    {
        if (points_.empty() && !picking_) {
            toObject();
            return;
        }
        if (points_.size() >= 3) {
            const Polyline pl(points_, true);
            engine->message(t("Area = %1,  Perimetro = %2").arg(fmt(polylineArea(pl)), fmt(shapeLength(pl))));
        }
        engine->finish();
    }

    bool wantsObjectPick() const override { return picking_; }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.empty())
            return;
        std::vector<Vec2> pts = points_;
        pts.push_back(cursor);
        out.push_back(Polyline(pts, true));
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    void toObject()
    {
        picking_ = true;
        engine->setPrompt(t("Selezionare oggetti:"));
    }

    std::vector<Vec2> points_;
    bool picking_ = false;
};

QString shapeKind(const Shape& shape)
{
    return std::visit(
        [](const auto& s) -> QString {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, Line>) return t("LINEA");
            else if constexpr (std::is_same_v<T, Circle>) return t("CERCHIO");
            else if constexpr (std::is_same_v<T, Arc>) return t("ARCO");
            else if constexpr (std::is_same_v<T, Polyline>) return t("POLILINEA");
            else if constexpr (std::is_same_v<T, Text>) return t("TESTO");
            else if constexpr (std::is_same_v<T, MText>) return t("TESTOM");
            else if constexpr (std::is_same_v<T, Dimension>) return t("QUOTA");
            else if constexpr (std::is_same_v<T, Hatch>) return t("TRATTEGGIO");
            else if constexpr (std::is_same_v<T, Wall>) return t("MURO");
            else if constexpr (std::is_same_v<T, Slab>) return t("SOLAIO");
            else if constexpr (std::is_same_v<T, Roof>) return t("TETTO");
            else if constexpr (std::is_same_v<T, Stair>) return t("SCALINATA");
            else if constexpr (std::is_same_v<T, Point>) return t("PUNTO");
            else return t("BLOCCO");
        },
        shape);
}

class ListCommand : public SelectionCommand {
protected:
    void afterSelection() override
    {
        const Document& doc = engine->document();
        for (EntityId id : ids_) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            const Story* story = doc.story(e->story);
            engine->message(t("%1  Layer: %2  Piano: %3  Colore: %4  Tipo di linea: %5  Spessore: %6")
                                .arg(shapeKind(e->shape), QString::fromStdString(e->layer),
                                     story ? QString::fromStdString(story->name) : QString(), colorName(e->color),
                                     linetypeName(e->linetype), lineweightName(e->lineweight)));
            engine->message(QStringLiteral("    ") + details(e->shape));
        }
        done();
    }

private:
    static QString details(const Shape& shape)
    {
        if (const auto* l = std::get_if<Line>(&shape))
            return t("da %1 a %2, lunghezza %3, angolo %4°")
                .arg(xy(l->a), xy(l->b), fmt(shapeLength(shape)), fmt(radToDeg(normalizeAngle(angleOf(l->b - l->a)))));
        if (const auto* pt = std::get_if<Point>(&shape))
            return t("in %1").arg(xy(pt->position));
        if (const auto* c = std::get_if<Circle>(&shape))
            return t("centro %1, raggio %2, circonferenza %3, area %4")
                .arg(xy(c->center), fmt(c->radius), fmt(shapeLength(shape)), fmt(*shapeArea(shape)));
        if (const auto* a = std::get_if<Arc>(&shape))
            return t("centro %1, raggio %2, angolo iniziale %3°, finale %4°, lunghezza %5")
                .arg(xy(a->center), fmt(a->radius), fmt(radToDeg(a->startAngle)), fmt(radToDeg(a->endAngle)),
                     fmt(shapeLength(shape)));
        if (const auto* pl = std::get_if<Polyline>(&shape)) {
            QString s = t("%1 vertici, %2, lunghezza %3").arg(pl->points.size()).arg(pl->closed ? t("chiusa") : t("aperta"), fmt(shapeLength(shape)));
            if (const auto area = shapeArea(shape))
                s += t(", area %1").arg(fmt(*area));
            return s;
        }
        if (const auto* tx = std::get_if<Text>(&shape))
            return t("\"%1\" in %2, altezza %3").arg(QString::fromStdString(tx->content), xy(tx->position), fmt(tx->height));
        if (const auto* m = std::get_if<MText>(&shape))
            return t("\"%1\" in %2, altezza %3, larghezza %4")
                .arg(QString::fromStdString(m->content).replace(QLatin1Char('\n'), QStringLiteral(" / ")), xy(m->position),
                     fmt(m->height), fmt(m->width));
        if (const auto* d = std::get_if<Dimension>(&shape))
            return t("misura %1").arg(fmt(d->measurement()));
        if (const auto* h = std::get_if<Hatch>(&shape))
            return t("motivo %1, %2 contorni").arg(QString::fromStdString(h->pattern)).arg(h->loops.size());
        if (const auto* w = std::get_if<Wall>(&shape))
            return t("da %1 a %2, lunghezza %3, spessore %4, altezza %5, %6 aperture")
                .arg(xy(w->start), xy(w->end), fmt(distance(w->start, w->end)), fmt(w->thickness), fmt(w->height))
                .arg(w->openings.size());
        if (const auto* ref = std::get_if<Insert>(&shape))
            return t("\"%1\" in %2, scala X %3 Y %4, rotazione %5°")
                .arg(ref->block ? QString::fromStdString(ref->block->name) : QString(), xy(ref->position), fmt(ref->scaleX),
                     fmt(ref->scaleY), fmt(radToDeg(ref->rotation)));
        return {};
    }
};

// ---------------------------------------------------------------- blocks

QString blockList(const Document& doc)
{
    if (doc.blocks().empty())
        return t("Il disegno non contiene blocchi.");
    QStringList names;
    for (const auto& [key, b] : doc.blocks())
        names << QString::fromStdString(b->name);
    return t("Blocchi: %1").arg(names.join(QStringLiteral(", ")));
}

// AutoCAD refuses these characters in names.
bool validBlockName(const QString& name)
{
    static const QString kInvalid = QStringLiteral("<>/\\\":;?*|,=`");
    if (name.isEmpty() || name.size() > 255 || name.startsWith(QLatin1Char('*')))
        return false;
    return std::none_of(name.begin(), name.end(), [](QChar c) { return kInvalid.contains(c); });
}

// The block last made or inserted: INSERT offers it first.
std::string gLastBlock;

// BLOCK (-BLOCK in AutoCAD): name, base point and objects. The objects become
// the block and are replaced by a reference to it, as with "Converti in blocco".
class BlockCommand : public Command {
public:
    void start() override
    {
        if (!engine->selection().empty())
            preselected_.assign(engine->selection().begin(), engine->selection().end());
        engine->setPrompt(t("Specificare nome del blocco o [?]:"));
    }

    bool wantsText() const override { return step_ == Step::Name; }
    bool wantsSelection() const override { return step_ == Step::Objects; }

    void text(const QString& raw) override
    {
        const QString name = raw.trimmed();
        if (name == QLatin1String("?")) {
            engine->message(blockList(engine->document()));
            return;
        }
        if (!validBlockName(name)) {
            engine->message(t("Nome non valido: \"%1\".").arg(name));
            return;
        }
        name_ = name;
        if (engine->document().block(name.toStdString())) {
            step_ = Step::Redefine;
            engine->setPrompt(t("Il blocco \"%1\" esiste già. Ridefinirlo? [Sì/No] <No>:").arg(name));
            return;
        }
        toBase();
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Redefine)
            return false;
        if (isAny(w, {"S", "SI", "SÌ", "Y", "YES"})) {
            toBase();
            return true;
        }
        if (isAny(w, {"N", "NO"})) {
            engine->finish();
            return true;
        }
        return false;
    }

    bool point(Vec2 p) override
    {
        if (step_ != Step::Base)
            return false;
        base_ = p;
        if (!preselected_.empty()) {
            make(preselected_);
            return true;
        }
        engine->clearSelection();
        step_ = Step::Objects;
        engine->setPrompt(t("Selezionare oggetti:"));
        return true;
    }

    void enter() override
    {
        if (step_ == Step::Objects && !engine->selection().empty()) {
            make({engine->selection().begin(), engine->selection().end()});
            return;
        }
        if (step_ == Step::Objects)
            engine->message(t("Nessun oggetto selezionato: blocco non creato."));
        engine->finish();
    }

private:
    enum class Step { Name, Redefine, Base, Objects };

    void toBase()
    {
        step_ = Step::Base;
        engine->setPrompt(t("Specificare punto base di inserimento:"));
    }

    void make(const std::vector<EntityId>& ids)
    {
        Document& doc = engine->document();
        std::vector<BlockItem> items;
        for (EntityId id : ids) {
            const Entity* e = doc.entity(id);
            if (!e)
                continue;
            BlockItem item;
            item.shape = e->shape;
            item.layer = e->layer;
            item.color = e->color;
            item.linetype = e->linetype;
            item.lineweight = e->lineweight;
            items.push_back(std::move(item));
        }
        const std::string name = name_.toStdString();
        const auto block = makeBlock(name, base_, std::move(items));
        if (!doc.defineBlock(block)) {
            engine->message(t("Un blocco non può contenere se stesso."));
            engine->finish();
            return;
        }
        for (EntityId id : ids)
            doc.remove(id);
        Insert ref;
        ref.block = block;
        ref.position = base_;
        doc.add(ref);
        gLastBlock = name;
        engine->message(QCoreApplication::translate("Command", "Blocco \"%1\" creato con %n oggetti.", nullptr,
                                                    static_cast<int>(block->items.size()))
                            .arg(name_));
        engine->clearSelection();
        engine->finish();
    }

    Step step_ = Step::Name;
    QString name_;
    Vec2 base_;
    std::vector<EntityId> preselected_;
};

// INSERT (-INSERT in AutoCAD): a reference to a block at the point picked,
// with the scale and rotation set before it.
class InsertCommand : public Command {
public:
    void start() override
    {
        const Document& doc = engine->document();
        if (doc.blocks().empty()) {
            engine->message(t("Il disegno non contiene blocchi: crearli con BLOCCO."));
            engine->finish();
            return;
        }
        if (!doc.block(gLastBlock))
            gLastBlock = doc.blocks().begin()->second->name;
        engine->setPrompt(t("Specificare nome del blocco o [?] <%1>:").arg(QString::fromStdString(gLastBlock)));
    }

    bool wantsText() const override { return step_ == Step::Name; }

    void text(const QString& raw) override
    {
        const QString name = raw.trimmed();
        if (name == QLatin1String("?")) {
            engine->message(blockList(engine->document()));
            return;
        }
        choose(name.toStdString());
    }

    void enter() override
    {
        if (step_ == Step::Name)
            choose(gLastBlock);
        else if (step_ == Step::Point)
            engine->finish();
        else
            toPoint();
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Point)
            return false;
        if (isAny(w, {"S", "SCALA", "SCALE"})) {
            step_ = Step::Scale;
            engine->setPrompt(t("Specificare fattore di scala <%1>:").arg(fmt(scale_)));
            return true;
        }
        if (isAny(w, {"R", "ROTAZIONE", "ROTATE"})) {
            step_ = Step::Rotation;
            engine->setPrompt(t("Specificare angolo di rotazione <%1>:").arg(fmt(rotation_)));
            return true;
        }
        return false;
    }

    bool number(double v) override
    {
        if (step_ == Step::Scale) {
            if (std::abs(v) < kEps)
                return false;
            scale_ = v;
        } else if (step_ == Step::Rotation) {
            rotation_ = v;
        } else {
            return false;
        }
        toPoint();
        return true;
    }

    bool point(Vec2 p) override
    {
        if (step_ != Step::Point)
            return false;
        engine->document().add(placed(p));
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Point)
            out.push_back(placed(cursor));
    }

private:
    enum class Step { Name, Point, Scale, Rotation };

    void choose(const std::string& name)
    {
        block_ = engine->document().block(name);
        if (!block_) {
            engine->message(t("Il blocco \"%1\" non esiste.").arg(QString::fromStdString(name)));
            return;
        }
        gLastBlock = block_->name;
        toPoint();
    }

    void toPoint()
    {
        step_ = Step::Point;
        engine->setPrompt(t("Specificare punto di inserimento o [Scala/Rotazione]:"));
    }

    Insert placed(Vec2 p) const
    {
        Insert ref;
        ref.block = block_;
        ref.position = p;
        ref.scaleX = scale_;
        ref.scaleY = scale_;
        ref.rotation = degToRad(rotation_);
        return ref;
    }

    Step step_ = Step::Name;
    std::shared_ptr<const Block> block_;
    double scale_ = 1.0;
    double rotation_ = 0.0;  // degrees
};


// ---------------------------------------------------------------- sections

// The section or elevation as a block: the cut parts filled and outlined
// thick, what lies beyond thin, the ground line and the title under it.
// Its base is the left end of the ground line.
std::shared_ptr<const Block> sectionBlock(const SectionDrawing& d, const std::string& name)
{
    std::vector<BlockItem> items;
    const Color cutFill{96, 96, 96};
    for (const Polyline& p : d.cut) {
        if (p.closed) {
            BlockItem fill;
            Hatch h;
            h.pattern = "SOLID";
            h.loops = {p};
            fill.shape = h;
            fill.color = cutFill;
            items.push_back(std::move(fill));
        }
        BlockItem outline;
        outline.shape = p;
        outline.lineweight = 50;
        items.push_back(std::move(outline));
    }
    for (const Line& l : d.beyond) {
        BlockItem item;
        item.shape = l;
        item.lineweight = 18;
        items.push_back(std::move(item));
    }
    BlockItem ground;
    ground.shape = d.ground;
    ground.lineweight = 70;
    items.push_back(std::move(ground));
    const double width = d.ground.b.x - d.ground.a.x;
    Text title;
    title.content = name;
    title.height = std::clamp(width / 40.0, 150.0, 500.0);
    title.position = {d.ground.a.x, d.ground.a.y - title.height * 2.5};
    BlockItem titleItem;
    titleItem.shape = title;
    items.push_back(std::move(titleItem));
    return makeBlock(name, d.ground.a, std::move(items));
}

// A free block name: "SEZIONE A-A", "SEZIONE B-B"... or "PROSPETTO SUD 2".
std::string freeBlockName(const Document& doc, const std::function<std::string(int)>& candidate)
{
    for (int i = 0;; ++i)
        if (!doc.block(candidate(i)))
            return candidate(i);
}

// Places the drawing as a block whose base goes where the user clicks.
class SectionPlacement {
public:
    bool prepare(const Document& doc, const SectionPlane& plane, const std::string& name, CommandEngine* engine)
    {
        const Model3D model = buildingModel(doc);
        if (model.empty()) {
            engine->message(t("Il disegno non ha muri, solai o tetti da mostrare."));
            return false;
        }
        const SectionDrawing d = sectionDrawing(model, plane);
        if (d.empty()) {
            engine->message(t("Nessun elemento dell'edificio davanti alla linea scelta."));
            return false;
        }
        block_ = sectionBlock(d, name);
        return true;
    }

    Insert placed(Vec2 p) const
    {
        Insert ref;
        ref.block = block_;
        ref.position = p;
        return ref;
    }

    void place(CommandEngine* engine, Vec2 p) const
    {
        Document& doc = engine->document();
        doc.defineBlock(block_);
        doc.add(placed(p));
        engine->message(t("%1 inserito: si sposta, si copia e si esplode come un blocco.").arg(QString::fromStdString(block_->name)));
    }

    const std::shared_ptr<const Block>& block() const { return block_; }

private:
    std::shared_ptr<const Block> block_;
};

// SECTION: a cut through the building along a line, seen from one side,
// drawn as a block; the section line goes in the plan with its letter.
class SectionCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare primo punto della linea di sezione:")); }

    bool point(Vec2 p) override
    {
        switch (step_) {
        case Step::First:
            a_ = p;
            step_ = Step::Second;
            engine->setPrompt(t("Specificare secondo punto:"));
            return true;
        case Step::Second:
            if (distance(a_, p) < kEps)
                return false;
            b_ = p;
            step_ = Step::Side;
            engine->setPrompt(t("Specificare il lato verso cui guardare:"));
            return true;
        case Step::Side: {
            const Vec2 n = lookDirection(p);
            if (length(n) < kEps)
                return false;
            const Document& doc = engine->document();
            letter_ = freeLetter(doc);
            plane_.origin = a_;
            plane_.direction = n;
            const double xa = sectionX(plane_, a_);
            const double xb = sectionX(plane_, b_);
            plane_.from = std::min(xa, xb);
            plane_.to = std::max(xa, xb);
            const std::string name = "SEZIONE " + letter_ + "-" + letter_;
            if (!placement_.prepare(doc, plane_, name, engine)) {
                engine->finish();
                return true;
            }
            step_ = Step::Place;
            engine->setPrompt(t("Specificare punto di inserimento della sezione:"));
            return true;
        }
        case Step::Place:
            addMarker();
            placement_.place(engine, p);
            engine->finish();
            return true;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Second)
            out.push_back(Line{a_, cursor});
        else if (step_ == Step::Side)
            for (const Shape& s : marker(lookDirection(cursor), "?"))
                out.push_back(s);
        else if (step_ == Step::Place)
            out.push_back(placement_.placed(cursor));
    }

private:
    enum class Step { First, Second, Side, Place };

    Vec2 lookDirection(Vec2 side) const
    {
        const Vec2 n = perpendicular(normalized(b_ - a_));
        const double s = dot(side - a_, n);
        return std::abs(s) < kEps ? Vec2{} : n * (s > 0.0 ? 1.0 : -1.0);
    }

    static std::string freeLetter(const Document& doc)
    {
        for (char c = 'A'; c <= 'Z'; ++c) {
            const std::string l(1, c);
            if (!doc.block("SEZIONE " + l + "-" + l))
                return l;
        }
        return freeBlockName(doc, [](int i) { return std::to_string(i + 1); });
    }

    // The section line, with arrows towards the side seen and the letter
    // at both ends.
    std::vector<Shape> marker(Vec2 n, const std::string& letter) const
    {
        std::vector<Shape> out;
        out.push_back(Line{a_, b_});
        if (length(n) < kEps)
            return out;
        const double size = std::clamp(distance(a_, b_) / 10.0, 300.0, 800.0);
        const Vec2 d = normalized(b_ - a_);
        for (Vec2 end : {a_, b_}) {
            const Vec2 tip = end + n * size;
            out.push_back(Line{end, tip});
            out.push_back(Line{tip, tip - n * (size * 0.35) + d * (size * 0.2)});
            out.push_back(Line{tip, tip - n * (size * 0.35) - d * (size * 0.2)});
            Text label;
            label.content = letter;
            label.height = size * 0.6;
            label.align = Text::Align::Center;
            const Vec2 outward = end == a_ ? -d : d;
            label.position = end + outward * (size * 0.5) + n * (size * 0.3) - Vec2{0.0, label.height * 0.5};
            out.push_back(label);
        }
        return out;
    }

    void addMarker()
    {
        for (const Shape& s : marker(plane_.direction, letter_))
            engine->document().add(s);
    }

    Step step_ = Step::First;
    Vec2 a_, b_;
    SectionPlane plane_;
    std::string letter_;
    SectionPlacement placement_;
};

// ELEVATION: the building seen from one side, drawn as a block.
class ElevationCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Specificare lato [Nord/Sud/Est/Ovest] <Sud>:"));
    }

    bool keyword(const QString& w) override
    {
        if (step_ != Step::Side)
            return false;
        if (isAny(w, {"N", "NORD", "NORTH"}))
            choose({0.0, -1.0}, "NORD");
        else if (isAny(w, {"S", "SUD", "SOUTH"}))
            choose({0.0, 1.0}, "SUD");
        else if (isAny(w, {"E", "EST", "EAST"}))
            choose({-1.0, 0.0}, "EST");
        else if (isAny(w, {"O", "OVEST", "W", "WEST"}))
            choose({1.0, 0.0}, "OVEST");
        else
            return false;
        return true;
    }

    void enter() override
    {
        if (step_ == Step::Side)
            choose({0.0, 1.0}, "SUD");
        else
            engine->finish();
    }

    bool point(Vec2 p) override
    {
        if (step_ != Step::Place)
            return false;
        placement_.place(engine, p);
        engine->finish();
        return true;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (step_ == Step::Place)
            out.push_back(placement_.placed(cursor));
    }

private:
    enum class Step { Side, Place };

    // `direction`: where the viewer looks, from the side named `side`.
    void choose(Vec2 direction, const std::string& side)
    {
        const Document& doc = engine->document();
        SectionPlane plane;
        plane.direction = direction;
        // In front of the whole drawing.
        const BBox all = doc.extents();
        const Vec2 c = all.valid() ? all.center() : Vec2{};
        const double reach = all.valid() ? all.width() + all.height() + 1000.0 : 1000.0;
        plane.origin = c - direction * reach;
        const std::string base = "PROSPETTO " + side;
        const std::string name = freeBlockName(doc, [&](int i) { return i == 0 ? base : base + " " + std::to_string(i + 1); });
        if (!placement_.prepare(doc, plane, name, engine)) {
            engine->finish();
            return;
        }
        step_ = Step::Place;
        engine->setPrompt(t("Specificare punto di inserimento del prospetto:"));
    }

    Step step_ = Step::Side;
    SectionPlacement placement_;
};
} // namespace

// PIANTA3D: walls, doors and windows from a plan drawn with lines, such as
// a DWG received from a client. The lines that became walls go to a layer
// that is turned off, so nothing is lost.
class PlanTo3dCommand : public SelectionCommand {
public:
    static constexpr const char* kOriginalLayer = "Pianta 2D originale";

    void preview(Vec2, std::vector<Shape>& out) const override
    {
        for (const Wall& w : plan_.walls)
            out.push_back(w);
    }

protected:
    void afterSelection() override
    {
        const Document& doc = engine->document();
        for (EntityId id : ids_) {
            const Entity* e = doc.entity(id);
            if (!e || isBuildingElement(e->shape))
                continue;
            doc.forEachPart(*e, [&](const Entity& part) {
                shapes_.push_back(part.shape);
                owners_.push_back(id);
            });
        }
        setUnit(guessPlanUnit(shapes_));
    }

    bool keyword(const QString& w) override
    {
        if (ask_ == Ask::Unit) {
            if (isAny(w, {"MI", "MILLIMETRI", "MM"}))
                setUnit(1.0);
            else if (isAny(w, {"C", "CENTIMETRI", "CM"}))
                setUnit(10.0);
            else if (isAny(w, {"ME", "METRI", "M"}))
                setUnit(1000.0);
            else
                return false;
            return true;
        }
        if (ask_ != Ask::Option)
            return false;
        if (isAny(w, {"A", "ALTEZZA"}))
            ask(Ask::WallHeight);
        else if (isAny(w, {"P", "PORTE"}))
            ask(Ask::DoorHeight);
        else if (isAny(w, {"F", "FINESTRE"}))
            ask(Ask::WindowHeight);
        else if (isAny(w, {"S", "SPESSORI"}))
            ask(Ask::MinThickness);
        else if (isAny(w, {"U", "UNITA", "UNITÀ"}))
            ask(Ask::Unit);
        else
            return false;
        return true;
    }

    bool onPoint(Vec2) override { return false; }

    bool number(double v) override
    {
        if (v <= 0.0 || ask_ == Ask::Option || ask_ == Ask::Unit)
            return false;
        PlanRecognitionOptions& o = options_;
        switch (ask_) {
        case Ask::WallHeight:
            o.wallHeight = v;
            break;
        case Ask::DoorHeight:
            o.door.height = v;
            break;
        case Ask::WindowHeight:
            o.window.height = v;
            ask(Ask::Sill);
            return true;
        case Ask::Sill:
            o.window.sill = v;
            break;
        case Ask::MinThickness:
            o.minThickness = v;
            o.maxThickness = std::max(o.maxThickness, v);
            ask(Ask::MaxThickness);
            return true;
        case Ask::MaxThickness:
            if (v < o.minThickness)
                return false;
            o.maxThickness = v;
            break;
        default:
            break;
        }
        recognize();
        return true;
    }

    void onEnter() override
    {
        if (ask_ == Ask::Option) {
            apply();
            return;
        }
        // Enter keeps the value shown.
        if (ask_ == Ask::WindowHeight)
            ask(Ask::Sill);
        else if (ask_ == Ask::MinThickness)
            ask(Ask::MaxThickness);
        else
            recognize();
    }

private:
    enum class Ask { Option, WallHeight, DoorHeight, WindowHeight, Sill, MinThickness, MaxThickness, Unit };

    static bool isBuildingElement(const Shape& s)
    {
        return std::holds_alternative<Wall>(s) || std::holds_alternative<Slab>(s) || std::holds_alternative<Roof>(s)
            || std::holds_alternative<Stair>(s);
    }

    static QString unitName(double unit)
    {
        return unit >= 1000.0 ? t("metri") : unit >= 10.0 ? t("centimetri") : t("millimetri");
    }

    // The defaults (in millimetres, heights from the wall, door and window
    // tools) in the unit of the drawing.
    void setUnit(double unit)
    {
        unit_ = unit;
        PlanRecognitionOptions base;
        const ToolSettings& tools = toolSettings();
        base.wallHeight = tools.wallHeight;
        base.door = tools.door;
        base.window = tools.window;
        options_ = base.inUnits(unit);
        recognize();
    }

    void recognize()
    {
        plan_ = recognizePlan(shapes_, options_);
        // Said again only when something changed, not for every value typed.
        const QString found = t("Riconosciuti %1 muri, %2 porte e %3 finestre (disegno in %4, muri spessi da %5 a %6)")
                            .arg(plan_.walls.size())
                            .arg(plan_.doors)
                            .arg(plan_.windows)
                            .arg(unitName(unit_))
                            .arg(fmt(options_.minThickness))
                            .arg(fmt(options_.maxThickness));
        if (found != lastFound_)
            engine->message(found);
        lastFound_ = found;
        ask(Ask::Option);
    }

    void ask(Ask a)
    {
        ask_ = a;
        const PlanRecognitionOptions& o = options_;
        switch (a) {
        case Ask::Option:
            engine->setPrompt(t("Specificare opzione [Altezza/Porte/Finestre/Spessori/Unità] <Converti>:"));
            break;
        case Ask::WallHeight:
            engine->setPrompt(t("Specificare altezza dei muri <%1>:").arg(fmt(o.wallHeight)));
            break;
        case Ask::DoorHeight:
            engine->setPrompt(t("Specificare altezza delle porte <%1>:").arg(fmt(o.door.height)));
            break;
        case Ask::WindowHeight:
            engine->setPrompt(t("Specificare altezza delle finestre <%1>:").arg(fmt(o.window.height)));
            break;
        case Ask::Sill:
            engine->setPrompt(t("Specificare altezza del davanzale <%1>:").arg(fmt(o.window.sill)));
            break;
        case Ask::MinThickness:
            engine->setPrompt(t("Specificare spessore minimo dei muri <%1>:").arg(fmt(o.minThickness)));
            break;
        case Ask::MaxThickness:
            engine->setPrompt(t("Specificare spessore massimo dei muri <%1>:").arg(fmt(o.maxThickness)));
            break;
        case Ask::Unit:
            engine->setPrompt(t("Specificare unità del disegno [MIllimetri/Centimetri/MEtri] <%1>:").arg(unitName(unit_)));
            break;
        }
    }

    void apply()
    {
        Document& doc = engine->document();
        if (plan_.walls.empty()) {
            engine->message(t("Nessun muro riconosciuto: i muri vanno disegnati con due linee parallele. "
                              "Provare l'opzione Spessori o Unità."));
            done();
            return;
        }
        for (const Wall& w : plan_.walls)
            doc.add(w);
        std::set<EntityId> moved;
        for (size_t k : plan_.used)
            moved.insert(owners_[k]);
        if (!doc.layer(kOriginalLayer))
            doc.addLayer({kOriginalLayer, {128, 128, 128}, false, false});
        for (EntityId id : moved)
            if (const Entity* e = doc.entity(id))
                doc.restyle(id, kOriginalLayer, styleOf(*e));
        engine->message(t("Creati %1 muri con %2 porte e %3 finestre. Le linee usate sono sul layer spento \"%4\".")
                            .arg(plan_.walls.size())
                            .arg(plan_.doors)
                            .arg(plan_.windows)
                            .arg(QString::fromUtf8(kOriginalLayer)));
        done();
    }

    std::vector<Shape> shapes_;
    std::vector<EntityId> owners_;
    double unit_ = 1.0;
    PlanRecognitionOptions options_;
    RecognizedPlan plan_;
    QString lastFound_;
    Ask ask_ = Ask::Option;
};

std::unique_ptr<Command> makeLineCommand() { return std::make_unique<LineCommand>(); }
std::unique_ptr<Command> makePlanTo3dCommand() { return std::make_unique<PlanTo3dCommand>(); }
std::unique_ptr<Command> makePointCommand() { return std::make_unique<PointCommand>(); }
std::unique_ptr<Command> makePolygonCommand() { return std::make_unique<PolygonCommand>(); }
std::unique_ptr<Command> makeBreakCommand() { return std::make_unique<BreakCommand>(); }
std::unique_ptr<Command> makePolylineCommand() { return std::make_unique<PolylineCommand>(); }
std::unique_ptr<Command> makeCircleCommand() { return std::make_unique<CircleCommand>(); }
std::unique_ptr<Command> makeArcCommand() { return std::make_unique<ArcCommand>(); }
std::unique_ptr<Command> makeRectangleCommand() { return std::make_unique<RectangleCommand>(); }
std::unique_ptr<Command> makeEraseCommand() { return std::make_unique<EraseCommand>(); }
std::unique_ptr<Command> makeMoveCommand(bool copy) { return std::make_unique<MoveCommand>(copy); }
std::unique_ptr<Command> makeRotateCommand() { return std::make_unique<RotateCommand>(); }
std::unique_ptr<Command> makeScaleCommand() { return std::make_unique<ScaleCommand>(); }
std::unique_ptr<Command> makeMirrorCommand() { return std::make_unique<MirrorCommand>(); }
std::unique_ptr<Command> makeOffsetCommand() { return std::make_unique<OffsetCommand>(); }
std::unique_ptr<Command> makeTrimCommand() { return std::make_unique<TrimExtendCommand>(false); }
std::unique_ptr<Command> makeExtendCommand() { return std::make_unique<TrimExtendCommand>(true); }
std::unique_ptr<Command> makeFilletCommand() { return std::make_unique<FilletCommand>(); }
std::unique_ptr<Command> makeChamferCommand() { return std::make_unique<ChamferCommand>(); }
std::unique_ptr<Command> makeStretchCommand() { return std::make_unique<StretchCommand>(); }
std::unique_ptr<Command> makeTextCommand() { return std::make_unique<TextCommand>(); }
std::unique_ptr<Command> makeTextEditCommand() { return std::make_unique<TextEditCommand>(); }
std::unique_ptr<Command> makeMTextCommand() { return std::make_unique<MTextCommand>(); }
std::unique_ptr<Command> makeDimLinearCommand(bool aligned) { return std::make_unique<DimLinearCommand>(aligned); }
std::unique_ptr<Command> makeDimRadialCommand(bool diameter) { return std::make_unique<DimRadialCommand>(diameter); }
void setMeasurementPrecision(int decimals) { gPrecision = decimals; }
std::unique_ptr<Command> makeUnitsCommand() { return std::make_unique<UnitsCommand>(); }
std::unique_ptr<Command> makeLayerLineCommand() { return std::make_unique<LayerLineCommand>(); }
std::unique_ptr<Command> makeDimAngularCommand() { return std::make_unique<DimAngularCommand>(); }
std::unique_ptr<Command> makeDimChainCommand(bool baseline) { return std::make_unique<DimChainCommand>(baseline); }
std::unique_ptr<Command> makeDimScaleCommand() { return std::make_unique<DimScaleCommand>(); }
std::unique_ptr<Command> makePlotCommand() { return std::make_unique<PlotCommand>(); }
std::unique_ptr<Command> makeHatchCommand() { return std::make_unique<HatchCommand>(); }
std::unique_ptr<Command> makeHatchEditCommand() { return std::make_unique<HatchEditCommand>(); }
std::unique_ptr<Command> makeWallCommand() { return std::make_unique<WallCommand>(); }
std::unique_ptr<Command> makeDoorCommand() { return std::make_unique<OpeningCommand>(Opening::Kind::Door); }
std::unique_ptr<Command> makeWindowCommand() { return std::make_unique<OpeningCommand>(Opening::Kind::Window); }
std::unique_ptr<Command> makeWallEditCommand() { return std::make_unique<WallEditCommand>(); }
std::unique_ptr<Command> makeStoryCommand() { return std::make_unique<StoryCommand>(); }
std::unique_ptr<Command> makeSlabCommand() { return std::make_unique<SlabCommand>(); }
std::unique_ptr<Command> makeStairCommand() { return std::make_unique<StairCommand>(); }
std::unique_ptr<Command> makeRoofCommand() { return std::make_unique<RoofCommand>(); }
std::unique_ptr<Command> makeView3dCommand() { return std::make_unique<View3dCommand>(); }
std::unique_ptr<Command> makeZoomCommand() { return std::make_unique<ZoomCommand>(); }
std::unique_ptr<Command> makeLayerCommand() { return std::make_unique<LayerCommand>(); }
std::unique_ptr<Command> makeLayoutCommand() { return std::make_unique<LayoutCommand>(); }
std::unique_ptr<Command> makeSectionCommand() { return std::make_unique<SectionCommand>(); }
std::unique_ptr<Command> makeElevationCommand() { return std::make_unique<ElevationCommand>(); }
std::unique_ptr<Command> makeColorCommand() { return std::make_unique<CurrentStyleCommand>(StyleQuestion::Kind::Color); }
std::unique_ptr<Command> makeLinetypeCommand() { return std::make_unique<CurrentStyleCommand>(StyleQuestion::Kind::Linetype); }
std::unique_ptr<Command> makeLineweightCommand() { return std::make_unique<CurrentStyleCommand>(StyleQuestion::Kind::Lineweight); }
std::unique_ptr<Command> makeLinetypeScaleCommand() { return std::make_unique<LinetypeScaleCommand>(); }
std::unique_ptr<Command> makeChangePropertiesCommand() { return std::make_unique<ChangePropertiesCommand>(); }
std::unique_ptr<Command> makeMatchPropertiesCommand() { return std::make_unique<MatchPropertiesCommand>(); }
std::unique_ptr<Command> makeGripCommand(Vec2 grip) { return std::make_unique<GripCommand>(grip); }
std::unique_ptr<Command> makeArrayCommand() { return std::make_unique<ArrayCommand>(); }
std::unique_ptr<Command> makeExplodeCommand() { return std::make_unique<ExplodeCommand>(); }
std::unique_ptr<Command> makeJoinCommand() { return std::make_unique<JoinCommand>(); }
std::unique_ptr<Command> makeEllipseCommand() { return std::make_unique<EllipseCommand>(); }
std::unique_ptr<Command> makeSplineCommand() { return std::make_unique<SplineCommand>(); }
std::unique_ptr<Command> makeLeaderCommand() { return std::make_unique<LeaderCommand>(); }
std::unique_ptr<Command> makeDistanceCommand() { return std::make_unique<DistanceCommand>(); }
std::unique_ptr<Command> makeAreaCommand() { return std::make_unique<AreaCommand>(); }
std::unique_ptr<Command> makeListCommand() { return std::make_unique<ListCommand>(); }
std::unique_ptr<Command> makeBlockCommand() { return std::make_unique<BlockCommand>(); }
std::unique_ptr<Command> makeInsertCommand() { return std::make_unique<InsertCommand>(); }

} // namespace cad
