#pragma once

#include <string>
#include <systemc>

#include "globals.h"

using namespace sc_core;

class Parser {
public:
  int parse(int argc, char *argv[]);
  void print_help(const char *progname);
  void print_args();

private:
  bool parse_connection_list(const std::string &arg);
  ConnectionType parse_connection_type(const std::string &value);
};