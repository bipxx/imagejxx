#include <imagejxx/auto_threshold.hpp>
#include <iostream>
int main() {
  const std::vector<std::uint64_t> counts(256,1);
  for(const auto *method:{"default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"})
    std::cout << method << ' ' << imagejxx::auto_threshold_histogram_imagej(counts,method) << '\n';
}
