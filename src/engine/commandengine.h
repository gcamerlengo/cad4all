// SPDX-License-Identifier: GPL-3.0-or-later
// Runs AutoCAD-style commands: receives what the user types and the points
// picked in the view, keeps the selection and the drawing aids (snap, ortho).
#pragma once

#include "document.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace cad {

class Command;

class CommandEngine : public QObject {
    Q_OBJECT

public:
    explicit CommandEngine(Document& doc, QObject* parent = nullptr);
    ~CommandEngine() override;

    Document& document() { return doc_; }
    const Document& document() const { return doc_; }

    // --- Input from the user interface ---
    // Text typed on the command line (Enter or Space pressed).
    void submit(const QString& text);
    // A whole paragraph, possibly of several lines, from the text editor
    // the active command asked for with textEditorRequested().
    void submitParagraph(const QString& text);
    // Starts a command by name or alias (LINE, LINEA, L...).
    bool run(const QString& name);
    // A point picked in the view, already snapped and constrained.
    void pickPoint(Vec2 p);
    // Escape: cancels the command, or clears the selection when idle.
    void cancel();
    // Current cursor position, used for previews and direct distance entry.
    void setCursor(Vec2 p) { cursor_ = p; }

    // Call after changing the document from outside a command (e.g. layer
    // settings), so the selection and the views follow.
    void documentChanged();

    bool isActive() const { return current_ != nullptr; }
    QString prompt() const { return prompt_; }
    QString activeCommandName() const { return activeName_; }

    // --- Selection ---
    // True when a click in the view should select objects rather than pick a point.
    bool wantsSelection() const;
    // True when the next click names a single object (TRIM, OFFSET...): the
    // view shows the pick box and sends the raw point, without snapping.
    bool wantsObjectPick() const;
    // True when the command waits for free text (TEXT): Space is then part
    // of the text and only Enter confirms it.
    bool wantsText() const;
    // Pick box size in drawing units; the view keeps it equal to a few pixels.
    double pickTolerance() const { return pickTolerance_; }
    void setPickTolerance(double tolerance) { pickTolerance_ = tolerance; }
    const std::set<EntityId>& selection() const { return selection_; }
    void select(const std::vector<EntityId>& ids, bool remove = false);
    void clearSelection();
    void eraseSelection();

    // --- Grips ---
    // Like AutoCAD's GRIPOBJLIMIT: more selected objects show no grips.
    static constexpr size_t kMaxGripObjects = 100;
    // The grips of the selected objects, while no command runs.
    std::vector<Vec2> grips() const;
    // Starts dragging the grip within `tolerance` of `p`, if there is one:
    // the next point picked moves it, on every selected object that has it.
    bool startGrip(Vec2 p, double tolerance);
    // Editable entity closest to `p` within `tolerance`, if any.
    std::optional<EntityId> pick(Vec2 p, double tolerance) const;
    std::vector<EntityId> pickBox(const BBox& box, bool crossing) const;

    // --- Drawing aids ---
    bool osnapEnabled() const { return osnap_; }
    void setOsnapEnabled(bool on);
    unsigned osnapMask() const { return osnapMask_; }
    void setOsnapMask(unsigned mask) { osnapMask_ = mask; }
    bool orthoEnabled() const { return ortho_; }
    void setOrthoEnabled(bool on);
    std::optional<SnapPoint> findSnap(Vec2 p, double tolerance) const;
    // Applies ortho relative to the rubber-band base point, if any.
    Vec2 constrain(Vec2 p) const;
    std::optional<Vec2> basePoint() const;
    std::optional<Vec2> lastPoint() const { return lastPoint_; }

    // Temporary shapes the active command wants drawn at `cursor`.
    void preview(Vec2 cursor, std::vector<Shape>& out) const;

    // List of command names, for help and completion.
    QStringList commandNames() const;

    // --- Used by commands ---
    void setPrompt(const QString& prompt);
    void message(const QString& text);
    // Ends the active command; safe to call from inside the command.
    void finish();
    void requestZoomExtents() { emit zoomExtentsRequested(); }
    void requestLayerManager() { emit layerManagerRequested(); }
    void requestLayouts() { emit layoutsRequested(); }
    void requestPlot() { emit plotRequested(); }
    void requestView3d() { emit view3dRequested(); }
    // Asks the interface for a text editor showing `text`; without one the
    // lines are typed on the command line.
    void requestTextEditor(const QString& title, const QString& text) { emit textEditorRequested(title, text); }

signals:
    void promptChanged(const QString& prompt);
    void messageAdded(const QString& text);
    void changed();
    void selectionChanged();
    void zoomExtentsRequested();
    void layerManagerRequested();
    void layoutsRequested();
    void plotRequested();
    void view3dRequested();
    void textEditorRequested(const QString& title, const QString& text);
    void settingsChanged();

private:
    struct Registration {
        QString name;
        std::function<std::unique_ptr<Command>()> factory;
    };

    void registerCommands();
    void start(const QString& name, std::unique_ptr<Command> command);
    void add(const QString& name, const QStringList& aliases, std::function<std::unique_ptr<Command>()> factory);
    void dispatch(const std::function<void(Command&)>& call);
    void endCommand();
    void pruneSelection();
    // TUTTO and ULTIMO at a selection prompt.
    bool selectByWord(const QString& word);

    Document& doc_;
    std::map<QString, Registration> registry_;  // upper-case name or alias -> command
    QStringList names_;

    std::unique_ptr<Command> current_;
    QString activeName_;
    QString lastCommand_;
    QString prompt_;
    bool finishing_ = false;

    std::set<EntityId> selection_;
    double pickTolerance_ = 1.0;
    std::optional<Vec2> lastPoint_;
    Vec2 cursor_;

    bool osnap_ = true;
    unsigned osnapMask_ = SnapEndpoint | SnapMidpoint | SnapCenter | SnapQuadrant | SnapIntersection | SnapInsertion | SnapNode;
    bool ortho_ = false;
};

// Base class of all commands. A command reacts to points, numbers, keywords
// and Enter, and calls engine->finish() when it is done.
class Command {
public:
    virtual ~Command() = default;

    virtual void start() = 0;
    virtual bool point(Vec2) { return false; }
    virtual bool number(double) { return false; }
    // `word` is upper case. Return false if it is not a valid option.
    virtual bool keyword(const QString&) { return false; }
    // Enter or Space with nothing typed.
    virtual void enter() { engine->finish(); }
    virtual bool wantsSelection() const { return false; }
    virtual bool wantsObjectPick() const { return false; }
    virtual bool wantsText() const { return false; }
    // A line of free text, when wantsText() is true. Empty text goes to enter().
    virtual void text(const QString&) {}
    // Text of one or more lines from the text editor.
    virtual void paragraph(const QString& s) { text(s); }
    virtual void preview(Vec2, std::vector<Shape>&) const {}
    // Point the rubber band starts from (also used by ortho).
    virtual std::optional<Vec2> base() const { return std::nullopt; }

    CommandEngine* engine = nullptr;
};

} // namespace cad
