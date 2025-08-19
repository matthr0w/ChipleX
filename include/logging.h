#pragma once

#include <iomanip>
#include <iostream>
#include <systemc.h>

#include "globals.h"

#include "common/protocol/ChipletExtension.h"

#define SC_LOG_INFO(module, transaction, ...)                                  \
  do {                                                                         \
    if (log_level <= LogLevel::INFO) {                                         \
      const auto *ext = (transaction).get_extension<ChipletExtension>();       \
      std::ostringstream _sc_stream, _sc_idstream;                             \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[0m[INFO]\033[0m   | " << std::setw(32)            \
                 << (module)->name() << " | ";                                 \
      if (ext) {                                                               \
        _sc_idstream << ext->request_id << "/" << ext->source_id << "/"        \
                     << ext->core_id << "/" << ext->destination_id;            \
      } else {                                                                 \
        _sc_idstream << "no-extension";                                        \
      }                                                                        \
      _sc_stream << std::setw(25) << _sc_idstream.str();                       \
      _sc_stream << " | " << __VA_ARGS__;                                      \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_WARN(module, transaction, ...)                                  \
  do {                                                                         \
    if (log_level <= LogLevel::WARN) {                                         \
      const auto *ext = (transaction).get_extension<ChipletExtension>();       \
      std::ostringstream _sc_stream, _sc_idstream;                             \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[33m[WARN]\033[0m   | " << std::setw(32)           \
                 << (module)->name() << " | ";                                 \
      if (ext) {                                                               \
        _sc_idstream << ext->request_id << "/" << ext->source_id << "/"        \
                     << ext->core_id << "/" << ext->destination_id;            \
      } else {                                                                 \
        _sc_idstream << "no-extension";                                        \
      }                                                                        \
      _sc_stream << std::setw(25) << _sc_idstream.str();                       \
      _sc_stream << " | " << __VA_ARGS__;                                      \
      std::cerr << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_ERROR(module, transaction, ...)                                 \
  do {                                                                         \
    if (log_level <= LogLevel::ERROR) {                                        \
      const auto *ext = (transaction).get_extension<ChipletExtension>();       \
      std::ostringstream _sc_stream, _sc_idstream;                             \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[31m[ERROR]\033[0m  | " << std::setw(32)           \
                 << (module)->name() << " | ";                                 \
      if (ext) {                                                               \
        _sc_idstream << ext->request_id << "/" << ext->source_id << "/"        \
                     << ext->core_id << "/" << ext->destination_id;            \
      } else {                                                                 \
        _sc_idstream << "no-extension";                                        \
      }                                                                        \
      _sc_stream << std::setw(25) << _sc_idstream.str();                       \
      _sc_stream << " | " << __VA_ARGS__;                                      \
      std::cerr << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_DEBUG(module, transaction, ...)                                 \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      const auto *ext = (transaction).get_extension<ChipletExtension>();       \
      std::ostringstream _sc_stream, _sc_idstream;                             \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[34m[DEBUG]\033[0m  | " << std::setw(32)           \
                 << (module)->name() << " | ";                                 \
                                                                               \
      if (ext) {                                                               \
        _sc_idstream << ext->request_id << "/" << ext->source_id << "/"        \
                     << ext->core_id << "/" << ext->destination_id;            \
      } else {                                                                 \
        _sc_idstream << "no-extension";                                        \
      }                                                                        \
                                                                               \
      _sc_stream << std::setw(25) << _sc_idstream.str();                       \
      _sc_stream << " | " << __VA_ARGS__;                                      \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_DEBUG_NO_TX(module, ...)                                        \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      std::ostringstream _sc_stream;                                           \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[34m[DEBUG]\033[0m  | " << std::setw(32)           \
                 << (module)->name() << " | " << __VA_ARGS__;                  \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)

#define SC_LOG_DELAY(module, transaction, type, value)                         \
  do {                                                                         \
    if (log_level <= LogLevel::DEBUG) {                                        \
      const auto *ext = (transaction).get_extension<ChipletExtension>();       \
      std::ostringstream _sc_stream, _sc_idstream;                             \
      _sc_stream << std::left << std::setw(16) << sc_time_stamp()              \
                 << " | \033[32m[DELAY]\033[0m  | " << std::setw(32)           \
                 << (module)->name() << " | ";                                 \
      if (ext) {                                                               \
        _sc_idstream << ext->request_id << "/" << ext->source_id << "/"        \
                     << ext->core_id << "/" << ext->destination_id;            \
      } else {                                                                 \
        _sc_idstream << "no-extension";                                        \
      }                                                                        \
      _sc_stream << std::setw(25) << _sc_idstream.str();                       \
      _sc_stream << " | " << type << ": " << value;                            \
      std::cout << _sc_stream.str() << std::endl;                              \
    }                                                                          \
  } while (0)
