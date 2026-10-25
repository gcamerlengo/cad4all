// SPDX-License-Identifier: GPL-3.0-or-later
#include "layerpanel.h"

#include "stylenames.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace cad;

LayerPanel::LayerPanel(CommandEngine& engine, QWidget* parent)
    : QWidget(parent), engine_(engine)
{
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Nome"), tr("Visibile"), tr("Bloccato"), tr("Colore"), tr("Tipo di linea"), tr("Spessore")});
    tree_->setRootIsDecorated(false);
    tree_->setFocusPolicy(Qt::NoFocus);
    // Every column as wide as its contents, so no name is cut; a narrow
    // panel scrolls sideways.
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(true);
    tree_->setToolTip(tr("Doppio clic sul nome per rendere corrente il layer, sul colore, sul tipo di linea o sullo spessore per cambiarli."));

    auto* add = new QPushButton(tr("Nuovo layer"), this);
    auto* current = new QPushButton(tr("Rendi corrente"), this);
    for (QPushButton* b : {add, current})
        b->setFocusPolicy(Qt::NoFocus);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(current);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(buttons);
    layout->addWidget(tree_);

    connect(add, &QPushButton::clicked, this, &LayerPanel::createLayer);
    connect(current, &QPushButton::clicked, this, [this] { makeCurrent(tree_->currentItem()); });
    connect(tree_, &QTreeWidget::itemChanged, this, &LayerPanel::itemChanged);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &LayerPanel::itemDoubleClicked);
    connect(&engine_, &CommandEngine::changed, this, [this] {
        if (engine_.document().revision() != shownRevision_)
            refresh();
    });
    refresh();
}

void LayerPanel::refresh()
{
    const Document& doc = engine_.document();
    shownRevision_ = doc.revision();
    const QString selected = tree_->currentItem() ? tree_->currentItem()->text(NameColumn) : QString();

    const QSignalBlocker blocker(tree_);
    tree_->clear();
    for (const Layer& l : doc.layers()) {
        auto* item = new QTreeWidgetItem(tree_);
        const QString name = QString::fromStdString(l.name);
        item->setText(NameColumn, name);
        item->setCheckState(VisibleColumn, l.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(LockedColumn, l.locked ? Qt::Checked : Qt::Unchecked);
        const QColor color(l.color.r, l.color.g, l.color.b);
        item->setBackground(ColorColumn, color);
        item->setText(ColorColumn, color.name());
        item->setForeground(ColorColumn, color.lightness() > 128 ? Qt::black : Qt::white);
        item->setText(LinetypeColumn, QString::fromStdString(l.linetype));
        item->setText(LineweightColumn, lineweightName(l.lineweight));
        if (l.name == doc.currentLayer()) {
            QFont f = item->font(NameColumn);
            f.setBold(true);
            item->setFont(NameColumn, f);
            item->setToolTip(NameColumn, tr("Layer corrente"));
        }
        if (name == selected)
            tree_->setCurrentItem(item);
    }
}

void LayerPanel::createLayer()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuovo layer"), tr("Nome del layer:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!engine_.document().addLayer(Layer::named(name.toStdString()))) {
        QMessageBox::warning(this, tr("Nuovo layer"), tr("Esiste già un layer chiamato \"%1\".").arg(name));
        return;
    }
    engine_.document().setCurrentLayer(name.toStdString());
    engine_.message(tr("Layer \"%1\" creato e reso corrente.").arg(name));
    engine_.documentChanged();
}

void LayerPanel::makeCurrent(QTreeWidgetItem* item)
{
    if (!item)
        return;
    const QString name = item->text(NameColumn);
    if (engine_.document().setCurrentLayer(name.toStdString())) {
        engine_.message(tr("Layer corrente: %1").arg(name));
        engine_.documentChanged();
    }
}

void LayerPanel::itemChanged(QTreeWidgetItem* item, int column)
{
    if (column != VisibleColumn && column != LockedColumn)
        return;
    Document& doc = engine_.document();
    const std::string name = item->text(NameColumn).toStdString();
    const Layer* current = doc.layer(name);
    if (!current)
        return;
    Layer updated = *current;
    updated.visible = item->checkState(VisibleColumn) == Qt::Checked;
    updated.locked = item->checkState(LockedColumn) == Qt::Checked;
    doc.updateLayer(name, updated);
    engine_.documentChanged();
}

void LayerPanel::itemDoubleClicked(QTreeWidgetItem* item, int column)
{
    if (column == NameColumn) {
        makeCurrent(item);
        return;
    }
    Document& doc = engine_.document();
    const std::string name = item->text(NameColumn).toStdString();
    const Layer* current = doc.layer(name);
    if (!current)
        return;
    if (column == LinetypeColumn || column == LineweightColumn) {
        QStringList choices;
        int index = 0;
        if (column == LinetypeColumn) {
            for (const Linetype& lt : doc.linetypes()) {
                if (lt.name == current->linetype)
                    index = static_cast<int>(choices.size());
                choices << QStringLiteral("%1  %2").arg(QString::fromStdString(lt.name), QString::fromStdString(lt.description));
            }
        } else {
            choices << lineweightName(kLineweightDefault);
            for (int w : standardLineweights()) {
                if (w == current->lineweight)
                    index = static_cast<int>(choices.size());
                choices << lineweightName(w);
            }
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, column == LinetypeColumn ? tr("Tipo di linea del layer") : tr("Spessore del layer"),
                                                     tr("Layer %1:").arg(item->text(NameColumn)), choices, index, false, &ok);
        if (!ok)
            return;
        Layer updated = *current;
        if (column == LinetypeColumn)
            updated.linetype = chosen.section(QLatin1Char(' '), 0, 0).toStdString();
        else
            updated.lineweight = parseLineweight(chosen).value_or(kLineweightDefault);
        doc.updateLayer(name, updated);
        engine_.documentChanged();
        return;
    }
    if (column != ColorColumn)
        return;
    const QColor chosen = QColorDialog::getColor(QColor(current->color.r, current->color.g, current->color.b), this, tr("Colore del layer"));
    if (!chosen.isValid())
        return;
    Layer updated = *current;
    updated.color = {static_cast<std::uint8_t>(chosen.red()), static_cast<std::uint8_t>(chosen.green()), static_cast<std::uint8_t>(chosen.blue())};
    doc.updateLayer(name, updated);
    engine_.documentChanged();
}
