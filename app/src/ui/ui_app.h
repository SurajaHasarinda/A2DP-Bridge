/*
 * GUI entry point (replaces the wxWidgets application object)
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_APP_H
#define UI_APP_H

namespace ui {

/* Name of the window message a second instance broadcasts to bring the first
 * one forward (see main.cpp). */
extern const wchar_t *const kActivateMessage;

/* Runs the Qt GUI until the window is closed; returns the process exit code */
int run_gui(int argc, char *argv[], bool start_minimized);

} // namespace ui

#endif /* UI_APP_H */
