#include "router.h"
#include "system.h"

int main() {
  SystemLoader sysloader("system.yaml", "./configs");
  sysloader.print_config();
  Router::instance().init(sysloader.get_config());
  Router::instance().print_table();
  return 0;
}