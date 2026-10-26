// SPDX-License-Identifier: GPL-3.0-or-later
#include "planwizard.h"

#include "drawingpainter.h"
#include "properties.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace cad;

// The plan in grey and the walls found in it in the accent colour.
class PlanPreview : public QWidget {
public:
    explicit PlanPreview(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(360, 300); }

    void setPlan(const std::vector<Shape>* shapes, const RecognizedPlan* plan)
    {
        shapes_ = shapes;
        plan_ = plan;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        if (!shapes_ || shapes_->empty()) {
            p.setPen(palette().placeholderText().color());
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("Nessuna linea da convertire"));
            return;
        }
        BBox box;
        for (const Shape& s : *shapes_)
            box.add(boundingBox(s));
        const double w = std::max(box.width(), 1e-9);
        const double h = std::max(box.height(), 1e-9);
        const double scale = std::min((width() - 24) / w, (height() - 24) / h);
        DrawingPainter painter(p, box.center(), scale, QPointF(width() / 2.0, height() / 2.0));

        QColor grey = palette().text().color();
        grey.setAlpha(150);
        p.setPen(QPen(grey, 0));
        for (const Shape& s : *shapes_)
            painter.drawShape(s);
        if (!plan_)
            return;
        p.setPen(QPen(theme::accent(), 1.6));
        const std::vector<WallFaces> faces = joinWalls(plan_->walls);
        for (size_t i = 0; i < plan_->walls.size(); ++i)
            painter.drawWall(plan_->walls[i], faces[i]);
    }

private:
    const std::vector<Shape>* shapes_ = nullptr;
    const RecognizedPlan* plan_ = nullptr;
};

namespace {

bool isPlanShape(const Shape& s)
{
    return std::holds_alternative<Line>(s) || std::holds_alternative<Polyline>(s) || std::holds_alternative<Arc>(s)
        || std::holds_alternative<Insert>(s);
}

QDoubleSpinBox* lengthBox(QWidget* parent)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(0.001, 1e7);
    box->setDecimals(3);
    box->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    return box;
}

QLabel* hint(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

PlanWizard::PlanWizard(const Document& doc, const std::set<EntityId>& selection, QWidget* parent)
    : QDialog(parent), doc_(doc), selection_(selection)
{
    setWindowTitle(tr("Da pianta 2D a 3D"));

    stepTitle_ = new QLabel(this);
    QFont titleFont = stepTitle_->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    titleFont.setBold(true);
    stepTitle_->setFont(titleFont);
    pages_ = new QStackedWidget(this);
    preview_ = new PlanPreview(this);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);

    // 1. What to convert.
    auto* source = new QWidget(this);
    auto* sourceLayout = new QVBoxLayout(source);
    sourceLayout->addWidget(hint(tr("I muri vanno disegnati con due linee parallele, le porte con l'arco di apertura e le finestre "
                                    "con le linee del vetro. Scegliere le linee della pianta, senza arredi e quote se possibile."),
                                 source));
    fromSelection_ = new QRadioButton(tr("Gli oggetti selezionati (%n)", nullptr, static_cast<int>(selection_.size())), source);
    fromLayers_ = new QRadioButton(tr("Gli oggetti di questi layer, sul piano corrente:"), source);
    layers_ = new QListWidget(source);
    sourceLayout->addWidget(fromSelection_);
    sourceLayout->addWidget(fromLayers_);
    sourceLayout->addWidget(layers_, 1);
    auto* unitForm = new QFormLayout;
    unit_ = new QComboBox(source);
    unit_->addItem(tr("Millimetri"), 1.0);
    unit_->addItem(tr("Centimetri"), 10.0);
    unit_->addItem(tr("Metri"), 1000.0);
    unitForm->addRow(tr("Unità del disegno:"), unit_);
    sourceLayout->addLayout(unitForm);
    pages_->addWidget(source);

    // Layers with lines on the current story, all checked but those named
    // like furniture, dimensions, texts or hatches.
    std::set<std::string> withLines;
    for (const auto& [id, e] : doc.entities())
        if (doc.isVisible(e) && isPlanShape(e.shape))
            withLines.insert(e.layer);
    const QRegularExpression notPlan(QStringLiteral("arred|mobil|furn|sanitar|quot|dim|test|text|tratteg|retin|campit|hatch|"
                                                    "cartig|title|assi|axis|griglia|grid|defpoints|vegeta|albe|auto"),
                                     QRegularExpression::CaseInsensitiveOption);
    for (const std::string& name : withLines) {
        auto* item = new QListWidgetItem(QString::fromStdString(name), layers_);
        item->setCheckState(item->text().contains(notPlan) ? Qt::Unchecked : Qt::Checked);
    }
    fromSelection_->setEnabled(!selection_.empty());
    (selection_.empty() ? fromLayers_ : fromSelection_)->setChecked(true);
    layers_->setEnabled(fromLayers_->isChecked());

    // 2. Walls.
    auto* walls = new QWidget(this);
    auto* wallForm = new QFormLayout(walls);
    minThickness_ = lengthBox(walls);
    maxThickness_ = lengthBox(walls);
    wallHeight_ = lengthBox(walls);
    wallForm->addRow(hint(tr("Due linee parallele a una distanza compresa tra questi spessori diventano un muro."), walls));
    wallForm->addRow(tr("Spessore minimo:"), minThickness_);
    wallForm->addRow(tr("Spessore massimo:"), maxThickness_);
    wallForm->addRow(tr("Altezza dei muri:"), wallHeight_);
    pages_->addWidget(walls);

    // 3. Doors and windows.
    auto* openings = new QWidget(this);
    auto* openingForm = new QFormLayout(openings);
    doorHeight_ = lengthBox(openings);
    windowHeight_ = lengthBox(openings);
    sill_ = lengthBox(openings);
    sill_->setMinimum(0.0);
    openingForm->addRow(hint(tr("Larghezza, posizione e verso di apertura vengono dalla pianta; le altezze si scelgono qui. "
                                "Un'interruzione del muro senza arco né vetro resta un passaggio libero."),
                             openings));
    openingForm->addRow(tr("Altezza delle porte:"), doorHeight_);
    openingForm->addRow(tr("Altezza delle finestre:"), windowHeight_);
    openingForm->addRow(tr("Altezza del davanzale:"), sill_);
    pages_->addWidget(openings);

    // 4. Confirm.
    auto* confirm = new QWidget(this);
    auto* confirmLayout = new QVBoxLayout(confirm);
    finalText_ = new QLabel(confirm);
    finalText_->setWordWrap(true);
    auto* extras = new QFormLayout;
    slab_ = new QCheckBox(tr("Solaio sotto tutti i muri, spessore:"), confirm);
    slab_->setChecked(true);
    slabThickness_ = lengthBox(confirm);
    extras->addRow(slab_, slabThickness_);
    roof_ = new QCheckBox(tr("Tetto sul contorno dei muri:"), confirm);
    roofType_ = new QComboBox(confirm);
    roofType_->addItem(tr("A padiglione"), QStringLiteral("P"));
    roofType_->addItem(tr("A capanna"), QStringLiteral("C"));
    roofType_->addItem(tr("A una falda"), QStringLiteral("F"));
    roofType_->addItem(tr("Piano"), QStringLiteral("PI"));
    extras->addRow(roof_, roofType_);
    roofPitch_ = new QDoubleSpinBox(confirm);
    roofPitch_->setRange(0.0, 75.0);
    roofPitch_->setSuffix(QStringLiteral("°"));
    roofPitch_->setValue(toolSettings().roofPitch);
    extras->addRow(tr("Pendenza del tetto:"), roofPitch_);
    roofType_->setEnabled(false);
    roofPitch_->setEnabled(false);
    connect(slab_, &QCheckBox::toggled, slabThickness_, &QWidget::setEnabled);
    connect(roof_, &QCheckBox::toggled, roofType_, &QWidget::setEnabled);
    connect(roof_, &QCheckBox::toggled, roofPitch_, &QWidget::setEnabled);
    showModel_ = new QCheckBox(tr("Mostrare il modello 3D alla fine"), confirm);
    showModel_->setChecked(true);
    confirmLayout->addWidget(finalText_);
    confirmLayout->addLayout(extras);
    confirmLayout->addWidget(showModel_);
    confirmLayout->addStretch();
    pages_->addWidget(confirm);

    back_ = new QPushButton(tr("< Indietro"), this);
    next_ = new QPushButton(tr("Avanti >"), this);
    convert_ = new QPushButton(tr("Converti"), this);
    auto* cancel = new QPushButton(tr("Annulla"), this);
    connect(back_, &QPushButton::clicked, this, [this] { goTo(pages_->currentIndex() - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { goTo(pages_->currentIndex() + 1); });
    connect(convert_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    auto* left = new QVBoxLayout;
    left->addWidget(stepTitle_);
    left->addWidget(pages_, 1);
    left->addWidget(summary_);
    auto* body = new QHBoxLayout;
    body->addLayout(left, 2);
    body->addWidget(preview_, 3);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(back_);
    buttons->addWidget(next_);
    buttons->addWidget(convert_);
    buttons->addWidget(cancel);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addLayout(buttons);
    resize(900, 560);

    connect(fromSelection_, &QRadioButton::toggled, this, [this] { sourceChanged(); });
    connect(layers_, &QListWidget::itemChanged, this, [this] { sourceChanged(); });
    connect(unit_, &QComboBox::currentIndexChanged, this, [this] { unitChanged(); });
    for (QDoubleSpinBox* box : {minThickness_, maxThickness_, wallHeight_, doorHeight_, windowHeight_, sill_})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { recognize(); });

    sourceChanged();
    goTo(0);
}

std::vector<EntityId> PlanWizard::entities() const
{
    std::vector<EntityId> out;
    std::set<std::string> layers;
    for (int i = 0; i < layers_->count(); ++i)
        if (layers_->item(i)->checkState() == Qt::Checked)
            layers.insert(layers_->item(i)->text().toStdString());
    for (const auto& [id, e] : doc_.entities()) {
        if (!doc_.isVisible(e) || !isPlanShape(e.shape))
            continue;
        if (fromSelection_->isChecked() ? selection_.count(id) > 0 : layers.count(e.layer) > 0)
            out.push_back(id);
    }
    return out;
}

double PlanWizard::unit() const { return unit_->currentData().toDouble(); }

PlanRecognitionOptions PlanWizard::options() const
{
    PlanRecognitionOptions o;
    o.minThickness = minThickness_->value();
    o.maxThickness = std::max(maxThickness_->value(), o.minThickness);
    o.wallHeight = wallHeight_->value();
    o.door.height = doorHeight_->value();
    o.window.height = windowHeight_->value();
    o.window.sill = sill_->value();
    // Openings and the widths of doors and windows come from the plan.
    const PlanRecognitionOptions inUnit = PlanRecognitionOptions{}.inUnits(unit());
    o.minOpening = inUnit.minOpening;
    o.maxOpening = inUnit.maxOpening;
    return o;
}

QStringList PlanWizard::commandInput() const
{
    const PlanRecognitionOptions o = options();
    auto n = [](double v) { return QString::number(v, 'g', 12); };
    const QString unitWord = unit() >= 1000.0 ? QStringLiteral("ME") : unit() >= 10.0 ? QStringLiteral("C") : QStringLiteral("MI");
    return {QStringLiteral("U"), unitWord,
            QStringLiteral("S"), n(o.minThickness), n(o.maxThickness),
            QStringLiteral("A"), n(o.wallHeight),
            QStringLiteral("P"), n(o.door.height),
            QStringLiteral("F"), n(o.window.height), n(o.window.sill),
            QString()};
}

QStringList PlanWizard::followingCommands() const
{
    auto n = [](double v) { return QString::number(v, 'g', 12); };
    QStringList out;
    if (slab_->isChecked())
        out << QStringLiteral("SOLAIO") << QStringLiteral("S") << n(slabThickness_->value()) << QStringLiteral("M") << QString();
    if (roof_->isChecked()) {
        // Overhang and thickness from the roof tool, in the unit of the drawing.
        const ToolSettings& tools = toolSettings();
        out << QStringLiteral("TETTO") << QStringLiteral("T") << roofType_->currentData().toString() << QStringLiteral("P")
            << n(roofPitch_->value()) << QStringLiteral("S") << n(tools.roofOverhang / unit()) << QStringLiteral("SP")
            << n(tools.roofThickness / unit()) << QStringLiteral("M") << QString();
    }
    return out;
}

bool PlanWizard::showModel() const { return showModel_->isChecked(); }

void PlanWizard::goTo(int page)
{
    page = std::clamp(page, 0, pages_->count() - 1);
    pages_->setCurrentIndex(page);
    static const char* titles[] = {QT_TR_NOOP("Che cosa convertire"), QT_TR_NOOP("Muri"), QT_TR_NOOP("Porte e finestre"),
                                   QT_TR_NOOP("Conferma")};
    stepTitle_->setText(tr("Passo %1 di %2: %3").arg(page + 1).arg(pages_->count()).arg(tr(titles[page])));
    back_->setEnabled(page > 0);
    next_->setVisible(page < pages_->count() - 1);
    convert_->setVisible(page == pages_->count() - 1);
    convert_->setDefault(page == pages_->count() - 1);
    next_->setDefault(page < pages_->count() - 1);
}

void PlanWizard::sourceChanged()
{
    layers_->setEnabled(fromLayers_->isChecked());
    shapes_.clear();
    for (EntityId id : entities())
        if (const Entity* e = doc_.entity(id))
            doc_.forEachPart(*e, [&](const Entity& part) { shapes_.push_back(part.shape); });
    const double guess = guessPlanUnit(shapes_);
    updating_ = true;
    unit_->setCurrentIndex(std::max(0, unit_->findData(guess)));
    updating_ = false;
    unitChanged();
}

// The defaults in millimetres (heights from the wall, door and window tools)
// in the unit of the drawing.
void PlanWizard::unitChanged()
{
    if (updating_)
        return;
    PlanRecognitionOptions base;
    const ToolSettings& tools = toolSettings();
    base.wallHeight = tools.wallHeight;
    base.door = tools.door;
    base.window = tools.window;
    const PlanRecognitionOptions o = base.inUnits(unit());
    updating_ = true;
    minThickness_->setValue(o.minThickness);
    maxThickness_->setValue(o.maxThickness);
    wallHeight_->setValue(o.wallHeight);
    slabThickness_->setValue(toolSettings().slabThickness / unit());
    doorHeight_->setValue(o.door.height);
    windowHeight_->setValue(o.window.height);
    sill_->setValue(o.window.sill);
    updating_ = false;
    recognize();
}

void PlanWizard::recognize()
{
    if (updating_)
        return;
    plan_ = recognizePlan(shapes_, options());
    preview_->setPlan(&shapes_, &plan_);
    const QString found = tr("Trovati %n muri", nullptr, static_cast<int>(plan_.walls.size())) + QStringLiteral(", ")
                        + tr("%n porte", nullptr, plan_.doors) + QStringLiteral(", ") + tr("%n finestre", nullptr, plan_.windows);
    summary_->setText(found + QStringLiteral("."));
    const Story* story = doc_.story(doc_.currentStory());
    const QString storyName = story ? QString::fromStdString(story->name) : QString();
    finalText_->setText(plan_.walls.empty()
                            ? tr("Nessun muro trovato. Tornare indietro e controllare le linee scelte, l'unità del disegno e gli spessori.")
                            : tr("%1 sul piano \"%2\". Le linee usate passano sul layer spento \"Pianta 2D originale\", "
                                 "quindi nulla va perso; con Annulla si torna indietro un passo alla volta.")
                                  .arg(found, storyName));
    convert_->setEnabled(!plan_.walls.empty());
}
