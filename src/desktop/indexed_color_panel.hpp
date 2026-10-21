#pragma once
#include "desktop.hpp"
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/indexed_rgb_command.hpp>
#include <variant>
namespace imagejxx::desktop {
using IndexedColorCommand=std::variant<ResolvedIndexedDatasetCommand,ResolvedIndexedRgbCommand>;
class IndexedColorPanel {
public:
  using Publication=std::function<void(const IndexedColorCommand &,PreparedOperation &,bool)>;
  explicit IndexedColorPanel(std::shared_ptr<JobScheduler>);
  ~IndexedColorPanel();
  void show(const DocumentSnapshot &,std::size_t,bool,const Json &record=Json(),bool expand=false);
  void patch(const Json &);
  void preview();
  void apply();
  void cancel(const char *);
  void request_cancel() const;
  void tick(const std::optional<DocumentSnapshot> &,std::size_t,const Publication &);
  void draw();
  bool visible() const;
  bool working() const;
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  std::optional<JobInfo> job_info() const;
  Json diagnostics() const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
}
