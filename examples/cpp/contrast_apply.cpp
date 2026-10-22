#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/application.hpp>
#include <iostream>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Image image(4,1,1,PixelType::UInt16);image.pixels={10,20,30,40};
  auto owner=std::make_shared<Document>(Dataset::from_stack({std::move(image)}));
  Application app;app.adopt(owner);
  const auto original=owner->snapshot();
  auto target=resolve_target(original);
  auto edit=prepare_contrast_apply(target,{10,40,12});
  if(!original.shares_data(owner->snapshot()))throw std::runtime_error("Prepare changed source");
  app.commit(edit);
  if(owner->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,1365,2730,4094})
    throw std::runtime_error("Unexpected Apply values");
  owner->undo();
  if(!owner->snapshot().data().dataset.shares_pixels(original.data().dataset))
    throw std::runtime_error("Undo did not restore source");
  std::cout<<"PASS installed scalar Apply API, atomic commit and Undo\n";
}
