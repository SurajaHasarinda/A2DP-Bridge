/*
 * Settings dialog implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "settings_dialog.h"

#include "backend.h"
#include "btstack_transport.h"
#include "system_integration.h"
#include "theme.h"
#include "ui_util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Backend *backend, QWidget *parent) : QDialog(parent), backend_(backend) {
    setWindowTitle(U("menu.settings"));
    setMinimumWidth(420);
    AppSettings &s = backend_->settings();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    /* ---- Appearance ---- */
    auto *look = new QFrame(this);
    look->setObjectName("card");
    auto *form = new QFormLayout(look);
    form->setContentsMargins(14, 12, 14, 12);
    form->setVerticalSpacing(10);

    auto *lang = new QComboBox(look);
    const auto langs = Localization::instance().get_available_languages();
    for (const auto &l : langs) lang->addItem(U8(l.display_name), U8(l.code));
    lang->setCurrentIndex(qMax(0, lang->findData(U8(s.language))));
    form->addRow(U("settings.language"), lang);
    connect(lang, &QComboBox::activated, this, [this, lang](int i) {
        AppSettings &st = backend_->settings();
        st.language = lang->itemData(i).toString().toStdString();
        Localization::instance().load(st.language);
        st.save();
        emit language_changed();
        reopen_ = true; /* the caller opens it again, in the new language */
        accept();
    });

    auto *theme = new QComboBox(look);
    theme->addItem(U("settings.theme_system"), static_cast<int>(Theme::Mode::System));
    theme->addItem(U("settings.theme_light"), static_cast<int>(Theme::Mode::Light));
    theme->addItem(U("settings.theme_dark"), static_cast<int>(Theme::Mode::Dark));
    theme->setCurrentIndex(qMax(0, theme->findData(static_cast<int>(TH().mode()))));
    form->addRow(U("settings.theme"), theme);
    connect(theme, &QComboBox::activated, this, [this, theme](int i) {
        auto mode = static_cast<Theme::Mode>(theme->itemData(i).toInt());
        TH().set_mode(mode);
        backend_->settings().theme = Theme::mode_to_string(mode);
        backend_->settings().save();
    });
    root->addWidget(look);

    /* ---- Options ---- */
    auto *opts = new QFrame(this);
    opts->setObjectName("card");
    auto *ov = new QVBoxLayout(opts);
    ov->setContentsMargins(14, 12, 14, 12);
    ov->setSpacing(8);

    auto add = [&](const char *key, bool value, auto apply) {
        auto *cb = new QCheckBox(U(key), opts);
        cb->setChecked(value);
        connect(cb, &QCheckBox::toggled, this, [this, apply](bool on) {
            apply(on);
            backend_->settings().save();
        });
        ov->addWidget(cb);
        return cb;
    };
    add("settings.start_with_windows", s.start_with_windows, [this](bool on) {
        backend_->settings().start_with_windows = on;
        if (on) RegisterStartup("--minimized");
        else UnregisterStartup();
    });
    add("settings.minimize_to_tray", s.minimize_to_tray, [this](bool on) { backend_->settings().minimize_to_tray = on; });
    add("settings.media_keys", s.media_keys, [this](bool on) {
        backend_->settings().media_keys = on;
        BtStackTransport::set_media_keys_enabled(on);
    });
    add("settings.remember_volume", s.remember_volume, [this](bool on) {
        backend_->settings().remember_volume = on;
        backend_->service().set_remember_volume(on);
    });
    root->addWidget(opts);

    /* ---- Debug ---- */
    auto *debug = new QCheckBox(U("settings.debug_mode"), this);
    debug->setChecked(s.debug_mode);
    connect(debug, &QCheckBox::toggled, this, [this](bool on) {
        /* debug.log is opened at startup and the HCI dump when BTstack is first
         * initialized, so the change applies after a restart. */
        backend_->settings().debug_mode = on;
        backend_->settings().save();
        QMessageBox::information(this, U("settings.debug_mode"),
                                 U(on ? "settings.debug_mode_enabled" : "settings.debug_mode_disabled"));
    });
    root->addWidget(debug);

    auto *close = new QPushButton(U("modal.close"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(close);
    root->addLayout(row);
}
