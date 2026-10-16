/*
 * Firmware dialog implementation
 *
 * Consolidated firmware management: detected adapter display, file-based chip
 * selection (scans the config folder for *_fw.bin + *_config.bin pairs),
 * firmware status, and manual placement instructions.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "firmware_dialog.h"

#include "backend.h"
#include "ui_util.h"

#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {
void set_state(QLabel *l, const char *flag) {
    for (const char *p : {"ok", "warning", "muted"}) l->setProperty(p, false);
    if (flag) l->setProperty(flag, true);
    l->style()->unpolish(l);
    l->style()->polish(l);
}
} // namespace

FirmwareDialog::FirmwareDialog(Backend *backend, QWidget *parent) : QDialog(parent), backend_(backend) {
    setWindowTitle(U("firmware.title"));
    setMinimumWidth(520);
    A2dpService &service = backend_->service();
    AppSettings &settings = backend_->settings();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    /* ---- Detected Bluetooth adapters ---- */
    auto *adapters_title = new QLabel(U("firmware.detected_adapters"), this);
    adapters_title->setProperty("heading", true);
    root->addWidget(adapters_title);
    auto *adapter_card = new QFrame(this);
    adapter_card->setObjectName("card");
    auto *av = new QVBoxLayout(adapter_card);
    av->setContentsMargins(14, 10, 14, 10);
    adapter_info_ = new QLabel(adapter_card);
    adapter_info_->setWordWrap(true);
    adapter_info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    av->addWidget(adapter_info_);
    root->addWidget(adapter_card);

    adapters_ = BtAdapterEnumerator::enumerate();
    bool realtek_seen = false;
    for (const auto &a : adapters_) {
        if (a.vendor == BtChipVendor::Realtek) realtek_seen = true;
        else if (a.vendor != BtChipVendor::Unknown && other_vendor_ == BtChipVendor::Unknown) other_vendor_ = a.vendor;
    }
    if (realtek_seen) other_vendor_ = BtChipVendor::Unknown;
    if (adapters_.empty()) {
        adapter_info_->setText(U("firmware.no_adapters"));
    } else {
        QStringList lines;
        for (const auto &a : adapters_) lines << U8(a.display_name);
        adapter_info_->setText(lines.join('\n'));
    }

    /* ---- Chip selector ----
     * Auto (from the detected adapter), Not Realtek, every Realtek chip in the
     * chip table whether its files are there or not, chips known only by
     * firmware files in the config folder, and Custom. Nothing is saved until
     * the user picks an entry. */
    auto *chip_title = new QLabel(U("settings.bt_chip"), this);
    chip_title->setProperty("heading", true);
    root->addWidget(chip_title);
    chip_ = new QComboBox(this);

    auto add_option = [this](uint16_t pid, const std::string &stem, const QString &label) {
        options_.push_back({pid, stem});
        chip_->addItem(label);
    };
    const uint16_t auto_pid = BtAdapterEnumerator::resolve_chip_pid(0, "", adapters_);
    std::string auto_chip;
    if (auto_pid != 0) {
        const char *name = BtAdapterEnumerator::realtek_chip_name(auto_pid);
        char buf[32];
        snprintf(buf, sizeof(buf), "Realtek 0x%04X", auto_pid);
        auto_chip = name ? name : buf;
    }
    add_option(0, "", fmt("firmware.chip_auto", auto_pid ? U8(auto_chip) : U("firmware.chip_auto_none")));
    add_option(NON_REALTEK_CHIP_PID, "", U("firmware.chip_non_realtek"));
    for (const auto &c : BtAdapterEnumerator::get_realtek_chips()) {
        if (c.pid == CUSTOM_CHIP_PID) continue;
        std::string stem = c.fw_name;
        stem.resize(stem.size() - 3); /* drop "_fw" */
        add_option(c.pid, stem, U8(c.chip_name));
    }
    for (const auto &e : BtAdapterEnumerator::scan_firmware_files(service.get_config_dir())) {
        if (e.pid == 0) add_option(0, e.stem, U8(e.display_name + " (" + e.stem + "_fw.bin)"));
    }
    add_option(CUSTOM_CHIP_PID, "custom", "Custom (custom_fw.bin, custom_config.bin)");

    int sel = -1;
    for (int i = 0; i < static_cast<int>(options_.size()) && sel < 0; i++) {
        const auto &o = options_[i];
        if (o.pid != 0 ? o.pid == settings.bt_chip_pid
                       : settings.bt_chip_pid == 0 && o.stem == settings.bt_chip_fw_stem)
            sel = i;
    }
    if (sel < 0) {
        /* A setting no entry stands for (e.g. a chip since dropped from the
         * table): keep it selectable as it is */
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%04X %s", settings.bt_chip_pid, settings.bt_chip_fw_stem.c_str());
        add_option(settings.bt_chip_pid, settings.bt_chip_fw_stem, buf);
        sel = static_cast<int>(options_.size()) - 1;
    }
    chip_->setCurrentIndex(sel);
    connect(chip_, &QComboBox::activated, this, &FirmwareDialog::on_chip_changed);
    root->addWidget(chip_);

    /* ---- Firmware status ---- */
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    root->addWidget(status_);
    manual_hint_ = new QLabel(this);
    manual_hint_->setWordWrap(true);
    manual_hint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(manual_hint_);

    /* ---- Buttons ---- */
    auto *row = new QHBoxLayout;
    download_btn_ = new QPushButton(U("firmware.open_download_page"), this);
    connect(download_btn_, &QPushButton::clicked, this, &FirmwareDialog::on_download);
    row->addWidget(download_btn_);
    auto *folder = new QPushButton(U("firmware.open_folder"), this);
    connect(folder, &QPushButton::clicked, this, [this] {
        const QString dir = U8(backend_->service().get_config_dir());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    row->addWidget(folder);
    row->addStretch(1);
    root->addLayout(row);

    auto *instructions = new QLabel(U("firmware.manual_instructions"), this);
    instructions->setProperty("secondary", true);
    instructions->setWordWrap(true);
    root->addWidget(instructions);

    auto *ok = new QPushButton(U("modal.ok"), this);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto *bar = new QHBoxLayout;
    bar->addStretch(1);
    bar->addWidget(ok);
    root->addLayout(bar);

    update_status();
}

/* Rescan firmware presence when the dialog regains focus (e.g. after the user
 * places files via Open Config Folder and switches back) */
bool FirmwareDialog::event(QEvent *e) {
    if (e->type() == QEvent::WindowActivate) {
        backend_->service().check_firmware_present();
        update_status();
    }
    return QDialog::event(e);
}

void FirmwareDialog::on_chip_changed(int sel) {
    if (sel < 0 || sel >= static_cast<int>(options_.size())) return;
    AppSettings &settings = backend_->settings();
    const uint16_t new_pid = options_[sel].pid;
    const std::string new_stem = options_[sel].stem;
    if (new_pid != settings.bt_chip_pid || new_stem != settings.bt_chip_fw_stem) {
        settings.bt_chip_pid = new_pid;
        settings.bt_chip_fw_stem = new_stem;
        settings.save();
        backend_->service().set_bt_chip_pid(new_pid);
        backend_->service().set_bt_chip_fw_stem(new_stem);
        backend_->service().check_firmware_present();
        chip_changed_ = true;
    }
    update_status();
}

uint16_t FirmwareDialog::effective_pid() const {
    const AppSettings &s = backend_->settings();
    return BtAdapterEnumerator::resolve_chip_pid(s.bt_chip_pid, s.bt_chip_fw_stem, adapters_);
}

void FirmwareDialog::update_status() {
    const AppSettings &settings = backend_->settings();
    const uint16_t pid = effective_pid();
    const bool is_auto = BtAdapterEnumerator::is_auto_chip(settings.bt_chip_pid, settings.bt_chip_fw_stem);
    const bool non_realtek = settings.bt_chip_pid == NON_REALTEK_CHIP_PID;

    /* Determine firmware/config filenames */
    std::string fw_name, cfg_name;
    const char *fw = BtAdapterEnumerator::realtek_fw_name(pid);
    const char *cfg = BtAdapterEnumerator::realtek_cfg_name(pid);
    if (fw && cfg) {
        fw_name = fw;
        cfg_name = cfg;
    } else if (!non_realtek && !settings.bt_chip_fw_stem.empty()) {
        fw_name = settings.bt_chip_fw_stem + "_fw";
        cfg_name = settings.bt_chip_fw_stem + "_config";
    }

    if (fw_name.empty() && other_vendor_ == BtChipVendor::Unknown && (non_realtek || is_auto)) {
        /* No Realtek init and no vendor we can say more about */
        const char *key = non_realtek ? "firmware.non_realtek"
                        : adapters_.empty() ? "firmware.auto_no_adapter" : "firmware.auto_unrecognized";
        status_->setText(U(key));
        set_state(status_, non_realtek ? "ok" : "warning");
        download_btn_->setEnabled(false);
        manual_hint_->clear();
    } else if (fw_name.empty() && other_vendor_ != BtChipVendor::Unknown) {
        /* Non-Realtek adapter: no rtl_bt files; explain what (if anything) this vendor needs instead */
        const char *key = "firmware.vendor_unsupported";
        bool ok = true;
        switch (other_vendor_) {
        case BtChipVendor::Intel:    key = "firmware.vendor_intel"; break;
        case BtChipVendor::Broadcom: key = "firmware.vendor_broadcom"; break;
        case BtChipVendor::Csr:      key = "firmware.vendor_csr"; break;
        default:                     ok = false; break;
        }
        status_->setText(U(key));
        set_state(status_, ok ? "ok" : "warning");
        download_btn_->setEnabled(other_vendor_ == BtChipVendor::Intel || other_vendor_ == BtChipVendor::Broadcom);
        manual_hint_->clear();
    } else if (fw_name.empty()) {
        status_->setText(U("firmware.select_chip"));
        set_state(status_, "warning");
        download_btn_->setEnabled(false);
        manual_hint_->clear();
    } else if (backend_->service().is_firmware_present()) {
        status_->setText(U("firmware.present"));
        set_state(status_, "ok");
        download_btn_->setEnabled(true);
        manual_hint_->setText(U8(fw_name + ".bin, " + cfg_name + ".bin"));
        set_state(manual_hint_, "muted");
    } else {
        status_->setText(U("firmware.missing_manual"));
        set_state(status_, "warning");
        download_btn_->setEnabled(true);
        manual_hint_->setText(U("firmware.required_files") + "\n" + U8(fw_name + ".bin, " + cfg_name + ".bin"));
        set_state(manual_hint_, "muted");
    }
}

void FirmwareDialog::on_download() {
    /* Open the firmware source for the adapter's vendor in the user's browser */
    const AppSettings &settings = backend_->settings();
    QString url = "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt";
    const bool realtek_selected = BtAdapterEnumerator::realtek_fw_name(effective_pid()) ||
                                  (settings.bt_chip_pid != NON_REALTEK_CHIP_PID && !settings.bt_chip_fw_stem.empty());
    if (!realtek_selected && other_vendor_ == BtChipVendor::Intel) {
        url = "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/intel";
    } else if (!realtek_selected && other_vendor_ == BtChipVendor::Broadcom) {
        /* linux-firmware carries no brcm .hcd; this collects them from the Windows drivers */
        url = "https://github.com/winterheart/broadcom-bt-firmware";
    }
    open_url(url);
}
