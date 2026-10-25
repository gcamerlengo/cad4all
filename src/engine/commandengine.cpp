// SPDX-License-Identifier: GPL-3.0-or-later
#include "commandengine.h"

#include "commands.h"
#include "coordinput.h"

#include <algorithm>

namespace cad {

CommandEngine::CommandEngine(Document& doc, QObject* parent)
    : QObject(parent), doc_(doc)
{
    registerCommands();
    prompt_ = tr("Comando:");
}

CommandEngine::~CommandEngine() = default;

void CommandEngine::add(const QString& name, const QStringList& aliases, std::function<std::unique_ptr<Command>()> factory)
{
    Registration reg{name, std::move(factory)};
    registry_[name] = reg;
    for (const QString& alias : aliases)
        registry_[alias] = reg;
    names_.append(name);
}

QStringList CommandEngine::commandNames() const
{
    return names_;
}

bool CommandEngine::run(const QString& rawName)
{
    QString name = rawName.trimmed().toUpper();
    // AutoCAD accepts "_LINE" (international name) and "'ZOOM" (transparent).
    while (name.startsWith(QLatin1Char('_')) || name.startsWith(QLatin1Char('\'')))
        name.remove(0, 1);

    if (name == QLatin1String("U") || name == QLatin1String("UNDO") || name == QLatin1String("ANNULLA")) {
        cancel();
        if (auto label = doc_.undo())
            message(label->empty() ? tr("Annullato.") : tr("Annullato: %1").arg(QString::fromStdString(*label)));
        else
            message(tr("Niente da annullare."));
        pruneSelection();
        emit changed();
        return true;
    }
    if (name == QLatin1String("REDO") || name == QLatin1String("RIPRISTINA")) {
        cancel();
        if (auto label = doc_.redo())
            message(label->empty() ? tr("Ripristinato.") : tr("Ripristinato: %1").arg(QString::fromStdString(*label)));
        else
            message(tr("Niente da ripristinare."));
        pruneSelection();
        emit changed();
        return true;
    }

    auto it = registry_.find(name);
    if (it == registry_.end()) {
        message(tr("Comando sconosciuto \"%1\". Digitare ? per l'elenco dei comandi.").arg(rawName.trimmed()));
        return false;
    }

    if (current_)
        cancel();

    current_ = it->second.factory();
    current_->engine = this;
    activeName_ = it->second.name;
    lastCommand_ = it->second.name;
    message(tr("Comando: %1").arg(activeName_));
    doc_.beginTransaction(activeName_.toStdString());
    dispatch([](Command& c) { c.start(); });
    emit changed();
    return true;
}

void CommandEngine::submit(const QString& rawText)
{
    if (current_ && current_->wantsText() && !rawText.trimmed().isEmpty()) {
        dispatch([&](Command& c) { c.text(rawText); });
        return;
    }

    const QString text = rawText.trimmed();

    if (!current_) {
        if (text.isEmpty()) {
            if (!lastCommand_.isEmpty())
                run(lastCommand_);
            return;
        }
        if (text == QLatin1String("?")) {
            message(tr("Comandi disponibili: %1, ANNULLA, RIPRISTINA").arg(names_.join(QStringLiteral(", "))));
            return;
        }
        run(text);
        return;
    }

    if (text.isEmpty()) {
        dispatch([](Command& c) { c.enter(); });
        return;
    }

    const QString upper = text.toUpper();
    bool handled = false;
    dispatch([&](Command& c) { handled = c.keyword(upper); });
    if (handled)
        return;

    if (const auto n = parseNumber(text.toStdString())) {
        dispatch([&](Command& c) { handled = c.number(*n); });
        if (handled)
            return;
        // Direct distance entry: a distance typed while a rubber band is shown
        // places the point along the cursor direction.
        if (const auto b = basePoint()) {
            Vec2 dir = normalized(constrain(cursor_) - *b);
            if (length(dir) < kEps)
                dir = {1.0, 0.0};
            pickPoint(*b + dir * *n);
            return;
        }
    }

    if (const auto p = parsePoint(text.toStdString(), lastPoint_)) {
        pickPoint(*p);
        return;
    }

    message(tr("Input non valido: \"%1\"").arg(text));
    emit promptChanged(prompt_);
}

void CommandEngine::pickPoint(Vec2 p)
{
    if (!current_)
        return;
    bool accepted = false;
    dispatch([&](Command& c) { accepted = c.point(p); });
    if (accepted)
        lastPoint_ = p;
}

void CommandEngine::cancel()
{
    if (current_) {
        message(tr("*Annulla*"));
        endCommand();
        return;
    }
    clearSelection();
}

void CommandEngine::finish()
{
    finishing_ = true;
}

void CommandEngine::dispatch(const std::function<void(Command&)>& call)
{
    if (!current_)
        return;
    call(*current_);
    if (finishing_)
        endCommand();
    emit changed();
}

void CommandEngine::endCommand()
{
    // Whatever the command already added stays in the drawing, as in AutoCAD.
    doc_.commitTransaction();
    current_.reset();
    activeName_.clear();
    finishing_ = false;
    pruneSelection();
    setPrompt(tr("Comando:"));
    emit changed();
}

void CommandEngine::setPrompt(const QString& prompt)
{
    prompt_ = prompt;
    emit promptChanged(prompt_);
}

void CommandEngine::message(const QString& text)
{
    emit messageAdded(text);
}

bool CommandEngine::wantsSelection() const
{
    return !current_ || current_->wantsSelection();
}

bool CommandEngine::wantsObjectPick() const
{
    return current_ && current_->wantsObjectPick();
}

bool CommandEngine::wantsText() const
{
    return current_ && current_->wantsText();
}

void CommandEngine::select(const std::vector<EntityId>& ids, bool remove)
{
    bool changedAny = false;
    for (EntityId id : ids) {
        const Entity* e = doc_.entity(id);
        if (!e || !doc_.isEditable(*e))
            continue;
        changedAny |= remove ? selection_.erase(id) > 0 : selection_.insert(id).second;
    }
    if (changedAny) {
        if (current_)
            message(tr("%n oggetti selezionati", nullptr, static_cast<int>(selection_.size())));
        emit selectionChanged();
        emit changed();
    }
}

void CommandEngine::clearSelection()
{
    if (selection_.empty())
        return;
    selection_.clear();
    emit selectionChanged();
    emit changed();
}

void CommandEngine::eraseSelection()
{
    if (current_ || selection_.empty())
        return;
    doc_.beginTransaction("ERASE");
    for (EntityId id : selection_)
        doc_.remove(id);
    doc_.commitTransaction();
    message(tr("%n oggetti cancellati", nullptr, static_cast<int>(selection_.size())));
    selection_.clear();
    emit selectionChanged();
    emit changed();
}

void CommandEngine::documentChanged()
{
    pruneSelection();
    emit changed();
}

void CommandEngine::pruneSelection()
{
    const auto before = selection_.size();
    std::erase_if(selection_, [&](EntityId id) {
        const Entity* e = doc_.entity(id);
        return !e || !doc_.isEditable(*e);
    });
    if (selection_.size() != before)
        emit selectionChanged();
}

std::optional<EntityId> CommandEngine::pick(Vec2 p, double tolerance) const
{
    std::optional<EntityId> best;
    double bestDist = tolerance;
    for (const auto& [id, e] : doc_.entities()) {
        if (!doc_.isEditable(e) || !boundingBox(e.shape).inflated(tolerance).contains(p))
            continue;
        const double d = distanceTo(e.shape, p);
        if (d <= bestDist) {
            bestDist = d;
            best = id;
        }
    }
    return best;
}

std::vector<EntityId> CommandEngine::pickBox(const BBox& box, bool crossing) const
{
    std::vector<EntityId> out;
    for (const auto& [id, e] : doc_.entities()) {
        if (!doc_.isEditable(e))
            continue;
        const bool hit = crossing ? crossesBox(e.shape, box) : box.contains(boundingBox(e.shape));
        if (hit)
            out.push_back(id);
    }
    return out;
}

void CommandEngine::setOsnapEnabled(bool on)
{
    osnap_ = on;
    message(on ? tr("<Snap ad oggetto attivato>") : tr("<Snap ad oggetto disattivato>"));
    emit settingsChanged();
}

void CommandEngine::setOrthoEnabled(bool on)
{
    ortho_ = on;
    message(on ? tr("<Orto attivato>") : tr("<Orto disattivato>"));
    emit settingsChanged();
}

std::optional<SnapPoint> CommandEngine::findSnap(Vec2 p, double tolerance) const
{
    if (!osnap_ || !current_ || current_->wantsSelection() || current_->wantsObjectPick())
        return std::nullopt;

    // Only entities near the cursor take part; this keeps snapping fast on
    // large drawings without a spatial index (to be added with DXF import).
    std::vector<const Entity*> near;
    for (const auto& [id, e] : doc_.entities()) {
        if (doc_.isVisible(e) && boundingBox(e.shape).inflated(tolerance).contains(p))
            near.push_back(&e);
    }

    std::vector<SnapPoint> candidates;
    for (const Entity* e : near)
        collectSnapPoints(e->shape, osnapMask_, candidates);
    if (osnapMask_ & SnapIntersection) {
        for (size_t i = 0; i < near.size(); ++i)
            for (size_t j = i + 1; j < near.size(); ++j)
                for (Vec2 v : intersections(near[i]->shape, near[j]->shape))
                    candidates.push_back({v, SnapIntersection});
    }

    std::optional<SnapPoint> best;
    double bestDist = tolerance;
    for (const SnapPoint& s : candidates) {
        const double d = distance(s.point, p);
        if (d <= bestDist) {
            bestDist = d;
            best = s;
        }
    }
    if (!best && (osnapMask_ & SnapNearest)) {
        for (const Entity* e : near) {
            const Vec2 q = nearestPoint(e->shape, p);
            const double d = distance(q, p);
            if (d <= bestDist) {
                bestDist = d;
                best = SnapPoint{q, SnapNearest};
            }
        }
    }
    return best;
}

std::optional<Vec2> CommandEngine::basePoint() const
{
    return current_ ? current_->base() : std::nullopt;
}

Vec2 CommandEngine::constrain(Vec2 p) const
{
    const auto b = basePoint();
    if (!ortho_ || !b)
        return p;
    const Vec2 d = p - *b;
    return std::abs(d.x) >= std::abs(d.y) ? Vec2{p.x, b->y} : Vec2{b->x, p.y};
}

void CommandEngine::preview(Vec2 cursor, std::vector<Shape>& out) const
{
    if (current_)
        current_->preview(cursor, out);
}

void CommandEngine::registerCommands()
{
    add(QStringLiteral("LINE"), {QStringLiteral("LINEA"), QStringLiteral("L")}, [] { return makeLineCommand(); });
    add(QStringLiteral("PLINE"), {QStringLiteral("PLINEA"), QStringLiteral("PL")}, [] { return makePolylineCommand(); });
    add(QStringLiteral("CIRCLE"), {QStringLiteral("CERCHIO"), QStringLiteral("C")}, [] { return makeCircleCommand(); });
    add(QStringLiteral("ARC"), {QStringLiteral("ARCO"), QStringLiteral("A")}, [] { return makeArcCommand(); });
    add(QStringLiteral("RECTANG"), {QStringLiteral("RETTANGOLO"), QStringLiteral("REC")}, [] { return makeRectangleCommand(); });
    add(QStringLiteral("ERASE"), {QStringLiteral("CANCELLA"), QStringLiteral("E")}, [] { return makeEraseCommand(); });
    add(QStringLiteral("MOVE"), {QStringLiteral("SPOSTA"), QStringLiteral("M")}, [] { return makeMoveCommand(false); });
    add(QStringLiteral("COPY"), {QStringLiteral("COPIA"), QStringLiteral("CO"), QStringLiteral("CP")}, [] { return makeMoveCommand(true); });
    add(QStringLiteral("ROTATE"), {QStringLiteral("RUOTA"), QStringLiteral("RO")}, [] { return makeRotateCommand(); });
    add(QStringLiteral("SCALE"), {QStringLiteral("SCALA"), QStringLiteral("SC")}, [] { return makeScaleCommand(); });
    add(QStringLiteral("MIRROR"), {QStringLiteral("SPECCHIO"), QStringLiteral("MI")}, [] { return makeMirrorCommand(); });
    add(QStringLiteral("OFFSET"), {QStringLiteral("O")}, [] { return makeOffsetCommand(); });
    add(QStringLiteral("TRIM"), {QStringLiteral("TAGLIA"), QStringLiteral("TR")}, [] { return makeTrimCommand(); });
    add(QStringLiteral("EXTEND"), {QStringLiteral("ESTENDI"), QStringLiteral("EX")}, [] { return makeExtendCommand(); });
    add(QStringLiteral("FILLET"), {QStringLiteral("RACCORDO"), QStringLiteral("RACCORDA"), QStringLiteral("F")}, [] { return makeFilletCommand(); });
    add(QStringLiteral("CHAMFER"), {QStringLiteral("CIMA"), QStringLiteral("CHA")}, [] { return makeChamferCommand(); });
    add(QStringLiteral("STRETCH"), {QStringLiteral("STIRA"), QStringLiteral("S")}, [] { return makeStretchCommand(); });
    add(QStringLiteral("TEXT"), {QStringLiteral("TESTO"), QStringLiteral("DTEXT"), QStringLiteral("DT")}, [] { return makeTextCommand(); });
    add(QStringLiteral("TEXTEDIT"), {QStringLiteral("MODIFICATESTO"), QStringLiteral("DDEDIT"), QStringLiteral("ED")}, [] { return makeTextEditCommand(); });
    add(QStringLiteral("DIMLINEAR"), {QStringLiteral("QUOTALIN"), QStringLiteral("QUOTALINEARE"), QStringLiteral("DLI")}, [] { return makeDimLinearCommand(false); });
    add(QStringLiteral("DIMALIGNED"), {QStringLiteral("QUOTALLINEATA"), QStringLiteral("DAL")}, [] { return makeDimLinearCommand(true); });
    add(QStringLiteral("DIMRADIUS"), {QStringLiteral("QUOTARAGGIO"), QStringLiteral("DRA")}, [] { return makeDimRadialCommand(false); });
    add(QStringLiteral("DIMDIAMETER"), {QStringLiteral("QUOTADIAMETRO"), QStringLiteral("QUOTADIAM"), QStringLiteral("DDI")}, [] { return makeDimRadialCommand(true); });
    add(QStringLiteral("DIMSCALE"), {QStringLiteral("SCALAQUOTE")}, [] { return makeDimScaleCommand(); });
    add(QStringLiteral("HATCH"), {QStringLiteral("TRATTEGGIO"), QStringLiteral("TRATTEGGIA"), QStringLiteral("BHATCH"), QStringLiteral("H"), QStringLiteral("BH")}, [] { return makeHatchCommand(); });
    add(QStringLiteral("HATCHEDIT"), {QStringLiteral("MODIFICATRATTEGGIO"), QStringLiteral("HE")}, [] { return makeHatchEditCommand(); });
    add(QStringLiteral("WALL"), {QStringLiteral("MURO"), QStringLiteral("MU")}, [] { return makeWallCommand(); });
    add(QStringLiteral("DOOR"), {QStringLiteral("PORTA"), QStringLiteral("PO")}, [] { return makeDoorCommand(); });
    add(QStringLiteral("WINDOW"), {QStringLiteral("FINESTRA"), QStringLiteral("FI")}, [] { return makeWindowCommand(); });
    add(QStringLiteral("WALLEDIT"), {QStringLiteral("MODIFICAMURO"), QStringLiteral("MM")}, [] { return makeWallEditCommand(); });
    add(QStringLiteral("STORY"), {QStringLiteral("PIANO"), QStringLiteral("PIANI"), QStringLiteral("PN")}, [] { return makeStoryCommand(); });
    add(QStringLiteral("SLAB"), {QStringLiteral("SOLAIO"), QStringLiteral("SO")}, [] { return makeSlabCommand(); });
    add(QStringLiteral("ROOF"), {QStringLiteral("TETTO"), QStringLiteral("TE")}, [] { return makeRoofCommand(); });
    add(QStringLiteral("VIEW3D"), {QStringLiteral("VISTA3D"), QStringLiteral("3D")}, [] { return makeView3dCommand(); });
    add(QStringLiteral("ZOOM"), {QStringLiteral("Z")}, [] { return makeZoomCommand(); });
    add(QStringLiteral("PLOT"), {QStringLiteral("STAMPA"), QStringLiteral("PRINT")}, [] { return makePlotCommand(); });
    add(QStringLiteral("LAYER"), {QStringLiteral("LA")}, [] { return makeLayerCommand(); });
}

} // namespace cad
