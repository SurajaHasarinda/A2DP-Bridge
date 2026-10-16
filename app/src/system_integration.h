/*
 * System Integration - Windows startup
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef SYSTEM_INTEGRATION_H
#define SYSTEM_INTEGRATION_H

#include <windows.h>
#include <string>

/* Windows Startup */
bool RegisterStartup(const std::string &args = "--minimized");
bool UnregisterStartup();
bool IsStartupRegistered();

/* Keep Windows from throttling this process (EcoQoS, coarse timers) while
 * it runs in the background or tray, e.g. behind a game */
void DisablePowerThrottling();

/* Utility */
std::wstring GetExePathW();

#endif /* SYSTEM_INTEGRATION_H */
