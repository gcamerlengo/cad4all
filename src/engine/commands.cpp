// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in drawing and editing commands. Prompts and options follow the
// Italian version of AutoCAD so that its users feel at home; English option
// letters are accepted too.
#include "commands.h"

#include "commandengine.h"
#include "editing.h"

#include <QCoreApplication>

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

class PolylineCommand : public Command {
public:
    void start() override { engine->setPrompt(t("Specificare punto iniziale:")); }

    bool point(Vec2 p) override
    {
        if (!points_.empty() && almostEqual(p, points_.back()))
            return false;
        points_.push_back(p);
        updatePrompt();
        return true;
    }

    bool keyword(const QString& w) override
    {
        if (isUndoWord(w) && !points_.empty()) {
            points_.pop_back();
            if (points_.empty())
                start();
            else
                updatePrompt();
            return true;
        }
        if (isCloseWord(w) && points_.size() >= 3) {
            engine->document().add(Polyline{points_, true});
            engine->finish();
            return true;
        }
        return false;
    }

    void enter() override
    {
        if (points_.size() >= 2)
            engine->document().add(Polyline{points_, false});
        engine->finish();
    }

    void preview(Vec2 cursor, std::vector<Shape>& out) const override
    {
        if (points_.empty())
            return;
        std::vector<Vec2> pts = points_;
        pts.push_back(cursor);
        out.push_back(Polyline{pts, false});
    }

    std::optional<Vec2> base() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    void updatePrompt()
    {
        engine->setPrompt(points_.size() >= 3 ? t("Specificare punto successivo o [Chiudi/Annulla]:")
                                              : t("Specificare punto successivo o [Annulla]:"));
    }

    std::vector<Vec2> points_;
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
        const auto id = pickObject(engine, p);
        if (!id)
            return false;
        Document& doc = engine->document();
        if (!std::holds_alternative<Line>(doc.entity(*id)->shape)) {
            engine->message(t("Per ora il raccordo funziona solo tra due linee."));
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
            engine->message(t("Selezionare una linea diversa."));
            return false;
        }

        const Entity a = *doc.entity(first_);
        const Line second = std::get<Line>(doc.entity(*id)->shape);
        FilletError error = FilletError::None;
        const auto result = filletLines(std::get<Line>(a.shape), firstPick_, second, p, gFilletRadius, &error);
        if (!result) {
            engine->message(error == FilletError::Parallel ? t("Le linee sono parallele.")
                                                           : t("Il raggio è troppo grande per queste linee."));
            return false;
        }
        doc.replace(first_, result->first);
        doc.replace(*id, result->second);
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
    enum class Step { First, Radius, Second };

    void toFirst()
    {
        step_ = Step::First;
        engine->setPrompt(t("Selezionare primo oggetto o [Raggio]:"));
    }

    Step step_ = Step::First;
    EntityId first_ = 0;
    Vec2 firstPick_;
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
std::unique_ptr<Command> makeZoomCommand() { return std::make_unique<ZoomCommand>(); }
std::unique_ptr<Command> makeLayerCommand() { return std::make_unique<LayerCommand>(); }

} // namespace cad
