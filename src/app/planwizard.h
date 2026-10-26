// SPDX-License-Identifier: GPL-3.0-or-later
// "Da pianta 2D a 3D": a step-by-step dialog that turns a plan drawn with
// lines into walls, doors and windows, with a live preview. It only gathers
// the choices; the conversion itself is the PIANTA3D command, so it can be
// undone in one step and is the same from a script.
#pragma once

#include "document.h"
#include "planrecognition.h"

#include <QDialog>
#include <QStringList>

#include <set>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class PlanPreview;

class PlanWizard : public QDialog {
    Q_OBJECT

public:
    PlanWizard(const cad::Document& doc, const std::set<cad::EntityId>& selection, QWidget* parent = nullptr);

    // The objects to convert, and what to type after PIANTA3D once they are selected.
    std::vector<cad::EntityId> entities() const;
    QStringList commandInput() const;
    // Commands to run after the walls are made: the slab and the roof, if chosen.
    QStringList followingCommands() const;
    bool showModel() const;

private:
    void goTo(int page);
    void sourceChanged();
    void unitChanged();
    void recognize();
    double unit() const;
    cad::PlanRecognitionOptions options() const;

    const cad::Document& doc_;
    std::set<cad::EntityId> selection_;
    std::vector<cad::Shape> shapes_;
    cad::RecognizedPlan plan_;
    bool updating_ = false;

    QLabel* stepTitle_;
    QStackedWidget* pages_;
    PlanPreview* preview_;
    QLabel* summary_;

    QRadioButton* fromSelection_;
    QRadioButton* fromLayers_;
    QListWidget* layers_;
    QComboBox* unit_;
    QDoubleSpinBox* minThickness_;
    QDoubleSpinBox* maxThickness_;
    QDoubleSpinBox* wallHeight_;
    QDoubleSpinBox* doorHeight_;
    QDoubleSpinBox* windowHeight_;
    QDoubleSpinBox* sill_;
    QLabel* finalText_;
    QCheckBox* slab_;
    QDoubleSpinBox* slabThickness_;
    QCheckBox* roof_;
    QComboBox* roofType_;
    QDoubleSpinBox* roofPitch_;
    QCheckBox* showModel_;

    QPushButton* back_;
    QPushButton* next_;
    QPushButton* convert_;
};
