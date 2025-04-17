#pragma once

#include <systemc.h>

#include <iomanip>
#include <iostream>

#include "payload_extension.h"

extern bool debug_msgs;

#define SC_LOG_DEBUG(module, ...)                                              \
  do {                                                                         \
    if (debug_msgs) {                                                          \
      std::cout << std::setw(9) << sc_time_stamp()                             \
                << " | \033[34m[DEBUG]\033[0m  | " << std::left                \
                << std::setw(25) << (module)->name() << " | " << __VA_ARGS__   \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

#define SC_LOG_INFO(module, ...)                                               \
  do {                                                                         \
    std::cout << std::setw(9) << sc_time_stamp()                               \
              << " | \033[0m[INFO]\033[0m   | " << std::left << std::setw(25)  \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_WARN(module, ...)                                               \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[33m[WARN]\033[0m   | " << std::left << std::setw(25) \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_ERROR(module, ...)                                              \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[31m[ERROR]\033[0m  | " << std::left << std::setw(25) \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_DELAY(type, value)                                              \
  do {                                                                         \
    std::cout << std::setw(9) << sc_time_stamp()                               \
              << " | \033[32m[DELAY]\033[0m  | " << std::left << std::setw(25) \
              << type << " | " << value << std::endl;                          \
  } while (0)

#define SC_DUMP_TRANS(module, transaction)                                     \
  do {                                                                         \
    const auto *ext = (transaction).get_extension<payload_extension>();        \
    std::ostringstream _sc_dump_trans_stream;                                  \
    _sc_dump_trans_stream << std::setw(9) << sc_time_stamp()                   \
                          << " | \033[35m[TRACE]\033[0m  | " << std::left      \
                          << std::setw(25) << (module)->name() << " | ";       \
    _sc_dump_trans_stream                                                      \
        << "ADDR: 0x" << std::hex << (transaction).get_address() << " CMD: "   \
        << ((transaction).get_command() == tlm::TLM_READ_COMMAND ? "READ"      \
                                                                 : "WRITE")    \
        << " LEN: " << std::dec << (transaction).get_data_length();            \
    if (ext) {                                                                 \
      _sc_dump_trans_stream << " | Request: " << ext->request_id               \
                            << " Source: " << ext->source_id                   \
                            << " Core: " << ext->core_id                       \
                            << " Destination: " << ext->destination_id;        \
    }                                                                          \
    std::cerr << _sc_dump_trans_stream.str() << std::endl;                     \
  } while (0)
