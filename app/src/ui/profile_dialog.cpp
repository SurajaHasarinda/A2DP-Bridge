/*
 * Profile dialog implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "profile_dialog.h"

#include "bt_device.h"
#include "media_payload_limit.h"
#include "theme.h"
#include "ui_util.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cctype>
#include <windows.h>

namespace {

const uint32_t RATE_VALUES[] = {0, 44100, 48000, 88200, 96000};
const uint32_t BIT_VALUES[] = {0, 16, 24};

std::wstring utf8_to_wide(const std::string &s) {
    std::wstring w;
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len > 0) {
        w.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    }
    return w;
}

std::string wide_to_utf8(const std::wstring &w) {
    std::string s;
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        s.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    }
    return s;
}

/* Full-width characters from an IME ("ＡＡ：ＢＢ") as their ASCII forms */
QString ascii_from_fullwidth(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        ushort v = c.unicode();
        if (v >= 0xFF01 && v <= 0xFF5E) out += QChar(v - 0xFEE0);
        else if (v == 0x3000) out += ' ';
        else out += c;
    }
    return out;
}

bool is_space(QChar c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool is_separator(QChar c) { return c == ':' || c == '-' || c == '.' || is_space(c); }
bool is_hex(QChar c) { return c.unicode() < 128 && std::isxdigit(c.unicode()); }

/* Formats an address as it is typed or pasted: hex digits in pairs joined by
 * colons, upper case ("aa-bb-c" -> "AA:BB:C"). A separator typed after a
 * complete pair stays, so "AA:" can be typed; no colon is added on its own,
 * so Backspace over one works. The caret keeps its place among the digits.
 * False if the text cannot become an address (other characters, more than 12
 * digits): it is left as it is. */
bool format_address_input(const QString &in, int caret_in, QString &out, int &caret_out) {
    QString s = ascii_from_fullwidth(in);
    int begin = 0, end = s.size();
    while (begin < end && is_space(s[begin])) begin++;
    QString digits;
    bool trailing_sep = false;
    int digits_before_caret = 0;
    for (int i = begin; i < end; i++) {
        QChar c = s[i];
        if (is_hex(c)) {
            if (digits.size() >= 12) return false;
            digits += c.toUpper();
            if (i < caret_in) digits_before_caret++;
            trailing_sep = false;
        } else if (is_separator(c)) {
            /* Anywhere: a digit typed or deleted inside the address moves the
             * rest along, and the pairs are formed again */
            trailing_sep = !digits.isEmpty() && digits.size() % 2 == 0;
        } else {
            return false;
        }
    }
    out.clear();
    for (int i = 0; i < digits.size(); i++) {
        if (i > 0 && i % 2 == 0) out += ':';
        out += digits[i];
    }
    if (trailing_sep && digits.size() < 12) out += ':';
    if (caret_in >= in.size()) caret_out = out.size();
    else caret_out = digits_before_caret + (digits_before_caret > 0 ? (digits_before_caret - 1) / 2 : 0);
    return true;
}

QLabel *hint_label(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setProperty("muted", true);
    l->setWordWrap(true);
    return l;
}

QFrame *make_card(QWidget *parent, QFormLayout **form) {
    auto *card = new QFrame(parent);
    card->setObjectName("card");
    auto *f = new QFormLayout(card);
    f->setContentsMargins(14, 12, 14, 12);
    f->setVerticalSpacing(10);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    *form = f;
    return card;
}

QLabel *section_title(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setProperty("heading", true);
    return l;
}

} // namespace

/* ======================================================================== */

ProfileDialog::ProfileDialog(Backend *backend, int edit_index, QWidget *parent)
    : QDialog(parent), backend_(backend), edit_index_(edit_index) {
    setWindowTitle(edit_index < 0 ? U("profile.new_title") : U("profile.edit_title"));
    resize(560, 700);
    setMinimumWidth(500);

    build_ui();

    if (edit_index >= 0 && edit_index < static_cast<int>(backend_->profiles().profiles().size()))
        populate_from_profile(backend_->profiles().profiles()[edit_index]);

    update_codec_dependent();
    connect(backend_, &Backend::scan_completed, this, [this] {
        scan_btn_->setText(U("connection.scan"));
        scan_btn_->setEnabled(true);
        refresh_device_list();
    });
}

void ProfileDialog::set_row_visible(QWidget *field, bool visible) {
    QFormLayout *forms[] = {codec_form_, audio_form_};
    for (QFormLayout *f : forms) {
        int row = -1;
        QFormLayout::ItemRole role;
        f->getWidgetPosition(field, &row, &role);
        if (row >= 0) {
            f->setRowVisible(row, visible);
            return;
        }
    }
}

void ProfileDialog::build_ui() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget;
    auto *root = new QVBoxLayout(body);
    root->setContentsMargins(20, 16, 20, 8);
    root->setSpacing(10);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    /* ---- Device ---- */
    root->addWidget(section_title(U("profile.section_device"), body));
    auto *dev_card = new QFrame(body);
    dev_card->setObjectName("card");
    auto *dv = new QVBoxLayout(dev_card);
    dv->setContentsMargins(14, 12, 14, 12);
    dv->setSpacing(10);

    auto *scan_row = new QHBoxLayout;
    scan_btn_ = new QPushButton(U("connection.scan"), dev_card);
    connect(scan_btn_, &QPushButton::clicked, this, &ProfileDialog::on_scan);
    scan_row->addWidget(scan_btn_);
    scan_row->addStretch(1);
    dev_edit_btn_ = new QPushButton(U("device.edit_name"), dev_card);
    dev_edit_btn_->setEnabled(false);
    connect(dev_edit_btn_, &QPushButton::clicked, this, &ProfileDialog::on_edit_name);
    scan_row->addWidget(dev_edit_btn_);
    dev_del_btn_ = new QPushButton(U("profile.delete"), dev_card);
    dev_del_btn_->setEnabled(false);
    connect(dev_del_btn_, &QPushButton::clicked, this, &ProfileDialog::on_delete_device);
    scan_row->addWidget(dev_del_btn_);
    dv->addLayout(scan_row);

    device_list_ = new QListWidget(dev_card);
    device_list_->setFixedHeight(110);
    connect(device_list_, &QListWidget::itemSelectionChanged, this, &ProfileDialog::on_device_selected);
    dv->addWidget(device_list_);
    refresh_device_list();

    auto *dform = new QFormLayout;
    dform->setVerticalSpacing(10);
    addr_ = new QLineEdit(dev_card);
    addr_->setPlaceholderText("XX:XX:XX:XX:XX:XX");
    connect(addr_, &QLineEdit::textEdited, this, &ProfileDialog::on_addr_edited);
    dform->addRow(U("connection.device_addr"), addr_);
    devname_ = new QLineEdit(dev_card);
    dform->addRow(U("profile.device_name"), devname_);
    dv->addLayout(dform);
    root->addWidget(dev_card);

    /* ---- Codec ---- */
    root->addWidget(section_title(U("profile.section_codec"), body));
    root->addWidget(make_card(body, &codec_form_));
    QWidget *codec_card = codec_form_->parentWidget();

    codec_ = new QComboBox(codec_card);
    for (const char *k : {"codec.auto", "codec.ldac", "codec.aptx_hd", "codec.aptx_ll", "codec.aptx", "codec.sbc",
                          "codec.aac", "codec.lhdc_v5"})
        codec_->addItem(U(k));
    codec_->setToolTip(U("tooltip.codec"));
    connect(codec_, &QComboBox::currentIndexChanged, this, [this] { update_codec_dependent(); });
    codec_form_->addRow(U("connection.codec"), codec_);

    fallback_ = new QCheckBox(U("connection.codec_fallback"), codec_card);
    fallback_->setToolTip(U("tooltip.codec_fallback"));
    fallback_->setChecked(true);
    codec_form_->addRow(QString(), fallback_);

    quality_ = new QComboBox(codec_card);
    quality_->setToolTip(U("tooltip.quality"));
    codec_form_->addRow(U("connection.quality"), quality_);

    abr_ = new QCheckBox(U("connection.abr"), codec_card);
    abr_->setToolTip(U("tooltip.abr"));
    codec_form_->addRow(QString(), abr_);

    sample_rate_ = new QComboBox(codec_card);
    sample_rate_->setToolTip(U("tooltip.sample_rate"));
    connect(sample_rate_, &QComboBox::currentIndexChanged, this, [this] { update_format_info(); });
    codec_form_->addRow(U("connection.sample_rate"), sample_rate_);

    bit_depth_ = new QComboBox(codec_card);
    bit_depth_->setToolTip(U("tooltip.bit_depth"));
    connect(bit_depth_, &QComboBox::currentIndexChanged, this, [this] { update_format_info(); });
    codec_form_->addRow(U("connection.bit_depth"), bit_depth_);

    /* ---- Audio source ---- */
    root->addWidget(section_title(U("profile.section_audio"), body));
    root->addWidget(make_card(body, &audio_form_));
    QWidget *audio_card = audio_form_->parentWidget();

    capture_ = new QComboBox(audio_card);
    for (const char *k : {"capture.loopback", "capture.virtual", "capture.app"}) capture_->addItem(U(k));
    capture_->setToolTip(U("tooltip.capture_mode"));
    connect(capture_, &QComboBox::currentIndexChanged, this, [this] { update_codec_dependent(); });
    audio_form_->addRow(U("capture.mode"), capture_);

    capture_hint_ = hint_label({}, audio_card);
    audio_form_->addRow(QString(), capture_hint_);

    app_row_ = new QWidget(audio_card);
    auto *app_h = new QHBoxLayout(app_row_);
    app_h->setContentsMargins(0, 0, 0, 0);
    app_ = new QComboBox(app_row_);
    app_->setToolTip(U("tooltip.app"));
    app_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    app_->setMinimumContentsLength(18);
    app_h->addWidget(app_, 1);
    app_refresh_ = new QPushButton(U("capture.app_refresh"), app_row_);
    connect(app_refresh_, &QPushButton::clicked, this, [this] { refresh_app_list(); });
    app_h->addWidget(app_refresh_);
    audio_form_->addRow(U("capture.app_select"), app_row_);
    app_note_ = hint_label({}, audio_card);
    audio_form_->addRow(QString(), app_note_);

    audio_dev_ = new QComboBox(audio_card);
    audio_dev_->setToolTip(U("tooltip.audio_device"));
    connect(audio_dev_, &QComboBox::currentIndexChanged, this, [this] { update_format_info(); });
    audio_form_->addRow(U("capture.audio_device"), audio_dev_);
    auto_switch_ = new QCheckBox(U("capture.auto_switch"), audio_card);
    auto_switch_->setToolTip(U("tooltip.auto_switch"));
    auto_switch_->setChecked(true);
    audio_form_->addRow(QString(), auto_switch_);

    format_info_ = hint_label({}, audio_card);
    audio_form_->addRow(QString(), format_info_);

    /* ---- Advanced ---- */
    auto *adv_toggle = new QPushButton(QString::fromUtf16(glyph::Chevron) + "  " + U("profile.section_advanced"), body);
    adv_toggle->setFlat(true);
    adv_toggle->setCheckable(true);
    adv_toggle->setFont(TH().icon_font(13));
    adv_toggle->setStyleSheet("text-align: left; border: none; padding: 4px 0;");
    root->addWidget(adv_toggle);
    QFormLayout *adv_form;
    QFrame *adv_card = make_card(body, &adv_form);
    adv_card->setVisible(false);
    connect(adv_toggle, &QPushButton::toggled, adv_card, &QWidget::setVisible);
    root->addWidget(adv_card);

    /* Windows volume synced with the headphones (volume_sync.h) */
    volume_sync_ = new QComboBox(adv_card);
    volume_sync_->setToolTip(U("tooltip.volume_sync"));
    volume_sync_->addItem(U("capture.volume_sync_auto"));
    volume_sync_->addItem(U("capture.volume_sync_off"));
    {
        AudioDeviceEnumerator enumerator;
        if (enumerator.init()) {
            volume_devices_ = enumerator.enumerate();
            enumerator.shutdown();
        }
    }
    for (auto &d : volume_devices_) volume_sync_->addItem(U8(d.display_name));
    adv_form->addRow(U("capture.volume_sync"), volume_sync_);

    /* Max media packet size */
    const QString packet_help = fmt("advanced.max_packet_help", (int)MEDIA_PAYLOAD_LIMIT_MIN,
                                    (int)MEDIA_PAYLOAD_LIMIT_MAX, (int)MEDIA_PAYLOAD_LIMIT_DEFAULT);
    auto *packet_row = new QWidget(adv_card);
    auto *ph = new QHBoxLayout(packet_row);
    ph->setContentsMargins(0, 0, 0, 0);
    max_packet_ = new QSpinBox(packet_row);
    max_packet_->setRange(MEDIA_PAYLOAD_LIMIT_MIN, MEDIA_PAYLOAD_LIMIT_MAX);
    max_packet_->setValue(MEDIA_PAYLOAD_LIMIT_DEFAULT);
    max_packet_->setToolTip(packet_help);
    ph->addWidget(max_packet_);
    auto *reset = new QPushButton(fmt("advanced.max_packet_recommended", (int)MEDIA_PAYLOAD_LIMIT_DEFAULT), packet_row);
    connect(reset, &QPushButton::clicked, this, [this] { max_packet_->setValue(MEDIA_PAYLOAD_LIMIT_DEFAULT); });
    ph->addWidget(reset);
    ph->addStretch(1);
    auto *packet_label = new QLabel(U("advanced.max_packet"), adv_card);
    packet_label->setToolTip(packet_help);
    adv_form->addRow(packet_label, packet_row);

    root->addStretch(1);

    /* ---- Buttons ---- */
    auto *bar = new QFrame(this);
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(20, 10, 20, 14);
    bh->addStretch(1);
    auto *cancel = new QPushButton(U("modal.cancel"), bar);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *save = new QPushButton(U("modal.save"), bar);
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, &ProfileDialog::on_save);
    bh->addWidget(cancel);
    bh->addWidget(save);
    outer->addWidget(bar);
}

/* ======================================================================== */

void ProfileDialog::populate_from_profile(const ConnectionProfile &p) {
    set_address(U8(p.device_address));
    devname_->setText(U8(p.device_name));
    codec_->setCurrentIndex(ProfileManager::codec_to_index(p.codec));
    pending_quality_ = p.quality; /* selected by update_codec_dependent below */
    abr_->setChecked(p.abr);
    fallback_->setChecked(p.codec_fallback);
    max_packet_->setValue(p.max_media_payload);

    const int sr_idx = (p.sample_rate == 44100) ? 1 : (p.sample_rate == 48000) ? 2
                     : (p.sample_rate == 88200) ? 3 : (p.sample_rate == 96000) ? 4 : 0;
    const int bd_idx = ProfileManager::bit_depth_to_index(p.bit_depth);
    saved_app_exe_ = p.app_exe;
    saved_app_name_ = p.app_name;
    capture_->setCurrentIndex(ProfileManager::capture_mode_to_index(p.capture_mode));

    update_codec_dependent();
    auto_switch_->setChecked(p.auto_switch_device);

    if (p.volume_sync == "off") {
        volume_sync_->setCurrentIndex(1);
    } else if (p.volume_sync == "device") {
        const std::wstring id = utf8_to_wide(p.volume_device_id);
        size_t i = 0;
        while (i < volume_devices_.size() && volume_devices_[i].id != id) i++;
        if (i == volume_devices_.size()) {
            /* Not connected now: keep it selectable as it was saved */
            AudioDeviceInfo missing{};
            missing.id = id;
            missing.display_name = p.volume_device_name;
            volume_devices_.push_back(missing);
            volume_sync_->addItem(fmt("capture.volume_sync_missing", p.volume_device_name));
        }
        volume_sync_->setCurrentIndex(static_cast<int>(i) + 2);
    }

    if (sr_idx < sample_rate_->count()) sample_rate_->setCurrentIndex(sr_idx);
    if (bd_idx >= 0 && bd_idx < bit_depth_->count()) bit_depth_->setCurrentIndex(bd_idx);

    if (!p.audio_device_id.empty()) {
        const std::wstring saved = utf8_to_wide(p.audio_device_id);
        for (size_t i = 0; i < audio_devices_.size(); i++)
            if (audio_devices_[i].id == saved) {
                audio_dev_->setCurrentIndex(static_cast<int>(i));
                break;
            }
    }
    update_format_info();
}

std::string ProfileDialog::selected_quality() const {
    const int sel = quality_ ? quality_->currentIndex() : -1;
    if (sel >= 0 && sel < static_cast<int>(quality_keys_.size())) return quality_keys_[sel];
    return "hq";
}

void ProfileDialog::update_codec_dependent() {
    const int codec_idx = codec_->currentIndex();
    const bool is_ldac = codec_idx == 1, is_lhdc = codec_idx == 7;
    const bool quality_enabled = codec_idx == 0 || codec_idx == 1 || codec_idx == 5 || codec_idx == 6 || is_lhdc;
    const bool abr_enabled = codec_idx == 0 || codec_idx == 1 || is_lhdc;

    /* Quality items with the codec's bitrates */
    const std::string cur_key = pending_quality_.empty() ? selected_quality() : pending_quality_;
    pending_quality_.clear();
    QSignalBlocker block(quality_);
    quality_->clear();
    quality_keys_ = {"hq", "sq", "mq"};
    auto add = [&](const char *key, const char *kbps) {
        quality_->addItem(kbps ? QString("%1 (%2 kbps)").arg(U(key), kbps) : U(key));
    };
    switch (codec_idx) {
    case 1: add("quality.high", "990"); add("quality.standard", "660"); add("quality.mobile", "330"); break;
    case 6: add("quality.high", "256"); add("quality.standard", "192"); add("quality.mobile", "128"); break;
    case 5: add("quality.high", "~345"); add("quality.standard", "~240"); add("quality.mobile", "~150"); break;
    case 7: /* LHDC V5: Android's four fixed levels (the sink may cap the maximum) */
        quality_keys_ = {"hq", "mhq", "sq", "mq"};
        add("quality.high", "1000"); add("quality.medium_high", "900");
        add("quality.standard", "500"); add("quality.mobile", "400");
        break;
    default: add("quality.high", nullptr); add("quality.standard", nullptr); add("quality.mobile", nullptr); break;
    }
    /* Keep the same quality; one the new codec lacks ("mhq") falls back to High */
    int sel = 0;
    for (size_t i = 0; i < quality_keys_.size(); i++)
        if (quality_keys_[i] == cur_key) { sel = static_cast<int>(i); break; }
    quality_->setCurrentIndex(sel);
    quality_->setEnabled(quality_enabled);

    /* Codecs without ABR: hide the row instead of greying it out */
    set_row_visible(abr_, abr_enabled);
    if (!abr_enabled) abr_->setChecked(false);
    /* Auto already takes the best codec the device offers */
    fallback_->setEnabled(codec_idx != 0);

    /* Sample rate options */
    {
        QSignalBlocker b(sample_rate_);
        const int cur = sample_rate_->currentIndex();
        sample_rate_->clear();
        sample_rate_->addItem(U("codec.auto"));
        sample_rate_->addItem("44100 Hz");
        sample_rate_->addItem("48000 Hz");
        if (is_ldac || is_lhdc) { /* LHDC V5 has no 88.2 kHz: that choice is streamed at 96 kHz */
            sample_rate_->addItem("88200 Hz");
            sample_rate_->addItem("96000 Hz");
        }
        sample_rate_->setCurrentIndex(cur >= 0 && cur < sample_rate_->count() ? cur : 0);
    }

    /* Bit depth */
    {
        QSignalBlocker b(bit_depth_);
        const bool bd_enabled = codec_idx == 1 || is_lhdc;
        const int cur = bit_depth_->currentIndex();
        bit_depth_->clear();
        if (bd_enabled) {
            bit_depth_->addItem(U("codec.auto"));
            bit_depth_->addItem(U("bitdepth.16"));
            bit_depth_->addItem(U("bitdepth.24"));
        } else {
            bit_depth_->addItem(U("bitdepth.16"));
        }
        bit_depth_->setEnabled(bd_enabled);
        bit_depth_->setCurrentIndex(cur >= 0 && cur < bit_depth_->count() ? cur : 0);
    }

    /* Capture-mode rows */
    const bool virtual_mode = capture_->currentIndex() == 1;
    set_row_visible(audio_dev_, virtual_mode);
    set_row_visible(auto_switch_, virtual_mode);
    if (virtual_mode && audio_dev_->count() == 0) {
        AudioDeviceEnumerator enumerator;
        if (enumerator.init()) {
            audio_devices_ = enumerator.get_virtual_devices();
            enumerator.shutdown();
        }
        QSignalBlocker b(audio_dev_);
        for (auto &d : audio_devices_) audio_dev_->addItem(U8(d.display_name));
        if (!audio_devices_.empty()) audio_dev_->setCurrentIndex(0);
    }
    const bool app_mode = capture_->currentIndex() == 2;
    set_row_visible(app_row_, app_mode);
    set_row_visible(app_note_, app_mode);
    if (app_mode && app_->count() == 0) refresh_app_list();

    update_capture_hint();
    update_format_info();
}

void ProfileDialog::update_format_info() {
    const int mode = capture_->currentIndex();
    /* Application capture: Windows converts to the requested rate */
    set_row_visible(format_info_, mode != 2);
    if (mode == 2) return;

    AudioDeviceFormat fmt_dev;
    if (mode == 1) {
        const int sel = audio_dev_->currentIndex();
        if (sel >= 0 && sel < static_cast<int>(audio_devices_.size()))
            fmt_dev = AudioDeviceEnumerator::get_device_format(audio_devices_[sel].id);
    } else {
        fmt_dev = AudioDeviceEnumerator::get_device_format(); /* default device */
    }
    if (!fmt_dev.valid) {
        format_info_->clear();
        return;
    }
    const int sr_sel = sample_rate_->currentIndex(), bd_sel = bit_depth_->currentIndex();
    const uint32_t profile_sr = (sr_sel >= 0 && sr_sel <= 4) ? RATE_VALUES[sr_sel] : 0;
    const uint32_t profile_bd = (bd_sel >= 0 && bd_sel <= 2) ? BIT_VALUES[bd_sel] : 0;
    /* Auto (0) never mismatches */
    const bool mismatch = (profile_sr != 0 && profile_sr != fmt_dev.sample_rate) ||
                          (profile_bd != 0 && profile_bd != fmt_dev.bits_per_sample);
    format_info_->setText(fmt("capture.device_format", fmt_dev.sample_rate, fmt_dev.bits_per_sample));
    format_info_->setProperty("muted", !mismatch);
    format_info_->setProperty("error", mismatch);
    format_info_->style()->unpolish(format_info_);
    format_info_->style()->polish(format_info_);
}

void ProfileDialog::update_capture_hint() {
    static const char *hint_keys[] = {"capture.hint_loopback", "capture.hint_virtual", "capture.hint_app"};
    int mode = capture_->currentIndex();
    if (mode < 0 || mode > 2) mode = 0;
    capture_hint_->setText(U(hint_keys[mode]));
    if (mode != 2) return;

    /* Application mode: say plainly when this Windows cannot do it */
    const bool supported = app_audio::capture_supported();
    app_->setEnabled(supported);
    app_refresh_->setEnabled(supported);
    app_note_->setText(supported ? U("capture.app_hint") : fmt("capture.app_unsupported", app_audio::windows_build()));
    app_note_->setProperty("muted", supported);
    app_note_->setProperty("error", !supported);
    app_note_->style()->unpolish(app_note_);
    app_note_->style()->polish(app_note_);
}

void ProfileDialog::refresh_app_list() {
    /* Keep the current choice, else the one saved in the profile */
    std::string keep = saved_app_exe_;
    const int cur = app_->currentIndex();
    if (cur >= 0 && cur < static_cast<int>(apps_.size())) keep = apps_[cur].exe_name;

    apps_ = app_audio::list_audio_apps();
    bool found = false;
    for (const auto &a : apps_)
        if (_stricmp(a.exe_name.c_str(), keep.c_str()) == 0) { found = true; break; }
    if (!keep.empty() && !found) {
        /* Saved app that is not playing (or not running) right now */
        AudioAppInfo saved;
        saved.exe_name = keep;
        saved.display_name = keep == saved_app_exe_ && !saved_app_name_.empty() ? saved_app_name_ : keep;
        apps_.insert(apps_.begin(), saved);
    }

    app_->clear();
    int select = apps_.empty() ? -1 : 0;
    for (size_t i = 0; i < apps_.size(); i++) {
        QString label = U8(apps_[i].display_name);
        if (i == 0 && !keep.empty() && !found) label = fmt("capture.app_not_playing", label);
        app_->addItem(label);
        if (_stricmp(apps_[i].exe_name.c_str(), keep.c_str()) == 0) select = static_cast<int>(i);
    }
    if (select >= 0) app_->setCurrentIndex(select);
}

/* ======================================================================== */
/* Device list                                                               */
/* ======================================================================== */

void ProfileDialog::refresh_device_list() {
    device_list_->clear();
    for (const auto &d : backend_->service().get_devices()) {
        device_list_->addItem(QString("[%1] %2 (%3)")
                                  .arg(U(d.saved ? "device.tag_saved" : "device.tag_audio"),
                                       d.name.empty() ? U("device.name_unknown") : U8(d.name), U8(d.addr_str)));
    }
}

void ProfileDialog::on_device_selected() {
    const int sel = device_list_->currentRow();
    if (sel < 0) {
        dev_edit_btn_->setEnabled(false);
        dev_del_btn_->setEnabled(false);
        return;
    }
    const auto devices = backend_->service().get_devices();
    if (sel < static_cast<int>(devices.size())) {
        const auto &d = devices[sel];
        set_address(U8(d.addr_str));
        devname_->setText(U8(d.name));
        /* Edit / delete only for saved (paired) devices */
        dev_edit_btn_->setEnabled(d.saved);
        dev_del_btn_->setEnabled(d.saved);
    }
}

void ProfileDialog::on_edit_name() {
    const int sel = device_list_->currentRow();
    const auto devices = backend_->service().get_devices();
    if (sel < 0 || sel >= static_cast<int>(devices.size()) || !devices[sel].saved) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, U("device.edit_name_title"), U("device.enter_name"),
                                               QLineEdit::Normal, U8(devices[sel].name), &ok);
    if (!ok) return;
    backend_->service().save_device(devices[sel].addr_str.c_str(), name.toStdString());
    devname_->setText(name);
    refresh_device_list();
}

void ProfileDialog::on_delete_device() {
    const int sel = device_list_->currentRow();
    const auto devices = backend_->service().get_devices();
    if (sel < 0 || sel >= static_cast<int>(devices.size()) || !devices[sel].saved) return;
    backend_->service().delete_saved_device(sel);
    refresh_device_list();
    dev_edit_btn_->setEnabled(false);
    dev_del_btn_->setEnabled(false);
}

void ProfileDialog::on_scan() {
    scan_btn_->setText(U("connection.scanning"));
    scan_btn_->setEnabled(false);
    backend_->service().start_scan(); /* Backend::scan_completed refreshes the list */
}

/* ======================================================================== */
/* Address entry                                                             */
/* ======================================================================== */

/* A value from a profile or the device list: kept even if it is not an
 * address, so it shows in red instead of being dropped */
void ProfileDialog::set_address(const QString &addr) {
    last_addr_ = addr;
    addr_->setText(addr);
    QString f;
    int c = 0;
    const bool invalid = !addr.isEmpty() && !format_address_input(addr, 0, f, c);
    addr_->setProperty("invalid", invalid);
    addr_->style()->unpolish(addr_);
    addr_->style()->polish(addr_);
}

void ProfileDialog::on_addr_edited(const QString &value) {
    const int caret_in = addr_->cursorPosition();
    QString formatted;
    int caret = 0;
    if (format_address_input(value, caret_in, formatted, caret)) {
        if (formatted != value) {
            addr_->setText(formatted);
            addr_->setCursorPosition(caret);
        }
        last_addr_ = formatted;
    } else if (!format_address_input(last_addr_, 0, formatted, caret)) {
        /* Already invalid (set from elsewhere): let it be edited freely */
        last_addr_ = value;
    } else if (value != last_addr_) {
        /* A character that is not part of an address, or a 13th digit: refuse
         * the edit and put the caret back where the input began */
        const int added = value.size() - last_addr_.size();
        int restore = caret_in - (added > 0 ? added : 0);
        restore = qBound(0, restore, static_cast<int>(last_addr_.size()));
        addr_->setText(last_addr_);
        addr_->setCursorPosition(restore);
        QApplication::beep();
    }
    QString f;
    int c = 0;
    const bool invalid = !addr_->text().isEmpty() && !format_address_input(addr_->text(), 0, f, c);
    addr_->setProperty("invalid", invalid);
    addr_->style()->unpolish(addr_);
    addr_->style()->polish(addr_);
}

/* ======================================================================== */

void ProfileDialog::on_save() {
    QString addr = addr_->text().trimmed();
    if (addr.isEmpty()) return;

    std::string canonical;
    if (!BtDeviceDiscovery::normalize_address(ascii_from_fullwidth(addr).toStdString(), canonical)) {
        QMessageBox::warning(this, U("error.invalid_address"), U("error.invalid_address_format"));
        addr_->setFocus();
        return;
    }
    addr_->setText(U8(canonical));

    ConnectionProfile p;
    p.device_address = canonical;
    p.device_name = devname_->text().toStdString();

    /* Internal name from device + codec */
    static const char *codec_short[] = {"Auto", "LDAC", "aptX HD", "aptX LL", "aptX", "SBC", "AAC", "LHDC V5"};
    std::string auto_name = p.device_name.empty() ? p.device_address : p.device_name;
    const int ci = codec_->currentIndex();
    if (ci >= 0 && ci < (int)(sizeof(codec_short) / sizeof(codec_short[0]))) {
        auto_name += " ";
        auto_name += codec_short[ci];
    }
    p.name = auto_name;
    p.codec = ProfileManager::index_to_codec(ci);
    p.quality = selected_quality();
    p.abr = abr_->isChecked();
    p.codec_fallback = fallback_->isChecked();

    const int sr_sel = sample_rate_->currentIndex();
    p.sample_rate = (sr_sel >= 0 && sr_sel <= 4) ? RATE_VALUES[sr_sel] : 0;
    const int bd_sel = bit_depth_->currentIndex();
    p.bit_depth = (bd_sel >= 0 && bd_sel <= 2) ? BIT_VALUES[bd_sel] : 0;

    const int mode = capture_->currentIndex();
    p.capture_mode = ProfileManager::index_to_capture_mode(mode);
    if (mode == 1) {
        const int dev_sel = audio_dev_->currentIndex();
        if (dev_sel >= 0 && dev_sel < static_cast<int>(audio_devices_.size())) {
            p.audio_device_id = wide_to_utf8(audio_devices_[dev_sel].id);
            p.audio_device_name = audio_devices_[dev_sel].display_name;
        }
    }
    if (mode == 2) {
        if (!app_audio::capture_supported()) {
            QMessageBox::warning(this, U("capture.app"), fmt("error.app_capture_unsupported", app_audio::windows_build()));
            return;
        }
        const int app_sel = app_->currentIndex();
        if (app_sel < 0 || app_sel >= static_cast<int>(apps_.size())) {
            QMessageBox::warning(this, U("capture.app"), U("error.no_app_selected"));
            return;
        }
        p.app_exe = apps_[app_sel].exe_name;
        p.app_name = apps_[app_sel].display_name;
    }
    p.auto_switch_device = auto_switch_->isChecked();

    const int vol_sel = volume_sync_->currentIndex();
    if (vol_sel >= 2 && vol_sel - 2 < static_cast<int>(volume_devices_.size())) {
        p.volume_sync = "device";
        p.volume_device_id = wide_to_utf8(volume_devices_[vol_sel - 2].id);
        p.volume_device_name = volume_devices_[vol_sel - 2].display_name;
    } else {
        p.volume_sync = vol_sel == 1 ? "off" : "auto";
    }
    p.max_media_payload = clamp_media_payload_limit(static_cast<uint32_t>(max_packet_->value()));

    if (edit_index_ >= 0) backend_->profiles().update(edit_index_, p);
    else backend_->profiles().add(p);
    accept();
}
