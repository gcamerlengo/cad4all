// SPDX-License-Identifier: GPL-3.0-or-later
#include "plotdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>

using namespace cad;

namespace {

// The last choices, kept for the next print in this session.
struct Remembered {
    QString destination;
    int paper = 1;  // A3
    int orientation = 0;
    int area = 0;
    QString scale;
    int units = 0;
    int lineWidth = 2;
    bool monochrome = true;
};
Remembered gLast;

const QList<std::pair<QString, QPageSize::PageSizeId>>& papers()
{
    static const QList<std::pair<QString, QPageSize::PageSizeId>> list = {
        {QStringLiteral("A4"), QPageSize::A4}, {QStringLiteral("A3"), QPageSize::A3},
        {QStringLiteral("A2"), QPageSize::A2}, {QStringLiteral("A1"), QPageSize::A1},
        {QStringLiteral("A0"), QPageSize::A0}, {QStringLiteral("Letter"), QPageSize::Letter},
    };
    return list;
}

const double kUnitsMm[] = {1.0, 10.0, 1000.0};
const double kLineWidths[] = {0.13, 0.18, 0.25, 0.35, 0.5, 0.7};

// "1:50", "50" or "Adatta" -> 50 or 0.
double parseScale(QString text)
{
    text = text.trimmed();
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const double a = text.left(colon).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        const double b = text.mid(colon + 1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        return a > 0.0 && b > 0.0 ? b / a : 0.0;
    }
    const double n = text.replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    return n > 0.0 ? n : 0.0;
}

} // namespace

PlotDialog::PlotDialog(const Document& doc, const BBox& extents, const BBox& view, const QString& documentName,
                       QWidget* parent)
    : QDialog(parent), doc_(doc), extents_(extents), view_(view), documentName_(documentName)
{
    setWindowTitle(tr("Stampa"));

    destination_ = new QComboBox(this);
    destination_->addItem(tr("File PDF"));
    const QString defaultPrinter = QPrinterInfo::defaultPrinterName();
    for (const QString& name : QPrinterInfo::availablePrinterNames())
        destination_->addItem(name == defaultPrinter ? tr("%1 (predefinita)").arg(name) : name, name);

    paper_ = new QComboBox(this);
    for (const auto& [name, id] : papers())
        paper_->addItem(name);
    orientation_ = new QComboBox(this);
    orientation_->addItems({tr("Automatico"), tr("Verticale"), tr("Orizzontale")});
    area_ = new QComboBox(this);
    area_->addItems({tr("Estensioni"), tr("Vista corrente")});
    scale_ = new QComboBox(this);
    scale_->setEditable(true);
    scale_->addItems({tr("Adatta al foglio"), QStringLiteral("1:1"), QStringLiteral("1:2"), QStringLiteral("1:5"),
                      QStringLiteral("1:10"), QStringLiteral("1:20"), QStringLiteral("1:25"), QStringLiteral("1:50"),
                      QStringLiteral("1:100"), QStringLiteral("1:200"), QStringLiteral("1:500"), QStringLiteral("1:1000")});
    units_ = new QComboBox(this);
    units_->addItems({tr("millimetri"), tr("centimetri"), tr("metri")});
    lineWidth_ = new QComboBox(this);
    for (double w : kLineWidths)
        lineWidth_->addItem(tr("%1 mm").arg(QLocale().toString(w)));
    monochrome_ = new QCheckBox(tr("Bianco e nero"), this);
    info_ = new QLabel(this);
    info_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Stampante:"), destination_);
    form->addRow(tr("Formato:"), paper_);
    form->addRow(tr("Orientamento:"), orientation_);
    form->addRow(tr("Area di stampa:"), area_);
    form->addRow(tr("Scala:"), scale_);
    form->addRow(tr("Unità del disegno:"), units_);
    form->addRow(tr("Spessore linee:"), lineWidth_);
    form->addRow(QString(), monochrome_);
    form->addRow(info_);

    preview_ = new QLabel(this);
    preview_->setFixedSize(300, 300);
    preview_->setAlignment(Qt::AlignCenter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annulla"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto updateButton = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setText(destination_->currentIndex() == 0 ? tr("Salva PDF...") : tr("Stampa"));
    };

    auto* top = new QHBoxLayout;
    top->addLayout(form);
    top->addWidget(preview_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(buttons);

    // Last choices.
    const int dest = destination_->findData(gLast.destination);
    destination_->setCurrentIndex(dest > 0 ? dest : 0);
    paper_->setCurrentIndex(gLast.paper);
    orientation_->setCurrentIndex(gLast.orientation);
    area_->setCurrentIndex(gLast.area);
    if (!gLast.scale.isEmpty())
        scale_->setCurrentText(gLast.scale);
    units_->setCurrentIndex(gLast.units);
    lineWidth_->setCurrentIndex(gLast.lineWidth);
    monochrome_->setChecked(gLast.monochrome);

    for (QComboBox* c : {destination_, paper_, orientation_, area_, units_, lineWidth_})
        connect(c, &QComboBox::currentIndexChanged, this, &PlotDialog::updatePreview);
    connect(scale_, &QComboBox::currentTextChanged, this, &PlotDialog::updatePreview);
    connect(monochrome_, &QCheckBox::toggled, this, &PlotDialog::updatePreview);
    connect(destination_, &QComboBox::currentIndexChanged, this, updateButton);
    updateButton();
    updatePreview();
}

PlotSettings PlotDialog::settings() const
{
    PlotSettings s;
    s.paper = papers().at(paper_->currentIndex()).second;
    s.orientation = static_cast<PlotSettings::Orientation>(orientation_->currentIndex());
    s.scale = scale_->currentIndex() == 0 && scale_->currentText() == scale_->itemText(0) ? 0.0 : parseScale(scale_->currentText());
    s.unitMm = kUnitsMm[units_->currentIndex()];
    s.monochrome = monochrome_->isChecked();
    s.lineWidthMm = kLineWidths[lineWidth_->currentIndex()];
    return s;
}

BBox PlotDialog::area() const
{
    return area_->currentIndex() == 0 && extents_.valid() ? extents_ : view_;
}

void PlotDialog::updatePreview()
{
    const PlotSettings s = settings();
    const BBox a = area();
    const QPageLayout layout = plotPageLayout(s, a);
    const QSizeF page = layout.fullRect(QPageLayout::Millimeter).size();
    const QRectF paint = layout.paintRect(QPageLayout::Millimeter);

    const double k = std::min((preview_->width() - 10) / page.width(), (preview_->height() - 10) / page.height());
    QPixmap pm(preview_->size());
    pm.fill(palette().color(QPalette::Window));
    QPainter p(&pm);
    const QRectF sheet((pm.width() - page.width() * k) / 2.0, (pm.height() - page.height() * k) / 2.0, page.width() * k,
                       page.height() * k);
    p.fillRect(sheet.translated(2, 2), QColor(0, 0, 0, 60));
    p.fillRect(sheet, Qt::white);
    p.setPen(QPen(QColor(200, 200, 200), 0, Qt::DashLine));
    const QRectF printable(sheet.topLeft() + paint.topLeft() * k, paint.size() * k);
    p.drawRect(printable);
    p.translate(printable.topLeft());
    PlotSettings thin = s;
    thin.lineWidthMm = 0.0;
    paintPlot(p, doc_, thin, a, printable.size(), k);
    p.end();
    preview_->setPixmap(pm);

    const double n = plotScaleDenominator(s, a);
    QString text = tr("Scala sul foglio: 1:%1").arg(QLocale().toString(n, 'g', n < 10 ? 3 : 5));
    if (s.scale > 0.0 && a.valid()) {
        const double needW = a.width() * plotPaperPerUnit(s, a);
        const double needH = a.height() * plotPaperPerUnit(s, a);
        if (needW > paint.width() + 0.01 || needH > paint.height() + 0.01)
            text += tr("\nIl disegno non entra nel foglio a questa scala: ne viene stampata la parte centrale.");
    }
    info_->setText(text);
}

QString PlotDialog::plot()
{
    if (exec() != QDialog::Accepted)
        return {};
    gLast.destination = destination_->currentData().toString();
    gLast.paper = paper_->currentIndex();
    gLast.orientation = orientation_->currentIndex();
    gLast.area = area_->currentIndex();
    gLast.scale = scale_->currentText();
    gLast.units = units_->currentIndex();
    gLast.lineWidth = lineWidth_->currentIndex();
    gLast.monochrome = monochrome_->isChecked();

    const PlotSettings s = settings();
    const BBox a = area();
    if (destination_->currentIndex() == 0) {
        QString path = QFileDialog::getSaveFileName(parentWidget(), tr("Salva PDF"), documentName_ + QStringLiteral(".pdf"),
                                                    tr("PDF (*.pdf)"));
        if (path.isEmpty())
            return {};
        if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
            path += QLatin1String(".pdf");
        QString error;
        if (!writePdf(path, doc_, s, a, &error))
            return tr("Impossibile salvare %1: %2").arg(path, error);
        return tr("Salvato %1").arg(QFileInfo(path).fileName());
    }

    QPrinter printer(QPrinterInfo::printerInfo(destination_->currentData().toString()), QPrinter::HighResolution);
    printer.setDocName(documentName_);
    printer.setPageLayout(plotPageLayout(s, a));
    QPainter p;
    if (!p.begin(&printer))
        return tr("Impossibile stampare su %1.").arg(printer.printerName());
    const QRect paint = printer.pageLayout().paintRectPixels(printer.resolution());
    paintPlot(p, doc_, s, a, paint.size(), printer.resolution() / 25.4);
    p.end();
    return tr("Inviato alla stampante %1").arg(printer.printerName());
}
