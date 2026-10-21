#include <imagejxx/operations.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  validate_operation_record("add", {{"value", 42.}});
  Image image(5, 4, 1, PixelType::UInt16);
  auto result = execute_operation("add", image, {{"value", 42.}});
  if (result.image.at(0, 0) != 42 || image.at(0, 0) != 0)
    return 1;
  auto binary = execute_operation("threshold", result.image, {{"lower", 40.}});
  auto labels =
      execute_operation("label", binary.image, {{"return_num", true}});
  if (labels.labelCount != 1)
    return 1;
  std::cout << operation_registry().size()
            << " headless native operations; labels: " << *labels.labelCount
            << '\n';
}
