/*
 * Backend - owns the streaming service, profiles and settings, and turns the
 * service's worker-thread callbacks into queued Qt signals
 *
 * Windows connect to these signals, so they stop receiving when destroyed
 * (the service keeps one callback slot; it never points at a window).
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_BACKEND_H
#define UI_BACKEND_H

#include "a2dp_service.h"
#include "app_settings.h"
#include "profile_manager.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>


class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    A2dpService &service() { return service_; }
    ProfileManager &profiles() { return profiles_; }
    AppSettings &settings() { return settings_; }

    A2dpService::State state() const { return state_; }
    const std::string &status_text() const { return status_text_; }
    const A2dpService::StreamInfo &stream_info() const { return info_; }

    /* Index into profiles() of the one streaming or last started, -1 = none */
    int selected_profile() const { return selected_; }
    /* Connect with a profile of the list (replaces a running stream) */
    void start_profile(int index);
    /* Connect with a profile that is not in the list (the receiver measurement) */
    void start_stream(const ConnectionProfile &profile);
    /* After the list changed (add / remove): keep the selection valid */
    void profile_removed(int index);

    /* Remember volumes: save them once they settle (called by a timer) */
    void save_remembered_volumes(bool now);


signals:
    void state_changed();
    void stream_info_changed();
    void scan_completed();

private:
    A2dpService service_;
    ProfileManager profiles_;
    AppSettings settings_;

    A2dpService::State state_ = A2dpService::State::Idle;
    std::string status_text_ = "Ready";
    A2dpService::StreamInfo info_{};
    int selected_ = -1;

    unsigned long volumes_changed_tick_ = 0;
};

#endif /* UI_BACKEND_H */
