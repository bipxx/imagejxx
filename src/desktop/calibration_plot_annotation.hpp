#pragma once

#include <imagejxx/imagej_intensity_calibration.hpp>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace imagejxx::desktop {

struct CalibrationPlotAnnotation {
  static constexpr std::size_t working_bytes=8192;
  bool model=false,fit=false,r_squared_undefined=false;
  std::string formula,coefficients,detail,objective,canvas_text;
  std::optional<double> rmse,r_squared;
};

namespace calibration_annotation_detail {
inline const MetadataValue* field(const MetadataObject& object,const char* key) {
  const auto it=object.find(key);return it==object.end()?nullptr:&it->second;
}
inline const std::string* text(const MetadataValue* value) {
  return value?std::get_if<std::string>(value):nullptr;
}
template<class T> const T* node(const MetadataValue* value) {
  if(!value)return nullptr;
  const auto p=std::get_if<std::shared_ptr<const MetadataTree>>(value);
  return p && *p?std::get_if<T>(&(*p)->value):nullptr;
}
inline std::optional<double> number(const MetadataValue* value) {
  if(!value)return {};
  double result;
  if(const auto p=std::get_if<double>(value))result=*p;
  else if(const auto p=std::get_if<std::int64_t>(value))result=double(*p);
  else if(const auto p=std::get_if<std::uint64_t>(value))result=double(*p);
  else return {};
  return std::isfinite(result)?std::optional<double>(result):std::nullopt;
}
inline bool flag(const MetadataObject& object,const char* key) {
  const auto v=field(object,key);const auto p=v?std::get_if<bool>(v):nullptr;return p && *p;
}
inline void append_number(std::string& out,double value) {
  char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,17);
  if(result.ec==std::errc{})out.append(buffer,result.ptr);
}
} // namespace calibration_annotation_detail

// Prepare once with the retained payload; do not rebuild text in the renderer.
inline std::string calibration_plot_canvas_text(const CalibrationPlotAnnotation& annotation) {
  std::string text=annotation.model?annotation.formula:annotation.detail;
  if(!annotation.coefficients.empty())text+="\n"+annotation.coefficients;
  if(annotation.fit) {
    text+="\n"+annotation.objective+"\nRMSE: ";
    if(annotation.rmse)calibration_annotation_detail::append_number(text,*annotation.rmse);else text+="unavailable";
    text+="\nR^2: ";
    if(annotation.r_squared)calibration_annotation_detail::append_number(text,*annotation.r_squared);
    else text+=annotation.r_squared_undefined?"undefined":"unavailable";
  }
  return text;
}

// Inspect only bounded, named provenance fields. Never serialize arbitrary
// nested metadata or materialize the stored lookup table for a UI annotation.
inline CalibrationPlotAnnotation calibration_plot_annotation(
    const ImageJIntensityCalibration& calibration,bool table) {
  using namespace calibration_annotation_detail;
  CalibrationPlotAnnotation out;
  if(calibration.mode()==ImageJIntensityMode::Signed16) {
    out.model=true;out.formula="y = x - 32768";out.coefficients="a=-32768; b=1";
    out.detail="Implicit signed16 calibration.";return out;
  }
  if(!calibration.has_density()) {
    out.model=true;out.formula="y = x";out.detail="Uncalibrated values.";return out;
  }
  out.detail="Lookup table: no supported recorded model provenance.";
  const auto& provenance=calibration.source_parameters();
  const auto producer=text(field(provenance,"producer")),name=text(field(provenance,"function"));
  if(!producer || *producer!="imagej154r.function" || !name || number(field(provenance,"version"))!=1.)return out;
  struct Model {const char* name;const char* formula;std::size_t count;const char* domain;};
  static constexpr Model models[]={
    {"straight_line","y = a + b*x",2,""},
    {"poly2","y = a + b*x + c*x^2",3,""},
    {"poly3","y = a + b*x + c*x^2 + d*x^3",4,""},
    {"poly4","y = a + b*x + c*x^2 + d*x^3 + e*x^4",5,""},
    {"exponential","y = a*exp(b*x)",2,""},
    {"power","y = a*x^b",2,""},
    {"log","y = a*ln(b*x)",2,"At x=0, the ImageJ convention is y=-1000*a."},
    {"rodbard","y = d + (a-d)/(1+(x/c)^b)",4,""},
    {"gamma_variate","y = b*(x-a)^c*exp(-(x-a)/d)",4,"At x<=a, y=0; otherwise b,c,d must be positive."},
    {"log2","y = a + b*ln(x-c)",3,"Defined only for x>c."},
    {"rodbard2","y = c*((x-a)/(d-x))^(1/b)",4,"ImageJ NIH inverse: y=0 below a or when d-x<2*denorm_min."},
    {"exp_with_offset","y = a*exp(-b*x) + c",3,""},
    {"gaussian","y = a + (b-a)*exp(-(x-c)^2/(2*d^2))",4,""},
    {"exp_recovery","y = a*(1-exp(-b*x)) + c",3,""},
    {"uncalibrated_od","y = 0.434294481*ln(255/x)",0,"At x=0, use x=0.5."}
  };
  const Model* model=nullptr;for(const auto& entry:models)if(*name==entry.name){model=&entry;break;}
  const auto coefficientValue=field(provenance,"coefficients");
  const auto coefficients=node<MetadataArray>(coefficientValue);
  // The legacy metadata JSON bridge represents an empty [] as a string list.
  // Accept that empty representation only for a genuinely zero-arity model.
  const auto legacy=coefficientValue?std::get_if<std::vector<std::string>>(coefficientValue):nullptr;
  if(!model || (coefficients?coefficients->size()!=model->count:!(model->count==0 && legacy && legacy->empty())) ||
      (*name=="uncalibrated_od" && calibration.bit_depth()!=8))return out;
  std::array<double,5> values{};
  for(std::size_t i=0;i<model->count;++i){const auto v=number(&(*coefficients)[i]);if(!v)return out;values[i]=*v;}
  out.model=true;out.formula=model->formula;out.detail=model->domain;out.coefficients.reserve(256);
  for(std::size_t i=0;i<model->count;++i) {
    if(i)out.coefficients+="; ";
    out.coefficients+=char('a'+i);out.coefficients+='=';append_number(out.coefficients,values[i]);
  }
  if(*name=="uncalibrated_od" && flag(provenance,"inverted_lut")) {
    out.formula="y = 0.434294481*ln(255/(255-x))";out.detail="Inverted OD: at x=255, use x=254.5.";
  }
  if(table)out.detail+=out.detail.empty()?"Stored samples include Float32 conversion.":" Stored samples include Float32 conversion.";
  if(calibration.bit_depth()==8 && *name!="uncalibrated_od" && flag(provenance,"zero_clip"))
    out.detail+=table?" Negative outputs are clipped in the stored UInt8 table.":" The fit curve is unclipped; the stored UInt8 table clips negative outputs.";
  const auto fit=node<MetadataObject>(field(provenance,"fit"));
  if(!fit || number(field(*fit,"version"))!=1.)return out;
  const auto method=text(field(*fit,"method"));
  if(!method || (*method!="imagej_linear_regression" && *method!="scaled_reorthogonalized_qr" && *method!="scaled_damped_least_squares"))return out;
  const bool inverse=*name=="rodbard2";
  const auto objectiveField=field(*fit,"objective");const auto objective=text(objectiveField);
  if((objectiveField && !objective) || (*method=="scaled_damped_least_squares" && !objective))return out;
  if(objective && *objective!=(inverse?"measured_as_function_of_known":"known_as_function_of_measured"))return out;
  out.fit=true;out.objective=inverse?"Fit objective: measured(known)":"Fit objective: known(measured)";
  out.rmse=number(field(*fit,"rmse"));if(out.rmse && *out.rmse<0)out.rmse.reset();
  const auto r2=field(*fit,"r_squared");out.r_squared=number(r2);
  out.r_squared_undefined=r2 && std::holds_alternative<std::monostate>(*r2);
  return out;
}

} // namespace imagejxx::desktop
