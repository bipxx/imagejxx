#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
// Admission before renderer allocation; image pixels remain shared with the
// prepared immutable result. Render storage has its own logical phase budget.
void admit_native_workflow_viewport(const DocumentSnapshot &, std::size_t);
class NativeWorkflowViewport {
public:
  NativeWorkflowViewport(const DocumentSnapshot &, std::size_t);
  ~NativeWorkflowViewport();
  bool draw(bool gl, bool enabled, bool diagnostics);
  void release_textures();
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
