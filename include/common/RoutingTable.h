#pragma once

#include <vector>

class RoutingTable {
public:
  static void initialize(unsigned int num_chiplets);
  static int get_route(unsigned int from, unsigned int to);
  static int get_destination(unsigned int from, unsigned int via);

private:
  static std::vector<std::vector<int>> table;
};