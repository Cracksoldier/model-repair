#pragma once

#include "modelrepair/RepairReport.hpp"
#include <QWidget>
#include <QTreeWidget>

namespace gui
{

class ReportView : public QWidget
{
    Q_OBJECT

public:
    explicit ReportView(QWidget* parent = nullptr);

    void set_report(const modelrepair::RepairReport& report);
    void clear();

private:
    QTreeWidget* tree_;
};

} // namespace gui
