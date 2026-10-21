#pragma once
#include "planar_annotation_panel.hpp"
#include <imagejxx/planar_annotation_command.hpp>

namespace imagejxx::desktop {
class PlanarAnnotationRebind {
public:
  PlanarAnnotationRebind();
  ~PlanarAnnotationRebind();
  void show(const DocumentSnapshot &,const MetadataObject &original);
  void draw(const std::optional<DocumentSnapshot> &,bool busy,const PlanarAnnotationPublisher &);
  void cancel(const char *reason);
  bool visible() const;
  Json diagnostics() const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
