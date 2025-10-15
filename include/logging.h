#pragma once

#include <iomanip>
#include <systemc.h>

#include "globals.h"

#define LOG_INFO(...)                                                          \
  do {                                                                         \
    if (log_level <= LogLevel::INFO) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[0m[INFO]\033[0m  | " << __VA_ARGS__;                    \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define LOG_WARN(...)                                                          \
  do {                                                                         \
    if (log_level <= LogLevel::WARN) {                                         \
      std::ostringstream _stream;                                              \
      _stream << "\033[33m[WARN]\033[0m  | " << __VA_ARGS__;                   \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)

#define LOG_ASSERT(assertion, ...)                                             \
  do {                                                                         \
    if (log_level <= LogLevel::ERROR && !(assertion)) {                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[31m[ERROR]\033[0m | " << __VA_ARGS__;                   \
      std::cout << _stream.str() << std::endl;                                 \
      throw std::runtime_error("");                                            \
    }                                                                          \
  } while (0)

#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    if (log_level <= LogLevel::ERROR) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[31m[ERROR]\033[0m | " << __VA_ARGS__;                   \
      std::cout << _stream.str() << std::endl;                                 \
      throw std::runtime_error("");                                            \
    }                                                                          \
  } while (0)

#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[34m[DEBUG]\033[0m | " << __VA_ARGS__;                   \
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
    if (log_level <= LogLevel::ERROR && !(assertion)) {                        \
      std::ostringstream _stream;                                              \
      _stream << "\033[31m[ERROR]\033[0m | " << std::left << std::setw(16)     \
              << sc_time_stamp() << " | " << std::setw(40) << (module)->name() \
              << " | " << __VA_ARGS__;                                         \
      std::cout << _stream.str() << std::endl;                                 \
      throw std::runtime_error("");                                            \
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
      throw std::runtime_error("");                                            \
    }                                                                          \
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
              << " | " << type << ": " << value;                               \
      std::cout << _stream.str() << std::endl;                                 \
    }                                                                          \
  } while (0)
