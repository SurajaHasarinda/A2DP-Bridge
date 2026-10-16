/*
 * About dialog implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "about_dialog.h"

#include "ui_util.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#ifndef APP_VERSION
#define APP_VERSION "1.0.1"
#endif

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(U("help.about_title"));
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(8);

    auto *title = new QLabel(U("help.full_name"), this);
    title->setProperty("title", true);
    root->addWidget(title);

    auto *version = new QLabel(QString("%1: %2").arg(U("help.version"), APP_VERSION), this);
    version->setProperty("secondary", true);
    root->addWidget(version);

    root->addSpacing(8);
    auto *ok = new QPushButton(U("modal.ok"), this);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(ok, 0, Qt::AlignHCenter);
}
