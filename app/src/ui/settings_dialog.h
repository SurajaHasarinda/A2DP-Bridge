/*
 * Settings dialog - language, theme and the options that were in the Settings
 * menu. Every change applies and saves at once.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_SETTINGS_DIALOG_H
#define UI_SETTINGS_DIALOG_H

#include <QDialog>

class Backend;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Backend *backend, QWidget *parent);

    /* Closed because the language changed: open it again */
    bool reopen() const { return reopen_; }

signals:
    /* The language was changed and loaded; windows should relabel */
    void language_changed();

private:
    Backend *backend_;
    bool reopen_ = false;
};

#endif /* UI_SETTINGS_DIALOG_H */
