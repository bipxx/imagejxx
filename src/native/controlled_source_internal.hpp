#pragma once
#include "kernel_control.hpp"
#include <optional>
namespace imagejxx::detail {
// Optional, synchronous capability for reviewed providers. PlaneSource's public
// vtable and legacy plugin contract stay unchanged. The context is borrowed for
// this call only; no provider may retain callbacks, tokens or the domain tag.
struct SourceReadControl {
  ExecutionControl control;
  const void *domain = nullptr;
};
inline SourceReadControl source_read_control() {
  SourceReadControl value;value.domain=&current_execution_scope;
  if(current_execution_scope){
    value.control.cancellation=execution_cancellation();
    value.control.checkpoint=[] {kernel_checkpoint();};
    value.control.stage_progress=[](std::string_view name,std::size_t done,std::size_t total){execution_stage_progress(name,done,total);};
  }
  return value;
}
class ControlledPlaneSource {
public:
  virtual ~ControlledPlaneSource() = default;
  virtual std::shared_ptr<const Image> controlled_plane(PlanePosition,const SourceReadControl &) const=0;
  virtual Image controlled_region(PlanePosition,int,int,int,int,const SourceReadControl &) const=0;
};
// Separate Python extensions each link a static core and own distinct TLS.
// Bridge only on a domain crossing; a same-domain scope would duplicate hooks.
class SourceReadScope {
  std::optional<ExecutionScope> scope_;
public:
  explicit SourceReadScope(const SourceReadControl &value){
    if(value.domain!=&current_execution_scope)scope_.emplace(value.control);
  }
};
inline std::shared_ptr<const Image> source_plane(const PlaneSource &source,PlanePosition p,const SourceReadControl &control){
  if(const auto aware=dynamic_cast<const ControlledPlaneSource *>(&source))return aware->controlled_plane(p,control);
  return source.read_plane(p);
}
inline Image source_region(const PlaneSource &source,PlanePosition p,int x,int y,int width,int height,const SourceReadControl &control){
  if(const auto aware=dynamic_cast<const ControlledPlaneSource *>(&source))return aware->controlled_region(p,x,y,width,height,control);
  return source.read_region(p,x,y,width,height);
}
}
