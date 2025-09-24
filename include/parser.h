#pragma once

class Parser {
public:
  int parse(int argc, char *argv[]);
  void print_help(const char *progname);
};