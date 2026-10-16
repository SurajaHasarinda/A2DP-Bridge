/*
 * Firmware dialog - detected adapters, chip selection, firmware files status
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_FIRMWARE_DIALOG_H
#define UI_FIRMWARE_DIALOG_H

#include "bt_adapter_enum.h"

#include <QDialog>

#include <string>
#include <vector>

class Backend;
class QComboBox;
class QLabel;
class QPushButton;

class FirmwareDialog : public QDialog {
    Q_OBJECT
public:
    FirmwareDialog(Backend *backend, QWidget *parent);

    /* The chip choice changed (BTstack must start again to use it) */
    bool chip_changed() const { return chip_changed_; }

protected:
    bool event(QEvent *e) override;

private:
    struct ChipOption {
        uint16_t pid;
        std::string stem;
    };
    void on_chip_changed(int index);
    void update_status();
    void on_download();
    uint16_t effective_pid() const;

    Backend *backend_;
    std::vector<BtAdapterInfo> adapters_;
    std::vector<ChipOption> options_;
    BtChipVendor other_vendor_ = BtChipVendor::Unknown;
    bool chip_changed_ = false;

    QLabel *adapter_info_;
    QComboBox *chip_;
    QLabel *status_;
    QLabel *manual_hint_;
    QPushButton *download_btn_;
};

#endif /* UI_FIRMWARE_DIALOG_H */
