#include "app/application.h"
#include "core/application_identity.h"

#include <cstring>
#include <print>

int main(int argc, char* argv[]) {
  setApplicationIdentity({.name = "tandem", .envPrefix = "TANDEM"});
  bool service = false;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--service") == 0) {
      service = true;
    } else {
      std::println(stderr, "usage: tandem-noctalia [--service]");
      return 2;
    }
  }
  return tandem::runApplication(service);
}
