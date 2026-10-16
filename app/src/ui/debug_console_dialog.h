/*
 * Debug console (Debug menu) - connection flow checklist, live debug.log view
 * with filters, and clipboard copies for bug reports
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_DEBUG_CONSOLE_DIALOG_H
#define UI_DEBUG_CONSOLE_DIALOG_H

#include "debug_log_model.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class Backend;
class LogTableModel;
class MainWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QTableWidget;
class QTimer;

/* Modeless; open with DebugConsoleDialog::show_for() so only one exists */
class DebugConsoleDialog : public QDialog {
    Q_OBJECT
public:
    static void show_for(MainWindow *window, DebugLogModel *model);

private:
    DebugConsoleDialog(MainWindow *window, DebugLogModel *model);

    void on_timer();
    void rebuild_filter();
    void append_new();
    bool matches(const DebugLogModel::Entry &e) const;
    void refresh_flow();
    void update_count();
    void jump_to(uint64_t seq);

    /* Copies */
    QString format_line(const DebugLogModel::Entry &e) const;
    QString flow_text() const;
    QString diagnostics_text() const;
    void copy_selected_lines();
    void copy_shown_lines();
    void copy_to_clipboard(const QString &text, int lines);

    /* Flow row as shown (status may be inferred from the service state) */
    struct FlowRow {
        DebugLogModel::StepStatus status;
        QString time, elapsed, detail;
    };
    FlowRow flow_row(int step, bool english = false) const;

    Backend *backend_;
    DebugLogModel *model_;
    QTimer *timer_;

    QLabel *session_label_;
    QTableWidget *flow_list_;
    QComboBox *level_choice_;
    QLineEdit *search_;
    QCheckBox *session_only_, *autoscroll_, *mask_addr_;
    QLabel *count_label_, *feedback_label_;
    QTableView *log_view_;
    LogTableModel *log_model_;

    /* Seqs of the entries shown, ascending */
    std::vector<uint64_t> shown_;
    uint64_t seen_seq_ = 0;   /* last model seq considered */
    uint64_t seen_first_ = 0; /* model first_seq at the last rebuild */
    uint32_t seen_session_ = 0;
    QString query_lower_;

    /* Live packet and capture rates for the audio step */
    uint64_t prev_tick_ = 0, prev_packets_ = 0, prev_bytes_ = 0, prev_capture_ = 0;
    double pps_ = -1.0, kbps_ = 0.0, capture_fps_ = 0.0;

    friend class LogTableModel;
};

#endif /* UI_DEBUG_CONSOLE_DIALOG_H */
