/*
 * Backend implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "backend.h"

#include "btstack_transport.h"
#include "localization.h"

#include <QApplication>
#include <QMetaObject>
#include <QPointer>

#include <windows.h>

Backend::Backend(QObject *parent) : QObject(parent) {

    settings_.load();
    /* A language this build lacks (settings from a newer version) shows in English */
    if (!Localization::instance().load(settings_.language)) Localization::instance().load("en");
    profiles_.load(settings_.legacy_max_media_payload);
    service_.load_saved_devices();
    service_.set_bt_chip_pid(settings_.bt_chip_pid);
    service_.set_bt_chip_fw_stem(settings_.bt_chip_fw_stem);
    BtStackTransport::set_afh_policy(settings_.afh);
    BtStackTransport::set_media_keys_enabled(settings_.media_keys);
    service_.set_remember_volume(settings_.remember_volume);
    service_.set_saved_volumes(settings_.saved_volumes);
    service_.set_debug_mode(settings_.debug_mode);
    service_.check_firmware_present();

    /* Worker threads -> GUI thread. The QPointer guards the window of time
     * between this object's destruction starting and the service stopping. */
    QPointer<Backend> self(this);
    service_.set_state_callback([self](A2dpService::State s, const std::string &text) {
        QMetaObject::invokeMethod(qApp, [self, s, text] {
            if (!self) return;
            self->state_ = s;
            self->status_text_ = text;
            /* A new connection gets new stream info; do not show the last one's codec */
            if (s == A2dpService::State::Connecting) self->info_ = {};
            emit self->state_changed();
        }, Qt::QueuedConnection);
    });
    service_.set_stream_info_callback([self](const A2dpService::StreamInfo &info) {
        QMetaObject::invokeMethod(qApp, [self, info] {
            if (!self) return;
            self->info_ = info;
            emit self->stream_info_changed();
        }, Qt::QueuedConnection);
    });
    service_.set_scan_complete_callback([self] {
        QMetaObject::invokeMethod(qApp, [self] {
            if (self) emit self->scan_completed();
        }, Qt::QueuedConnection);
    });
}

Backend::~Backend() {
    service_.stop_streaming();
    save_remembered_volumes(true);
}

void Backend::start_profile(int index) {
    if (index < 0 || index >= static_cast<int>(profiles_.profiles().size())) return;
    auto state = service_.state();
    if (state == A2dpService::State::Streaming || state == A2dpService::State::Connecting)
        service_.stop_streaming();
    selected_ = index;
    const ConnectionProfile &p = profiles_.profiles()[index];
    settings_.last_profile = p.name;
    settings_.save();
    service_.start_streaming(p);
    emit state_changed(); /* the selection changed */
}

void Backend::start_stream(const ConnectionProfile &profile) {
    auto state = service_.state();
    if (state == A2dpService::State::Streaming || state == A2dpService::State::Connecting)
        service_.stop_streaming();
    selected_ = -1;
    service_.start_streaming(profile);
    emit state_changed();
}

void Backend::profile_removed(int index) {
    if (selected_ == index) selected_ = -1;
    else if (selected_ > index) selected_--;
}

void Backend::save_remembered_volumes(bool now) {
    if (service_.saved_volumes_changed()) {
        settings_.saved_volumes = service_.saved_volumes();
        volumes_changed_tick_ = GetTickCount();
        if (volumes_changed_tick_ == 0) volumes_changed_tick_ = 1;
    }
    /* Wait until the volume settles, not a write per slider step */
    if (volumes_changed_tick_ && (now || GetTickCount() - volumes_changed_tick_ >= 2000)) {
        settings_.save();
        volumes_changed_tick_ = 0;
    }
}

