#include "system.h"

int main() {
  SystemLoader loader("system.yaml", "./configs");
  loader.print_system_config();
  return 0;
}