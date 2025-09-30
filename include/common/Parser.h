#pragma once

class Parser {
public:
  Parser(int argc, char *argv[]) { parse(argc, argv); };

  void print_help(const char *progname);

private:
  int parse(int argc, char *argv[]);
};