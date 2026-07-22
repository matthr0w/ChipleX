#pragma once

#include <iomanip>
#include <systemc.h>

#include "globals.h"

#define LOG_INFO(...)                                                          \
  do {                                                                         \
    if (log_level <= LogLevel::INFO) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[0m[INFO]\033[0m  | " << std::left << std::setw(16)      \
              << sc_time_stamp() << " | " << __VA_ARGS__;                      \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define LOG_WARN(...)                                                          \
  do {                                                                         \
    if (log_level <= LogLevel::WARN) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[33m[WARN]\033[0m  | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << __VA_ARGS__;                      \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

// Errors and failed assertions always throw (halting the offending process),
// even under SILENT; only the diagnostic message is suppressed by log level.
// Previously the throw was gated by the log level, so under SILENT an error was
// a complete no-op and execution fell through to undefined behavior (e.g.
// indexing an array with a -1 "no route" id). sc_main catches the exception and
// shuts down cleanly (see main.cpp).
#define LOG_ASSERT(assertion, ...)                                             \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      if (log_level <= LogLevel::ERROR) {                                      \
        std::ostringstream _stream;                                            \
        _stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)   \
                << sc_time_stamp() << " | " << __VA_ARGS__;                    \
        std::cout << _stream.str() << std::endl;                               \
      }                                                                        \
      throw std::runtime_error("Assertion failed");                           \
    }                                                                          \
  } while (0)

#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    if (log_level <= LogLevel::ERROR) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << __VA_ARGS__;                      \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
    throw std::runtime_error("Simulation error");                             \
  } while (0)

#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[34m[DEBUG]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << __VA_ARGS__;                      \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define SC_LOG_INFO(module, ...)                                               \
  do {                                                                         \
    if (log_level <= LogLevel::INFO) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[0m[INFO]\033[0m  | " << std::left << std::setw(16)      \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << __VA_ARGS__;                                         \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define SC_LOG_WARN(module, ...)                                               \
  do {                                                                         \
    if (log_level <= LogLevel::WARN) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[33m[WARN]\033[0m  | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << __VA_ARGS__;                                         \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define SC_LOG_ASSERT(module, assertion, ...)                                  \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      if (log_level <= LogLevel::ERROR) {                                      \
        std::ostringstream _stream;                                            \
        _stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)   \
                << sc_time_stamp() << " | " << std::setw(40)                   \
                << (module)->name() << " | " << __VA_ARGS__;                   \
        std::cout << _stream.str() << std::endl;                               \
      }                                                                        \
      throw std::runtime_error("Assertion failed");                           \
    }                                                                          \
  } while (0)

#define SC_LOG_ERROR(module, ...)                                              \
  do {                                                                         \
    if (log_level <= LogLevel::ERROR) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << __VA_ARGS__;                                         \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
    throw std::runtime_error("Simulation error");                             \
  } while (0)

#define SC_LOG_DEBUG(module, ...)                                              \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[34m[DEBUG]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << __VA_ARGS__;                                         \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define SC_LOG_DELAY(module, type, value)                                      \
  do {                                                                         \
    if (log_level <= LogLevel::DELAY) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[32m[DELAY]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << type << ": " << value;                              \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)
