/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include <cstdarg>
#include <format>
#include <string>

enum Level { ERROR, WARNING };

extern void (*send_log)(Level level, const char* message);
SPLAT_EXPORT_API void set_log_recv(void (*recv_log)(Level level,
                                                    const char* message));

void log_error(const char* format, ...);
void log_warn(const char* format, ...);