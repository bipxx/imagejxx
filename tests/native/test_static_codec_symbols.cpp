#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void *load(const char *path, int flags) {
  auto handle = dlopen(path, flags);
  if (!handle) throw std::runtime_error(dlerror());
  return handle;
}
std::string version(void *handle) {
  dlerror();
  const auto probe = reinterpret_cast<const char *(*)()>(dlsym(handle, "imagejxx_test_codec_probe"));
  if (const auto error = dlerror()) throw std::runtime_error(error);
  return probe();
}
}
int main(int argc, char **argv) {
  try {
    if (argc != 4) throw std::invalid_argument("Supply host, unbound and bound codec modules");
    auto host = load(argv[1], RTLD_NOW | RTLD_GLOBAL);
    auto control = load(argv[2], RTLD_NOW | RTLD_LOCAL);
    auto bound = load(argv[3], RTLD_NOW | RTLD_LOCAL);
    if (version(control) != "unrelated loaded codec")
      throw std::runtime_error("Control did not reproduce ELF symbol interposition");
    if (version(bound) != "selected static codec")
      throw std::runtime_error("Selected static codec was replaced by the loaded host");
    dlclose(bound); dlclose(control); dlclose(host);
    std::cout << "Static codec binding and unbound interposition control passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
