// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigator.h"

#include "icons.h"

using namespace cad;

namespace {

constexpr int kStoryRole = Qt::UserRole;

} // namespace

Navigator::Navigator(const Document& doc, QWidget* parent)
    : QTreeWidget(parent)
    , doc_(doc)
{
    setHeaderHidden(true);
    setFocusPolicy(Qt::NoFocus);
    stories_ = new QTreeWidgetItem(this, {tr("Piani")});
    stories_->setIcon(0, icons::make(QStringLiteral("story")));
    auto* views = new QTreeWidgetItem(this, {tr("Viste")});
    plan_ = new QTreeWidgetItem(views, {tr("Pianta del piano corrente")});
    plan_->setIcon(0, icons::make(QStringLiteral("plan")));
    view3d_ = new QTreeWidgetItem(views, {tr("Vista 3D")});
    view3d_->setIcon(0, icons::make(QStringLiteral("view3d")));
    expandAll();
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item == plan_)
            emit planChosen();
        else if (item == view3d_)
            emit view3dChosen();
        else if (item->parent() == stories_)
            emit storyChosen(item->data(0, kStoryRole).toInt());
    });
    refresh();
}

void Navigator::refresh()
{
    const auto& stories = doc_.stories();
    // Rebuilt only when the stories change, so the tree keeps its state.
    bool same = stories_->childCount() == static_cast<int>(stories.size());
    int row = 0;
    for (auto it = stories.rbegin(); same && it != stories.rend(); ++it, ++row) {
        const QTreeWidgetItem* item = stories_->child(row);
        same = item->data(0, kStoryRole).toInt() == it->id
            && item->text(0) == tr("%1  (%2)").arg(QString::fromStdString(it->name)).arg(it->elevation);
    }
    if (!same) {
        qDeleteAll(stories_->takeChildren());
        for (auto it = stories.rbegin(); it != stories.rend(); ++it) {
            auto* item = new QTreeWidgetItem(stories_, {tr("%1  (%2)").arg(QString::fromStdString(it->name)).arg(it->elevation)});
            item->setData(0, kStoryRole, it->id);
        }
    }
    for (int i = 0; i < stories_->childCount(); ++i) {
        QTreeWidgetItem* item = stories_->child(i);
        QFont f = item->font(0);
        f.setBold(item->data(0, kStoryRole).toInt() == doc_.currentStory());
        item->setFont(0, f);
    }
}
