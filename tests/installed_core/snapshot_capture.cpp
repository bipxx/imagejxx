#include <imagejxx/application.hpp>
#include <stdexcept>
#include <iostream>
using namespace imagejxx;
int main() {try {
  Application app;
  auto source=std::make_shared<Document>(Dataset::from_stack({Image(5,4)}));
  app.adopt(source);
  auto display=source->snapshot().display();display.properties["opaque"]=std::string(8192,'x');
  source->set_display(display);
  const auto plan=app.prepare_snapshot_capture(65536);
  const auto captured=app.capture_snapshot(plan);
  if(captured.id()!=source->id || plan.bytes()<8192 ||
      app.data_snapshot(source->id).get()!=source->data_snapshot().get() ||
      !metadata_equal(captured.display().properties,display.properties))throw std::runtime_error("Capture mismatch");
  bool short_cap=false,stale=false,closed=false;
  try {source->prepare_snapshot_capture(plan.bytes()-1);}catch(const std::invalid_argument&){short_cap=true;}
  display.zoom=2;source->set_display(display);
  try {app.capture_snapshot(plan);}catch(const RevisionConflict&){stale=true;}
  const auto current=app.prepare_snapshot_capture(65536);app.close(source->id);
  try {app.capture_snapshot(current);}catch(const DocumentUnavailable&){closed=true;}
  if(!short_cap || !stale || !closed)throw std::runtime_error("Capture guard missing");
  std::cout<<"PASS installed snapshot capture admission and membership\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
