/*
 * Profile dialog - create or edit one connection profile
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_PROFILE_DIALOG_H
#define UI_PROFILE_DIALOG_H

#include "app_audio.h"
#include "audio_device_enum.h"
#include "backend.h"

#include <QDialog>

#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidget;

class ProfileDialog : public QDialog {
    Q_OBJECT
public:
    /* edit_index < 0: a new profile */
    ProfileDialog(Backend *backend, int edit_index, QWidget *parent);

private:
    void build_ui();
    void populate_from_profile(const ConnectionProfile &p);
    void update_codec_dependent();
    void update_format_info();
    void update_capture_hint();
    void refresh_app_list();
    void refresh_device_list();
    void set_address(const QString &addr);
    void on_addr_edited(const QString &text);
    void on_scan();
    void on_device_selected();
    void on_edit_name();
    void on_delete_device();
    void on_save();
    std::string selected_quality() const;
    void set_row_visible(QWidget *field, bool visible);

    Backend *backend_;
    int edit_index_;

    QPushButton *scan_btn_ = nullptr;
    QPushButton *dev_edit_btn_ = nullptr;
    QPushButton *dev_del_btn_ = nullptr;
    QListWidget *device_list_ = nullptr;
    QLineEdit *addr_ = nullptr;
    QLineEdit *devname_ = nullptr;

    QFormLayout *codec_form_ = nullptr;
    QComboBox *codec_ = nullptr;
    QCheckBox *fallback_ = nullptr;
    QComboBox *quality_ = nullptr;
    QCheckBox *abr_ = nullptr;
    QComboBox *sample_rate_ = nullptr;
    QComboBox *bit_depth_ = nullptr;
    QComboBox *latency_ = nullptr;

    QFormLayout *audio_form_ = nullptr;
    QComboBox *capture_ = nullptr;
    QLabel *capture_hint_ = nullptr;
    QWidget *app_row_ = nullptr;
    QComboBox *app_ = nullptr;
    QPushButton *app_refresh_ = nullptr;
    QLabel *app_note_ = nullptr;
    QComboBox *audio_dev_ = nullptr;
    QCheckBox *auto_switch_ = nullptr;
    QComboBox *volume_sync_ = nullptr;
    QLabel *format_info_ = nullptr;

    QSpinBox *max_packet_ = nullptr;

    std::vector<std::string> quality_keys_;
    std::string pending_quality_;
    std::vector<AudioDeviceInfo> audio_devices_;  /* virtual devices (Virtual Device mode) */
    std::vector<AudioDeviceInfo> volume_devices_; /* every output device (volume sync) */
    std::vector<AudioAppInfo> apps_;
    std::string saved_app_exe_, saved_app_name_;
    QString last_addr_;
};

#endif /* UI_PROFILE_DIALOG_H */
