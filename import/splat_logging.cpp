/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "splat_logging.h"

void (*send_log)(Level level, const char* message);

void set_log_recv(void (*recv_log)(Level level, const char* message)) {
  send_log = recv_log;
}

void log(Level level, const char* format, va_list args) {
  if (!send_log) {
    return;
  }

  int size = vsnprintf(nullptr, 0, format, args);
  std::string buffer("", size + 1);
  vsnprintf(buffer.data(), buffer.size(), format, args);
  send_log(level, buffer.c_str());
}

void log_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list args2;
  va_copy(args2, args);
  va_end(args);

  log(Level::ERROR, format, args2);

  va_end(args2);
}

void log_warn(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list args2;
  va_copy(args2, args);
  va_end(args);

  log(Level::WARNING, format, args2);

  va_end(args2);
}