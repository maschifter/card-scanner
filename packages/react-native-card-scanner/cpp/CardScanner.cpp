#include "CardScanner.h"
#include <executorch/extension/module/module.h>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;

double CardScanner::multiply(const std::string &modelSource) {
  std::unique_ptr<Module> module = std::make_unique<Module>(modelSource);

  const auto result = module->forward();

  const auto output = result->at(0).toTensor().numel();

  return static_cast<double>(output);
}
} // namespace cardscanner
