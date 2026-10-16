/*
 * About dialog - version, links and the license notices the bundled libraries require
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_ABOUT_DIALOG_H
#define UI_ABOUT_DIALOG_H

#include <QDialog>

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent);
};

#endif /* UI_ABOUT_DIALOG_H */
