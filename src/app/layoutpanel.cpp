// SPDX-License-Identifier: GPL-3.0-or-later
#include "layoutpanel.h"

#include "plot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

using namespace cad;

namespace {

QString scaleLabel(double n)
{
    return QStringLiteral("1:%1").arg(QString::fromStdString(formatMeasurement(n)));
}

// "1:50", "50" or "Adatta" (0).
double parseScale(const QString& raw, bool* ok)
{
    const QString text = raw.trimmed();
    *ok = true;
    if (text.isEmpty() || text.compare(QObject::tr("Adatta"), Qt::CaseInsensitive) == 0)
        return 0.0;
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    const double a = colon > 0 ? text.left(colon).toDouble(ok) : 1.0;
    bool okB = true;
    const double b = (colon > 0 ? text.mid(colon + 1) : text).toDouble(&okB);
    *ok = *ok && okB && a > 0.0 && b > 0.0;
    return *ok ? b / a : 0.0;
}

} // namespace

LayoutPreview::LayoutPreview(const Document& doc, QWidget* parent)
    : QWidget(parent), doc_(doc)
{
    setMinimumSize(160, 120);
}

void LayoutPreview::setLayout(const Layout* layout)
{
    name_ = layout ? layout->name : std::string();
    update();
}

void LayoutPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(128, 128, 128));
    const Layout* layout = doc_.layout(name_);
    if (!layout)
        return;
    const Vec2 size = layout->size();
    const double k = std::min((width() - 16.0) / size.x, (height() - 16.0) / size.y);
    if (k <= 0.0)
        return;
    const QRectF paper((width() - size.x * k) / 2.0, (height() - size.y * k) / 2.0, size.x * k, size.y * k);
    p.fillRect(paper.translated(3, 3), QColor(80, 80, 80));
    p.fillRect(paper, Qt::white);
    p.translate(paper.topLeft());
    paintLayout(p, doc_, *layout, k, true);
}

LayoutPanel::LayoutPanel(CommandEngine& engine, std::function<BBox()> visibleArea, QWidget* parent)
    : QWidget(parent), engine_(engine), visibleArea_(std::move(visibleArea))
{
    list_ = new QListWidget(this);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setToolTip(tr("Doppio clic su una tavola per modificarla."));
    preview_ = new LayoutPreview(engine_.document(), this);

    auto* add = new QPushButton(tr("Nuova"), this);
    edit_ = new QPushButton(tr("Modifica"), this);
    remove_ = new QPushButton(tr("Elimina"), this);
    export_ = new QPushButton(tr("Esporta PDF"), this);
    print_ = new QPushButton(tr("Stampa"), this);
    export_->setToolTip(tr("Tutte le tavole in un PDF, una per pagina"));

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    for (QPushButton* b : {add, edit_, remove_, export_, print_}) {
        b->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(b);
    }
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(buttons);
    layout->addWidget(list_, 1);
    layout->addWidget(preview_, 2);

    connect(add, &QPushButton::clicked, this, &LayoutPanel::createLayout);
    connect(edit_, &QPushButton::clicked, this, &LayoutPanel::editLayout);
    connect(remove_, &QPushButton::clicked, this, &LayoutPanel::removeLayout);
    connect(export_, &QPushButton::clicked, this, &LayoutPanel::exportPdf);
    connect(print_, &QPushButton::clicked, this, &LayoutPanel::print);
    connect(list_, &QListWidget::itemDoubleClicked, this, &LayoutPanel::editLayout);
    connect(list_, &QListWidget::currentRowChanged, this, [this] {
        preview_->setLayout(engine_.document().layout(currentName().toStdString()));
        updateButtons();
    });
    connect(&engine_, &CommandEngine::changed, this, [this] {
        if (engine_.document().revision() != shownRevision_)
            refresh();
    });
    refresh();
}

QString LayoutPanel::currentName() const
{
    return list_->currentItem() ? list_->currentItem()->text() : QString();
}

void LayoutPanel::refresh()
{
    const Document& doc = engine_.document();
    shownRevision_ = doc.revision();
    QString selected = currentName();
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        for (const Layout& l : doc.layouts()) {
            auto* item = new QListWidgetItem(QString::fromStdString(l.name), list_);
            QString tip = QStringLiteral("%1 %2").arg(QString::fromStdString(l.paper), l.landscape ? tr("orizzontale") : tr("verticale"));
            if (!l.viewports.empty())
                tip += QStringLiteral(", ") + scaleLabel(l.viewports.front().scale);
            item->setToolTip(tip);
        }
    }
    if (!doc.layout(selected.toStdString()) && !doc.layouts().empty())
        selected = QString::fromStdString(doc.layouts().back().name);
    select(selected);
    preview_->setLayout(doc.layout(currentName().toStdString()));
    updateButtons();
}

void LayoutPanel::select(const QString& name)
{
    const QList<QListWidgetItem*> items = list_->findItems(name, Qt::MatchExactly);
    if (!items.isEmpty())
        list_->setCurrentItem(items.front());
}

void LayoutPanel::updateButtons()
{
    const bool any = !engine_.document().layouts().empty();
    const bool current = list_->currentItem() != nullptr;
    edit_->setEnabled(current);
    remove_->setEnabled(current);
    export_->setEnabled(any);
    print_->setEnabled(any);
}

bool LayoutPanel::editDialog(Layout& layout, bool isNew)
{
    const Document& doc = engine_.document();
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("Nuova tavola") : tr("Modifica tavola"));

    auto* name = new QLineEdit(QString::fromStdString(layout.name), &dialog);
    auto* paper = new QComboBox(&dialog);
    for (const PaperSize& s : paperSizes())
        paper->addItem(QStringLiteral("%1 (%2 × %3 mm)").arg(QString::fromStdString(s.name)).arg(s.width).arg(s.height),
                       QString::fromStdString(s.name));
    paper->setCurrentIndex(std::max(0, paper->findData(QString::fromStdString(layout.paper))));
    auto* orientation = new QComboBox(&dialog);
    orientation->addItems({tr("Orizzontale"), tr("Verticale")});
    orientation->setCurrentIndex(layout.landscape ? 0 : 1);

    auto* area = new QComboBox(&dialog);
    if (!isNew)
        area->addItem(tr("Invariata"), 0);
    area->addItem(tr("Estensioni del disegno"), 1);
    area->addItem(tr("Vista corrente"), 2);
    auto* scale = new QComboBox(&dialog);
    scale->setEditable(true);
    scale->addItem(tr("Adatta"));
    for (double n : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0})
        scale->addItem(scaleLabel(n));
    const Viewport* view = layout.viewports.empty() ? nullptr : &layout.viewports.front();
    scale->setCurrentText(isNew || !view ? tr("Adatta") : scaleLabel(view->scale));
    scale->setToolTip(tr("1:N, per esempio 1:50, oppure Adatta per farci stare l'area"));
    auto* unit = new QComboBox(&dialog);
    unit->addItem(tr("Millimetri"), 1.0);
    unit->addItem(tr("Centimetri"), 10.0);
    unit->addItem(tr("Metri"), 1000.0);
    unit->setCurrentIndex(std::max(0, unit->findData(layout.unitMm)));
    unit->setToolTip(tr("Unità in cui è disegnato il modello"));
    auto* story = new QComboBox(&dialog);
    story->addItem(tr("Piano corrente"), 0);
    for (const Story& s : doc.stories())
        story->addItem(QString::fromStdString(s.name), s.id);
    story->setCurrentIndex(std::max(0, story->findData(view ? view->story : 0)));

    auto* titleBlock = new QGroupBox(tr("Cartiglio"), &dialog);
    titleBlock->setCheckable(true);
    titleBlock->setChecked(layout.hasTitleBlock);
    const TitleBlock& tb = layout.titleBlock;
    auto* title = new QLineEdit(QString::fromStdString(tb.title), titleBlock);
    title->setPlaceholderText(tr("Nome della tavola"));
    auto* project = new QLineEdit(QString::fromStdString(tb.project), titleBlock);
    auto* number = new QLineEdit(QString::fromStdString(tb.number), titleBlock);
    auto* date = new QLineEdit(QString::fromStdString(tb.date), titleBlock);
    auto* author = new QLineEdit(QString::fromStdString(tb.author), titleBlock);
    auto* tbForm = new QFormLayout(titleBlock);
    tbForm->addRow(tr("Titolo:"), title);
    tbForm->addRow(tr("Progetto:"), project);
    tbForm->addRow(tr("Tavola n.:"), number);
    tbForm->addRow(tr("Data:"), date);
    tbForm->addRow(tr("Disegnatore:"), author);

    auto* form = new QFormLayout;
    form->addRow(tr("Nome:"), name);
    form->addRow(tr("Formato:"), paper);
    form->addRow(tr("Orientamento:"), orientation);
    form->addRow(tr("Area da stampare:"), area);
    form->addRow(tr("Scala:"), scale);
    form->addRow(tr("Unità del disegno:"), unit);
    form->addRow(tr("Piano:"), story);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* layoutBox = new QVBoxLayout(&dialog);
    layoutBox->addLayout(form);
    layoutBox->addWidget(titleBlock);
    layoutBox->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        const std::string n = name->text().trimmed().toStdString();
        const Layout* other = doc.layout(n);
        bool ok = false;
        parseScale(scale->currentText(), &ok);
        if (n.empty() || (other && (isNew || other->name != layout.name)))
            QMessageBox::warning(&dialog, dialog.windowTitle(), tr("Il nome manca o è già usato da un'altra tavola."));
        else if (!ok)
            QMessageBox::warning(&dialog, dialog.windowTitle(), tr("Scala non valida: scrivere per esempio 1:50."));
        else
            dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return false;

    // The area to show: the one shown so far, the whole drawing or the view.
    BBox shown;
    const int areaChoice = area->currentData().toInt();
    if (areaChoice == 0 && view)
        shown = viewportArea(*view, layout.unitMm);
    else if (areaChoice == 2 && visibleArea_)
        shown = visibleArea_();
    else
        shown = doc.extents();
    bool ok = false;
    const double n = parseScale(scale->currentText(), &ok);

    layout.name = name->text().trimmed().toStdString();
    layout.paper = paper->currentData().toString().toStdString();
    layout.landscape = orientation->currentIndex() == 0;
    layout.unitMm = unit->currentData().toDouble();
    layout.hasTitleBlock = titleBlock->isChecked();
    layout.titleBlock = {title->text().toStdString(), project->text().toStdString(), number->text().toStdString(),
                         date->text().toStdString(), author->text().toStdString()};
    fitViewport(layout, shown, n, story->currentData().toInt());
    return true;
}

void LayoutPanel::createLayout()
{
    Document& doc = engine_.document();
    Layout layout;
    for (int i = 1;; ++i) {
        layout.name = tr("Tavola %1").arg(i).toStdString();
        if (!doc.layout(layout.name))
            break;
    }
    const BBox extents = doc.extents();
    layout.landscape = !extents.valid() || extents.width() >= extents.height();
    layout.titleBlock.number = std::to_string(doc.layouts().size() + 1);
    layout.titleBlock.date = QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")).toStdString();
    if (!editDialog(layout, true))
        return;
    engine_.cancel();
    doc.addLayout(layout);
    emit message(tr("Tavola %1 creata.").arg(QString::fromStdString(layout.name)));
    engine_.documentChanged();
    select(QString::fromStdString(layout.name));
}

void LayoutPanel::editLayout()
{
    Document& doc = engine_.document();
    const std::string old = currentName().toStdString();
    const Layout* current = doc.layout(old);
    if (!current)
        return;
    Layout layout = *current;
    if (!editDialog(layout, false))
        return;
    engine_.cancel();
    doc.updateLayout(old, layout);
    engine_.documentChanged();
    select(QString::fromStdString(layout.name));
}

void LayoutPanel::removeLayout()
{
    const QString name = currentName();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Elimina tavola"), tr("Eliminare la tavola %1?").arg(name)) != QMessageBox::Yes)
        return;
    engine_.cancel();
    engine_.document().removeLayout(name.toStdString());
    emit message(tr("Tavola %1 eliminata.").arg(name));
    engine_.documentChanged();
}

void LayoutPanel::exportPdf()
{
    const Document& doc = engine_.document();
    QString path = QFileDialog::getSaveFileName(this, tr("Esporta tavole in PDF"), QString(), tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");
    QString error;
    if (writeLayoutsPdf(path, doc, doc.layouts(), true, &error))
        emit message(tr("%n tavole salvate in %1.", nullptr, int(doc.layouts().size())).arg(path));
    else
        QMessageBox::warning(this, tr("Esporta tavole"), tr("Impossibile salvare %1: %2").arg(path, error));
}

void LayoutPanel::print()
{
    const Document& doc = engine_.document();
    if (doc.layouts().empty())
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    printer.setPageLayout(layoutPageLayout(doc.layouts().front()));
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Stampa tavole"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    QPainter p;
    bool first = true;
    for (const Layout& l : doc.layouts()) {
        printer.setPageLayout(layoutPageLayout(l));
        if (first) {
            if (!p.begin(&printer)) {
                QMessageBox::warning(this, tr("Stampa tavole"), tr("Impossibile avviare la stampa."));
                return;
            }
            first = false;
        } else {
            printer.newPage();
        }
        paintLayout(p, doc, l, printer.resolution() / 25.4, true);
    }
    p.end();
    emit message(tr("Tavole inviate alla stampante."));
}
