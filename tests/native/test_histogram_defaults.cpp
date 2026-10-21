#include <imagejxx/preferences.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
void require(bool ok){if(!ok)throw std::runtime_error("Histogram defaults contract failed");}
int main(){
  Settings settings;settings.update({{"theme",std::string("light")},{"histogram_custom",true},{"histogram_bins",std::int64_t(8)},{"histogram_lower",10.},{"histogram_upper",20.},{"histogram_log",true},{"histogram_scientific",true},{"histogram_roi",std::string("positioned_rois")}});
  const auto before=settings.values();bool rejected=false;
  try{settings.update({{"theme",std::string("dark")},{"histogram_lower",30.},{"histogram_upper",20.}});}catch(const std::invalid_argument&){rejected=true;}
  require(rejected && settings.values()==before);
  std::vector<std::string> warnings;auto migrated=Settings::migrate({{"histogram_lower",10.},{"histogram_upper",20.},{"theme",std::string("light")}},warnings);
  require(warnings.empty() && migrated.number("histogram_lower")==10 && migrated.number("histogram_upper")==20);
  migrated=Settings::migrate({{"histogram_lower",10.},{"histogram_upper",5.},{"theme",std::string("light")}},warnings);
  require(warnings.size()==1 && migrated.number("histogram_lower")==0 && migrated.number("histogram_upper")==1 && migrated.text("theme")=="light");
  settings.reset("Histogram");require(!settings.boolean("histogram_custom") && !settings.boolean("histogram_log") && !settings.boolean("histogram_scientific") && settings.boolean("histogram_use_roi") && settings.integer("histogram_bins")==256 && settings.number("histogram_lower")==0 && settings.number("histogram_upper")==1 && settings.text("histogram_roi")=="active_selection" && settings.text("theme")=="light");
  const auto doc=PreferencesDocument::parse(R"({"format":"imagejxx-preferences","version":1,"values":{"histogram_lower":10,"histogram_upper":20,"future":{"keep":[1,true,null]}},"extension":{"keep":42}})",false);
  require(!doc.write_blocked && doc.settings.number("histogram_lower")==10 && doc.settings.number("histogram_upper")==20 && doc.unknown_values_json==R"({"future":{"keep":[1,true,null]}})" && doc.unknown_fields_json==R"({"extension":{"keep":42}})");
  const auto restored=PreferencesDocument::parse(doc.json(),false);require(restored.settings.values()==doc.settings.values() && restored.unknown_values_json==doc.unknown_values_json && restored.unknown_fields_json==doc.unknown_fields_json);
  std::cout<<"histogram defaults schema, atomic bounds, recovery and opaque roundtrip passed\n";
}
