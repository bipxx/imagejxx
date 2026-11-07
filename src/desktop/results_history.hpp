#pragma once
// Results-only cell deltas. The image document's history is independent.
#include "results_import.hpp"
#include <map>
#include <memory>

namespace imagejxx::desktop {
class ResultsCellHistory {
public:
  struct Cell {
    bool present=false;
    Json value;
    static Cell capture(const Json& row,const std::string& column) {
      const auto it=row.find(column);return it==row.end()?Cell{}:Cell{true,*it};
    }
    bool matches(const Json& row,const std::string& column) const {
      const auto it=row.find(column);
      return present?(it!=row.end() && sameResultsImportJson(value,*it)):it==row.end();
    }
  };
  struct Entry {
    RenderMemory::Lease memory;
    std::size_t bytes=0,row=0,min_rows=0;
    std::string column;
    Cell before,after;
    Json implicit;
  };
  struct Guard {
    RenderMemory::Lease memory;
    std::size_t bytes=0;
    Json expected;
  };
private:
  std::vector<std::shared_ptr<const Entry>> entries_;
  std::map<std::size_t,std::shared_ptr<const Guard>> guards_;
  std::size_t cursor_=0,evicted_=0,resets_=0;
  void prune() noexcept {
    for(auto it=guards_.begin();it!=guards_.end();) {
      const bool used=std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry){return entry->row==it->first;});
      if(!used)it=guards_.erase(it);else ++it;
    }
  }
  static std::size_t cellBytes(const Json& row,const std::string& column) {
    const auto it=row.find(column);return it==row.end()?128:resultImportJsonBytes(*it);
  }
public:
  std::size_t undoSteps() const noexcept{return cursor_;}
  std::size_t redoSteps() const noexcept{return entries_.size()-cursor_;}
  std::size_t evicted() const noexcept{return evicted_;}
  std::size_t resets() const noexcept{return resets_;}
  std::size_t bytes() const noexcept {
    std::size_t bytes=0;for(const auto& entry:entries_)bytes+=entry->bytes;for(const auto& guard:guards_)bytes+=guard.second->bytes;return bytes;
  }
  const Entry* target(bool redo) const noexcept {
    if(redo)return redoSteps()?entries_[cursor_].get():nullptr;
    return undoSteps()?entries_[cursor_-1].get():nullptr;
  }
  void clear() noexcept {decltype(entries_) retired;entries_.swap(retired);guards_.clear();cursor_=evicted_=resets_=0;}
  void swap(ResultsCellHistory& other) noexcept {
    entries_.swap(other.entries_);guards_.swap(other.guards_);std::swap(cursor_,other.cursor_);std::swap(evicted_,other.evicted_);std::swap(resets_,other.resets_);
  }
  // Configuration changes retire history only, never current table values.
  // Keep nearest redo states; discard farthest redo before oldest applied edits.
  void trim(std::size_t steps,std::size_t limit,bool preserveLast=false) {
    while(entries_.size()>steps || bytes()>limit) {
      if(preserveLast && entries_.size()==1)throw std::length_error("Results edit exceeds history capacity");
      if(redoSteps())entries_.pop_back();
      else {entries_.erase(entries_.begin());--cursor_;}
      ++evicted_;prune();
    }
  }
  template<class Implicit> const char* conflict(const Json& rows,bool redo,Implicit implicit) const {
    const auto* entry=target(redo);if(!entry)return "empty";
    if(!rows.is_array() || rows.size()<entry->min_rows || entry->row>=rows.size())return "rows_removed";
    const auto guard=guards_.find(entry->row);
    if(guard==guards_.end() || !sameResultsImportJson(rows.at(entry->row),guard->second->expected))return "row_changed";
    if(!(redo?entry->before:entry->after).matches(rows.at(entry->row),entry->column))return "cell_changed";
    if(!sameResultsImportJson(implicit(entry->row,entry->column),entry->implicit))return "default_changed";
    return "";
  }
  ResultsCellHistory recorded(std::size_t row,std::size_t count,const std::string& column,
      const Json& before,const Json& after,const Json& implicit,RenderMemory& memory,
      std::size_t steps,std::size_t limit,std::size_t externalWorking) const {
    if(RenderMemory::add(externalWorking,65536)>limit)throw std::length_error("Results history exceeds processing budget");
    auto containers=memory.acquire(65536);ResultsCellHistory next=*this;
    const auto prior=next.guards_.find(row);
    if(prior!=next.guards_.end() && !sameResultsImportJson(prior->second->expected,before)) {
      const auto resets=next.resets_+1,evicted=next.evicted_;next.clear();next.resets_=resets;next.evicted_=evicted;
    }
    next.entries_.erase(next.entries_.begin()+next.cursor_,next.entries_.end());next.prune();
    const auto entryBytes=RenderMemory::add(1024,RenderMemory::add(RenderMemory::mul(column.size(),2),
        RenderMemory::add(RenderMemory::add(cellBytes(before,column),cellBytes(after,column)),resultImportJsonBytes(implicit))));
    auto peak=std::max(resultImportJsonBytes(before),resultImportJsonBytes(after));
    const auto oldGuard=next.guards_.find(row);if(oldGuard!=next.guards_.end())peak=std::max(peak,oldGuard->second->bytes-1024);
    const auto guardBytes=RenderMemory::add(1024,peak);
    const auto working=RenderMemory::mul(RenderMemory::add(entryBytes,guardBytes),2);
    if(RenderMemory::add(RenderMemory::add(externalWorking,65536),working)>limit)throw std::length_error("Results history exceeds processing budget");
    auto storage=memory.acquire(working);auto entry=std::make_shared<Entry>();entry->memory=storage.retain(entryBytes);entry->bytes=entryBytes;
    entry->row=row;entry->min_rows=count;entry->column=column;entry->before=Cell::capture(before,column);entry->after=Cell::capture(after,column);entry->implicit=implicit;
    auto guard=std::make_shared<Guard>();guard->memory=storage.retain(guardBytes);guard->bytes=guardBytes;guard->expected=after;
    next.entries_.push_back(std::move(entry));next.guards_[row]=std::move(guard);next.cursor_=next.entries_.size();
    next.trim(steps,limit,true);return next;
  }
  ResultsCellHistory travelled(bool redo,const Json& row,RenderMemory& memory,
      std::size_t limit,std::size_t externalWorking) const {
    const auto* entry=target(redo);if(!entry)throw std::invalid_argument("No Results cell edit to restore");
    const auto oldGuard=guards_.at(entry->row);const auto guardBytes=std::max(oldGuard->bytes,RenderMemory::add(1024,resultImportJsonBytes(row)));
    const auto working=RenderMemory::add(65536,RenderMemory::mul(guardBytes,2));
    if(RenderMemory::add(externalWorking,working)>limit)throw std::length_error("Results history restore exceeds processing budget");
    auto storage=memory.acquire(working);ResultsCellHistory next=*this;auto guard=std::make_shared<Guard>();guard->memory=storage.retain(guardBytes);guard->bytes=guardBytes;guard->expected=row;
    next.guards_.at(entry->row)=std::move(guard);if(redo)++next.cursor_;else --next.cursor_;return next;
  }
};
} // namespace imagejxx::desktop
