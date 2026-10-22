#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace imagejxx::desktop {
// Creator-thread admission for managed surfaces. Logical texture storage uses
// four bytes/texel; driver overhead and provider/transform internals are external.
class RenderMemory {
  struct State {
    std::size_t limit,retained=0,working=0,peak=0;
    std::uint64_t denied=0;
    explicit State(std::size_t value):limit(value){}
  };
  std::shared_ptr<State> state_;
public:
  class Lease {
    friend class RenderMemory;
    std::shared_ptr<State> state_;
    std::size_t bytes_=0;
    bool retained_=false;
    Lease(std::shared_ptr<State> state,std::size_t bytes,bool retained=false) noexcept
      :state_(std::move(state)),bytes_(bytes),retained_(retained){}
  public:
    Lease()=default;
    Lease(const Lease &)=delete;
    Lease &operator=(const Lease &)=delete;
    Lease(Lease &&other) noexcept:state_(std::move(other.state_)),bytes_(std::exchange(other.bytes_,0)),retained_(other.retained_){}
    Lease &operator=(Lease &&other) noexcept {if(this!=&other){reset();state_=std::move(other.state_);bytes_=std::exchange(other.bytes_,0);retained_=other.retained_;}return *this;}
    ~Lease(){reset();}
    void reset() noexcept {if(state_){(retained_?state_->retained:state_->working)-=bytes_;state_.reset();bytes_=0;}}
    Lease retain(std::size_t bytes) {
      if(!state_ || retained_ || bytes>bytes_)throw std::logic_error("Render surface exceeded admitted bytes");
      bytes_-=bytes;state_->working-=bytes;state_->retained+=bytes;
      return Lease(state_,bytes,true);
    }
    std::size_t bytes() const noexcept{return bytes_;}
  };
  explicit RenderMemory(std::size_t limit=1024ULL*1024*1024):state_(std::make_shared<State>(limit)){if(!limit)throw std::invalid_argument("Render memory limit must be positive");}
  std::size_t limit()const noexcept{return state_->limit;}
  std::size_t retained()const noexcept{return state_->retained;}
  std::size_t working()const noexcept{return state_->working;}
  std::size_t used()const noexcept{return retained()+working();}
  std::size_t peak()const noexcept{return state_->peak;}
  std::uint64_t denied()const noexcept{return state_->denied;}
  void validateLimit(std::size_t value)const {if(!value || value<used())throw std::invalid_argument("Render memory limit is below live reservations; close views or release render contexts first");}
  void setLimit(std::size_t value){validateLimit(value);state_->limit=value;}
  Lease acquire(std::size_t bytes){
    if(bytes>limit()-used()){
      if(state_->denied!=std::numeric_limits<std::uint64_t>::max())++state_->denied;
      throw std::invalid_argument("Render buffers exceed shared memory limit");
    }
    state_->working+=bytes;state_->peak=std::max(state_->peak,used());return Lease(state_,bytes);
  }
  static std::size_t add(std::size_t a,std::size_t b){if(b>std::numeric_limits<std::size_t>::max()-a)throw std::overflow_error("Render byte count overflow");return a+b;}
  static std::size_t mul(std::size_t a,std::size_t b){if(a && b>std::numeric_limits<std::size_t>::max()/a)throw std::overflow_error("Render byte count overflow");return a*b;}
  struct Plan {
    std::size_t texture,readback,rank,peak;
  };
  static Plan plan(int width,int height,int samples,bool gl,bool capture,bool rank=false,std::size_t fallback_samples=0){
    if(width<=0 || height<=0 || samples<=0)throw std::invalid_argument("Invalid render dimensions");
    const auto pixels=mul(std::size_t(width),std::size_t(height));
    Plan p{gl?mul(pixels,4):0,capture?mul(pixels,mul(8,std::size_t(samples)+3)):0,rank?mul(pixels,add(mul(8,std::size_t(samples)),3)):0,0};
    // Two extracted/selected source planes, RGB output and optional upload.
    // Fallback/row samples cover regional providers that decode a full XY plane.
    p.peak=add(add(add(p.texture,p.readback),p.rank),add(mul(pixels,add(mul(16,std::size_t(samples)),gl?27:24)),mul(fallback_samples,8)));
    return p;
  }
};
}
