#pragma once
#include <imagejxx/dataset.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <type_traits>
#include <stdexcept>
#include <vector>

namespace imagejxx::desktop {
// Creator-thread cache. Weak slots do not keep closed frontends/providers alive.
// Caller-held handles and temporary provider allocations are outside this budget.
class VirtualViewCache {
public:
  struct Slot {
    std::optional<Dataset> source;
    PlanePosition position{};
    std::shared_ptr<const Image> pixels;
    std::uint64_t stamp=0;
    void clear() noexcept { pixels.reset();source.reset();stamp=0; }
  };
  explicit VirtualViewCache(std::size_t budget=128ULL*1024*1024):budget_(budget) {
    if(!budget)throw std::invalid_argument("Virtual view cache budget must be positive");
  }
  std::shared_ptr<Slot> slot() {
    prune();auto value=std::make_shared<Slot>();slots_.push_back(value);return value;
  }
  std::size_t budget() const noexcept {return budget_;}
  std::size_t bytes() const noexcept {
    std::size_t total=0;for(const auto &weak:slots_)if(auto s=weak.lock())if(s->pixels)total+=cost(*s->pixels);
    return total;
  }
  std::size_t entries() const noexcept {
    std::size_t total=0;for(const auto &weak:slots_)if(auto s=weak.lock())total+=bool(s->pixels);return total;
  }
  std::uint64_t evictions() const noexcept {return evictions_;}
  void setBudget(std::size_t value) {
    if(!value)throw std::invalid_argument("Virtual view cache budget must be positive");
    budget_=value;trim(budget_,nullptr);
  }
  std::shared_ptr<const Image> read(const std::shared_ptr<Slot> &s,const Dataset &data,PlanePosition p) {
    if(s->pixels && s->source && s->source->shares_pixels(data) &&
       s->position.t==p.t && s->position.c==p.c && s->position.z==p.z){s->stamp=stamp();return s->pixels;}
    auto available=budget_/sizeof(double);const auto shape=data.shape();
    for(int extent:{shape.x,shape.y,shape.s}) {
      if(std::size_t(extent)>available)throw std::invalid_argument("Virtual XY plane exceeds aggregate decoded cache budget");
      available/=std::size_t(extent);
    }
    // Acquire/validate before evicting a useful entry. A provider may throw or
    // reenter; no iterators, locks or borrowed entry references cross this call.
    auto retained=data;
    auto pixels=data.plane(p);
    if(pixels->pixels.capacity()>budget_/sizeof(double))
      throw std::invalid_argument("Virtual XY buffer capacity exceeds aggregate decoded cache budget");
    const auto required=cost(*pixels);
    static_assert(std::is_nothrow_move_assignable<std::optional<Dataset>>::value,"Cache source publication must not allocate");
    trim(budget_-required,s.get());
    s->source=std::move(retained);s->position=p;s->pixels=std::move(pixels);s->stamp=stamp();
    return s->pixels;
  }
private:
  std::size_t budget_;
  std::uint64_t clock_=0,evictions_=0;
  std::vector<std::weak_ptr<Slot>> slots_;
  static std::size_t cost(const Image &p) noexcept {return p.pixels.capacity()*sizeof(double);}
  void prune(){slots_.erase(std::remove_if(slots_.begin(),slots_.end(),[](const auto &s){return s.expired();}),slots_.end());}
  std::uint64_t stamp() noexcept {
    if(clock_==std::numeric_limits<std::uint64_t>::max()) {
      for(auto &weak:slots_)if(auto s=weak.lock())s->stamp/=2;
      clock_/=2;
    }
    return ++clock_;
  }
  void trim(std::size_t available,const Slot *except) {
    prune();
    for(;;){
      std::size_t total=0;std::shared_ptr<Slot> oldest;
      for(auto &weak:slots_)if(auto s=weak.lock())if(s.get()!=except && s->pixels){
        total+=cost(*s->pixels);if(!oldest || s->stamp<oldest->stamp)oldest=std::move(s);
      }
      if(total<=available)return;
      oldest->clear();if(evictions_!=std::numeric_limits<std::uint64_t>::max())++evictions_;
    }
  }
};
}
