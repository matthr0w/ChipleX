#pragma once

#include <iomanip>
#include <iostream>
#include <systemc.h>

#include "globals.h"

#define SC_LOG_INFO(module, ...)                                               \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << "\033[0m[INFO]\033[0m  | " << std::left << std::setw(16)   \
                 << sc_time_stamp() << " | " << std::setw(38)                  \
                 << (module)->name() << " | " << __VA_ARGS__;                  \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_WARN(module, ...)                                               \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << "\033[33m[WARN]\033[0m  | " << std::left << std::setw(16)  \
                 << sc_time_stamp() << " | " << std::setw(38)                  \
                 << (module)->name() << " | " << __VA_ARGS__;                  \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_ERROR(module, ...)                                              \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)  \
                 << sc_time_stamp() << " | " << std::setw(38)                  \
                 << (module)->name() << " | " << __VA_ARGS__;                  \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_DEBUG(module, ...)                                              \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << "\033[34m[DEBUG]\033[0m | " << std::left << std::setw(16)  \
                 << sc_time_stamp() << " | " << std::setw(38)                  \
                 << (module)->name() << " | " << __VA_ARGS__;                  \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_DELAY(module, type, value)                                      \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << "\033[32m[DELAY]\033[0m | " << std::left << std::setw(16)  \
                 << sc_time_stamp() << " | " << std::setw(38)                  \
                 << (module)->name() << " | " << type << ": " << value;        \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)
