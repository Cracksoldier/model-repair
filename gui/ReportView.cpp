#include "ReportView.hpp"

#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QString>

namespace gui
{

ReportView::ReportView(QWidget* parent) : QWidget(parent)
{
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({"Step", "Found", "Fixed", "Time (ms)"});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setRootIsDecorated(false);
    tree_->setAlternatingRowColors(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tree_);
}

void ReportView::set_report(const modelrepair::RepairReport& r)
{
    tree_->clear();

    // Summary row
    auto* summary = new QTreeWidgetItem(tree_);
    summary->setText(0, r.diagnose_only ? "Diagnosis" : "Summary");
    summary->setText(1, QString("V: %1 → %2").arg(r.vertices_before).arg(r.vertices_after));
    summary->setText(2, QString("T: %1 → %2").arg(r.triangles_before).arg(r.triangles_after));

    // Area / volume annotation appended to watertight status cell
    QString stats = r.is_closed_after && r.is_manifold_after ? "✓ watertight" : "⚠ not watertight";
    stats += QString("   Area: %1 mm²").arg(r.surface_area_after, 0, 'f', 1);
    if (r.volume_after.has_value())
        stats += QString("   Vol: %1 mm³").arg(*r.volume_after, 0, 'f', 1);
    summary->setText(3, stats);
    QFont bold = summary->font(0);
    bold.setBold(true);
    for (int c = 0; c < 4; ++c)
        summary->setFont(c, bold);

    // Per-step rows
    for (const auto& step : r.steps)
    {
        if (!step.was_run)
            continue;
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(step.name));
        item->setText(1, QString::number(step.issues_found));
        item->setText(2, QString::number(step.issues_fixed));
        item->setText(3, QString::number(step.duration_ms, 'f', 1));

        if (step.issues_found > 0 && step.issues_fixed < step.issues_found)
        {
            // Some issues could not be fixed — highlight in amber
            for (int c = 0; c < 4; ++c)
                item->setForeground(c, Qt::darkYellow);
        }
    }

    tree_->expandAll();
}

void ReportView::clear()
{
    tree_->clear();
}

} // namespace gui
