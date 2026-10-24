// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in drawing and editing commands. Prompts and options follow the
// Italian version of AutoCAD so that its users feel at home; English option
// letters are accepted too.
#include "commands.h"

#include "commandengine.h"
#include "editing.h"
#include "hatch.h"

#include <QCoreApplication>

#include <limits>

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

class CircleCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare centro del cerchio:")); }

    bool point(Vec2 p) override
    {
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
        if (center_ && !diameter_ && isAny(w, {"D", "DIAMETRO", "DIAMETER"})) {
            diameter_ = true;
            engine->setPrompt(t("Specificare diametro del cerchio:"));
            return true;
        }
        return false;
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (!center_)
            return;
        const double d = distance(*center_, cursor);
        if (d > kEps)
            out.push_back(Circle{*center_, diameter_ ? d / 2.0 : d});
        out.push_back(Line{*center_, cursor});
    }

    std::optional<Vec2> base() const override { return center_; }

private:
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

    std::optional<Vec2> center_;
    bool diameter_ = false;
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

    void text(const QString& s) override
    {
        Document& doc = engine->document();
        if (const Entity* e = doc.entity(target_)) {
            Shape shape = e->shape;
            if (auto* tx = std::get_if<Text>(&shape))
                tx->content = s.toStdString();
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
        for (const auto& [id, e] : doc.entities())
            if (doc.isVisible(e))
                shapes.push_back(e.shape);
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

// ---------------------------------------------------------------- view

class ZoomCommand : public Command {
public:
    void start() override
    {
        engine->setPrompt(t("Zoom [Estensioni] <Estensioni>:  (rotella del mouse per zoom, tasto centrale per spostare la vista)"));
    }

    bool keyword(const QString& w) override
    {
        if (isAny(w, {"E", "ESTENSIONI", "EXTENTS", "T", "TUTTO", "ALL"})) {
            enter();
            return true;
        }
        return false;
    }

    void enter() override
    {
        engine->requestZoomExtents();
        engine->finish();
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

} // namespace

std::unique_ptr<Command> makeLineCommand() { return std::make_unique<LineCommand>(); }
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
std::unique_ptr<Command> makeDimLinearCommand(bool aligned) { return std::make_unique<DimLinearCommand>(aligned); }
std::unique_ptr<Command> makeDimRadialCommand(bool diameter) { return std::make_unique<DimRadialCommand>(diameter); }
std::unique_ptr<Command> makeDimScaleCommand() { return std::make_unique<DimScaleCommand>(); }
std::unique_ptr<Command> makeHatchCommand() { return std::make_unique<HatchCommand>(); }
std::unique_ptr<Command> makeHatchEditCommand() { return std::make_unique<HatchEditCommand>(); }
std::unique_ptr<Command> makeZoomCommand() { return std::make_unique<ZoomCommand>(); }
std::unique_ptr<Command> makeLayerCommand() { return std::make_unique<LayerCommand>(); }

} // namespace cad
