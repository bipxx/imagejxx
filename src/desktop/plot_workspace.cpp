#include "plot_workspace.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace imagejxx::desktop {
namespace {
constexpr std::size_t max_windows=256,max_nodes=1024*1024,max_text=1024*1024;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid retained numeric plot workspace");}
void keys(const Json& j,std::initializer_list<const char*> names) {
  if(!j.is_object() || j.size()!=names.size())invalid();
  for(auto name:names)if(!j.contains(name))invalid();
}
std::uint64_t readUnsigned(const Json& j) {
  if(!j.is_number_integer() || (!j.is_number_unsigned() && j.get<std::int64_t>()<0))invalid();
  return j.get<std::uint64_t>();
}
bool flag(const Json& j){if(!j.is_boolean())invalid();return j.get<bool>();}
double readFinite(const Json& j){if(!j.is_number() || !std::isfinite(j.get<double>()))invalid();return j.get<double>();}
std::string text(const Json& j){if(!j.is_string() || j.get_ref<const std::string&>().size()>max_text)invalid();return j.get<std::string>();}
std::size_t inspect(const Json& j,std::size_t& nodes,std::size_t depth=0) {
  if(++nodes>max_nodes || depth>12)invalid();
  auto bytes=std::size_t(128);
  if(j.is_string()) {
    const auto size=j.get_ref<const std::string&>().size();if(size>max_text)invalid();
    bytes=RenderMemory::add(bytes,RenderMemory::mul(size+1,4));
  } else if(j.is_structured())for(auto it=j.begin();it!=j.end();++it) {
    if(j.is_object() && it.key().size()>128)invalid();
    bytes=RenderMemory::add(bytes,inspect(it.value(),nodes,depth+1));
  }
  return bytes;
}
template<class T> using Word=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
template<class T> Json words(const std::vector<T>& input) {
  Json out=Json::array();out.get_ref<Json::array_t&>().reserve(input.size());
  for(const auto value:input){Word<T> word;std::memcpy(&word,&value,sizeof(word));out.push_back(word);}return out;
}
template<class T> std::vector<T> values(const Json& input) {
  if(!input.is_array() || input.size()>max_nodes)invalid();
  std::vector<T> out;out.reserve(input.size());
  for(const auto& j:input){const auto n=readUnsigned(j);if(n>std::numeric_limits<Word<T>>::max())invalid();const auto word=Word<T>(n);T value;std::memcpy(&value,&word,sizeof(value));out.push_back(value);}return out;
}
Json optional(const std::optional<double>& v){return v?Json(*v):Json(nullptr);}
std::optional<double> optional(const Json& j){return j.is_null()?std::nullopt:std::optional<double>{readFinite(j)};}
Json encodeView(const CalibrationPlotView& v) {
  return {{"fractions",v.fractions},{"manual_limits",v.manual_limits?Json(*v.manual_limits):Json(nullptr)},
    {"log_x",v.log_x},{"log_y",v.log_y},{"grid",v.grid},{"ticks",v.ticks},{"nice_ticks",v.nice_ticks},{"minor_ticks",v.minor_ticks}};
}
std::array<double,4> four(const Json& j){if(!j.is_array() || j.size()!=4)invalid();return {readFinite(j[0]),readFinite(j[1]),readFinite(j[2]),readFinite(j[3])};}
CalibrationPlotView decodeView(const Json& j,const ImageJIntensityFitPlot& data) {
  keys(j,{"fractions","manual_limits","log_x","log_y","grid","ticks","nice_ticks","minor_ticks"});
  CalibrationPlotView v;v.log_x=flag(j.at("log_x"));v.log_y=flag(j.at("log_y"));v.grid=flag(j.at("grid"));v.ticks=flag(j.at("ticks"));v.nice_ticks=flag(j.at("nice_ticks"));v.minor_ticks=flag(j.at("minor_ticks"));
  if(!j.at("manual_limits").is_null() && !v.set_limits(four(j.at("manual_limits"))))invalid();
  if(!v.update(data,four(j.at("fractions"))))invalid();return v;
}
Json encodeStyle(const CalibrationPlotStyle& s) {
  if(!s.valid())invalid();
  return {{"line_width",s.line_width},{"point_radius",s.point_radius},{"font_scale",s.font_scale},{"annotation_x",s.annotation_x},{"annotation_y",s.annotation_y},{"annotation_visible",s.annotation_visible},{"curve_color",s.curve_color},{"point_color",s.point_color}};
}
CalibrationPlotStyle decodeStyle(const Json& j) {
  keys(j,{"line_width","point_radius","font_scale","annotation_x","annotation_y","annotation_visible","curve_color","point_color"});
  CalibrationPlotStyle s;s.line_width=readFinite(j.at("line_width"));s.point_radius=readFinite(j.at("point_radius"));s.font_scale=readFinite(j.at("font_scale"));s.annotation_x=readFinite(j.at("annotation_x"));s.annotation_y=readFinite(j.at("annotation_y"));s.annotation_visible=flag(j.at("annotation_visible"));
  const auto a=readUnsigned(j.at("curve_color")),b=readUnsigned(j.at("point_color"));if(a>0xffffff || b>0xffffff)invalid();s.curve_color=std::uint32_t(a);s.point_color=std::uint32_t(b);if(!s.valid())invalid();return s;
}
Json encodePayload(const CalibrationPlotPayload& p) {
  const auto& d=p.data;const auto& a=p.annotation;
  Json histogram=nullptr;
  if(p.histogram)histogram={{"edges",words(p.histogram->histogram.edges)},{"counts",p.histogram->histogram.counts}};
  return {{"kind",p.kindName()},{"source_id",p.source_id},{"source_title",p.source_title},{"context_caption",p.context_caption},{"x_axis",p.x_axis},{"y_axis",p.y_axis},{"table",p.table},{"histogram_raw_csv",p.histogram_raw_csv},
    {"data",{{"function",d.function},{"value_unit",d.value_unit},{"objective",d.objective},{"measured",words(d.measured)},{"known",words(d.known)},{"curve_x",words(d.curve_x)},{"curve_y",words(d.curve_y)},{"bounds",{d.x_min,d.x_max,d.y_min,d.y_max}}}},
    {"annotation",{{"model",a.model},{"fit",a.fit},{"r_squared_undefined",a.r_squared_undefined},{"formula",a.formula},{"coefficients",a.coefficients},{"detail",a.detail},{"objective",a.objective},{"canvas_text",a.canvas_text},{"rmse",optional(a.rmse)},{"r_squared",optional(a.r_squared)}}},{"histogram",std::move(histogram)}};
}
std::shared_ptr<const CalibrationPlotPayload> decodePayload(const Json& j,RenderMemory& memory,std::uint64_t id) {
  keys(j,{"kind","source_id","source_title","context_caption","x_axis","y_axis","table","histogram_raw_csv","data","annotation","histogram"});
  const auto& d=j.at("data");const auto& a=j.at("annotation");
  keys(d,{"function","value_unit","objective","measured","known","curve_x","curve_y","bounds"});
  keys(a,{"model","fit","r_squared_undefined","formula","coefficients","detail","objective","canvas_text","rmse","r_squared"});
  ImageJIntensityFitPlot data;data.function=text(d.at("function"));data.value_unit=text(d.at("value_unit"));data.objective=text(d.at("objective"));
  data.measured=values<double>(d.at("measured"));data.known=values<double>(d.at("known"));data.curve_x=values<float>(d.at("curve_x"));data.curve_y=values<float>(d.at("curve_y"));
  if(data.measured.size()!=data.known.size() || data.curve_x.size()!=data.curve_y.size())invalid();
  const auto bounds=four(d.at("bounds"));data.x_min=bounds[0];data.x_max=bounds[1];data.y_min=bounds[2];data.y_max=bounds[3];
  CalibrationPlotAnnotation annotation;annotation.model=flag(a.at("model"));annotation.fit=flag(a.at("fit"));annotation.r_squared_undefined=flag(a.at("r_squared_undefined"));
  annotation.formula=text(a.at("formula"));annotation.coefficients=text(a.at("coefficients"));annotation.detail=text(a.at("detail"));annotation.objective=text(a.at("objective"));annotation.canvas_text=text(a.at("canvas_text"));annotation.rmse=optional(a.at("rmse"));annotation.r_squared=optional(a.at("r_squared"));
  const auto kind=text(j.at("kind"));const bool table=flag(j.at("table"));
  PlotKind type;if(kind=="fit")type=PlotKind::Fit;else if(kind=="calibration_table")type=PlotKind::CalibrationTable;else if(kind=="histogram")type=PlotKind::Histogram;else if(kind=="profile")type=PlotKind::Profile;else invalid();
  if(table!=(type==PlotKind::CalibrationTable))invalid();
  std::shared_ptr<DatasetHistogramResult> histogram;
  if(type==PlotKind::Histogram) {
    const auto& h=j.at("histogram");keys(h,{"edges","counts"});
    histogram=std::make_shared<DatasetHistogramResult>();histogram->histogram.edges=values<double>(h.at("edges"));
    const auto& counts=h.at("counts");if(!counts.is_array() || counts.empty() || counts.size()>65536 || histogram->histogram.edges.size()!=counts.size()+1)invalid();
    histogram->histogram.counts.reserve(counts.size());for(const auto& count:counts)histogram->histogram.counts.push_back(readUnsigned(count));
    const auto& edges=histogram->histogram.edges;for(std::size_t i=0;i<edges.size();++i)if(!std::isfinite(edges[i]) || (i && edges[i]<edges[i-1]))invalid();
  } else if(!j.at("histogram").is_null())invalid();
  if(type==PlotKind::Profile) {
    if(data.measured.empty())invalid();for(std::size_t i=0;i<data.measured.size();++i)if(!std::isfinite(data.measured[i]) || !std::isfinite(data.known[i]) || (i && data.measured[i]<data.measured[i-1]))invalid();
  }
  auto sourceTitle=text(j.at("source_title")),context=text(j.at("context_caption")),x=text(j.at("x_axis")),y=text(j.at("y_axis"));
  auto bytes=CalibrationPlotPayload::bytes(data,annotation,sourceTitle,context);bytes=RenderMemory::add(bytes,RenderMemory::add(x.capacity()+1,y.capacity()+1));
  if(histogram)bytes=RenderMemory::add(bytes,RenderMemory::add(sizeof(DatasetHistogramResult)+128,RenderMemory::mul(histogram->histogram.edges.capacity()+histogram->histogram.counts.capacity(),8)));
  auto admitted=memory.acquire(bytes);
  auto result=std::make_shared<CalibrationPlotPayload>(std::move(data),std::move(annotation),table,readUnsigned(j.at("source_id")),std::move(sourceTitle),std::move(context));
  result->kind=type;result->x_axis=std::move(x);result->y_axis=std::move(y);result->histogram=std::move(histogram);result->histogram_raw_csv=flag(j.at("histogram_raw_csv"));result->payload_id=id;result->memory=admitted.retain(bytes);return result;
}
} // namespace

PlotWorkspaceRecord encodePlotWorkspace(const std::vector<PlotWorkspaceEntry>& entries,RenderMemory& memory) {
  if(entries.size()>max_windows)invalid();
  std::vector<const CalibrationPlotPayload*> payloads;payloads.reserve(entries.size());
  std::size_t allocation=RenderMemory::add(8192,RenderMemory::mul(entries.size(),16384));
  for(const auto& entry:entries) {
    if(!entry.payload)invalid();const auto* payload=entry.payload.get();
    if(std::find(payloads.begin(),payloads.end(),payload)==payloads.end()) {
      payloads.push_back(payload);allocation=RenderMemory::add(allocation,RenderMemory::mul(payload->memory.bytes(),32));
    }
  }
  PlotWorkspaceRecord out;out.memory=memory.acquire(allocation);
  Json data=Json::array(),windows=Json::array();
  for(const auto* payload:payloads)data.push_back(encodePayload(*payload));
  for(const auto& entry:entries)windows.push_back({{"payload",std::size_t(std::find(payloads.begin(),payloads.end(),entry.payload.get())-payloads.begin())},{"view",encodeView(entry.view)},{"style",encodeStyle(entry.style)}});
  out.value={{"schema","imagejxx.retained-numeric-plots"},{"version",1},{"payloads",std::move(data)},{"windows",std::move(windows)}};
  std::size_t nodes=0;if(inspect(out.value,nodes)>out.memory.bytes())invalid();return out;
}
DecodedPlotWorkspace decodePlotWorkspace(const Json& j,RenderMemory& memory,std::uint64_t firstPayloadId) {
  std::size_t nodes=0;const auto estimate=inspect(j,nodes);
  keys(j,{"schema","version","payloads","windows"});
  if(text(j.at("schema"))!="imagejxx.retained-numeric-plots" || readUnsigned(j.at("version"))!=1)invalid();
  const auto& data=j.at("payloads");const auto& windows=j.at("windows");
  if(!data.is_array() || !windows.is_array() || data.size()>max_windows || windows.size()>max_windows || data.size()>windows.size())invalid();
  if(!firstPayloadId || data.size()>std::numeric_limits<std::uint64_t>::max()-firstPayloadId)invalid();
  DecodedPlotWorkspace out;out.memory=memory.acquire(estimate);out.next_payload_id=firstPayloadId;
  std::vector<std::shared_ptr<const CalibrationPlotPayload>> payloads;payloads.reserve(data.size());
  for(const auto& payload:data)payloads.push_back(decodePayload(payload,memory,out.next_payload_id++));
  std::vector<bool> used(data.size(),false);out.entries.reserve(windows.size());
  for(const auto& window:windows) {
    keys(window,{"payload","view","style"});const auto index=readUnsigned(window.at("payload"));if(index>=payloads.size())invalid();used[std::size_t(index)]=true;
    auto payload=payloads[std::size_t(index)];auto view=decodeView(window.at("view"),payload->data);auto style=decodeStyle(window.at("style"));out.entries.push_back({std::move(payload),view,style});
  }
  for(bool referenced:used)if(!referenced)invalid();return out;
}
} // namespace imagejxx::desktop
