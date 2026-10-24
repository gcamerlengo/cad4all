// SPDX-License-Identifier: GPL-3.0-or-later
// The Stampa dialog: printer or PDF, sheet, scale, colours, with a preview
// of the sheet.
#pragma once

#include "plot.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

class PlotDialog : public QDialog {
    Q_OBJECT

public:
    // `extents` and `view` are the two areas that can be printed.
    PlotDialog(const cad::Document& doc, const cad::BBox& extents, const cad::BBox& view, const QString& documentName,
               QWidget* parent = nullptr);

    // Prints or writes the PDF with the chosen settings. Returns a message
    // for the command line, empty when cancelled.
    QString plot();

private:
    PlotSettings settings() const;
    cad::BBox area() const;
    void updatePreview();

    const cad::Document& doc_;
    cad::BBox extents_;
    cad::BBox view_;
    QString documentName_;

    QComboBox* destination_;
    QComboBox* paper_;
    QComboBox* orientation_;
    QComboBox* area_;
    QComboBox* scale_;
    QComboBox* units_;
    QComboBox* lineWidth_;
    QCheckBox* monochrome_;
    QLabel* info_;
    QLabel* preview_;
};
