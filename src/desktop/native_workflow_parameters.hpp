#pragma once
#include "desktop.hpp"
#include <imagejxx/native_workflow.hpp>
namespace imagejxx::desktop {
class NativeWorkflowParameters {
public:
  NativeWorkflowParameters(const DocumentSnapshot &,
                           std::shared_ptr<const NativeWorkflowOperation>,
                           std::size_t cap);
  ~NativeWorkflowParameters();
  bool visible() const;
  void cancel();
  void draw(bool enabled,
            const std::function<void(const NativeWorkflowOperation &)> &);
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
