#include <imagejxx/imagej_intensity_execution.hpp>
#include <imagejxx/imagej_intensity_plot.hpp>
#include "kernel_control.hpp"
#include "contrast_command_record.hpp"
#include <imagejxx/document.hpp>
#include <imagejxx/imagej_dataset_projection.hpp>
#include "axis_command_record.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
namespace wire = detail::axis_command_record;
constexpr const char* schema = "imagejxx.intensity-calibration";
const MetadataObject& object(const MetadataValue& value) { return wire::object_value(value); }
const std::string& text(const MetadataValue& value) { return wire::text_value(value); }
void validate_unit(const std::string& unit) {
  if (unit.size() > 1024U * 1024U || unit.find('\0') != std::string::npos)
    throw std::invalid_argument("Intensity value unit exceeds1MiB or contains NUL");
  try { (void)detail::session_metadata::Json(unit).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity value unit must be strict UTF-8");
  }
}
int depth(PixelType type) {
  if (type == PixelType::UInt8) return 8;
  if (type == PixelType::UInt16) return 16;
  if (type == PixelType::Float32) return 32;
  if (type == PixelType::Float64) return 64;
  throw std::invalid_argument("Invalid intensity pixel type");
}
std::size_t table_size(int bits) {
  if (bits == 8) return 256;
  if (bits == 16) return 65536;
  throw std::invalid_argument("Intensity lookup requires8/16-bit integer storage");
}
unsigned nibble(char c) {
  if (c >= '0' && c <= '9') return unsigned(c - '0');
  if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
  throw std::invalid_argument("Intensity table requires lowercase hexadecimal words");
}
bool boolean(const MetadataValue& value) {
  if (const auto flag = std::get_if<bool>(&value)) return *flag;
  throw std::invalid_argument("Intensity signed16 flag requires Boolean");
}
MetadataObject base(const char* mode, std::string unit) {
  validate_unit(unit);
  return {{"schema", std::string(schema)}, {"version", std::int64_t(1)},
          {"mode", std::string(mode)}, {"value_unit", std::move(unit)}};
}
constexpr const char* function_names[]={"straight_line","poly2","poly3","poly4","exponential",
    "power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset",
    "gaussian","exp_recovery","uncalibrated_od"};
constexpr std::size_t function_lengths[]={2,3,4,5,2,2,2,4,4,3,4,3,4,3,0};

int function_id(const std::string& name) {
  for(int i=0;i<15;++i)if(name==function_names[i])return i;
  throw std::invalid_argument("Unknown intensity function");
}
double function_value(int id,const std::vector<double>& p,double x,bool inverted_lut=false) {
    switch(id) {
    case 0:return p[0]+x*p[1];
    case 1:return p[0]+x*(p[1]+x*p[2]);
    case 2:return p[0]+x*(p[1]+x*(p[2]+x*p[3]));
    case 3:return p[0]+x*(p[1]+x*(p[2]+x*(p[3]+x*p[4])));
    case 4:return p[0]*std::exp(p[1]*x);
    case 5:return p[0]*std::pow(x,p[1]);
    case 6:return x==0.0?-1000*p[0]:p[0]*std::log(p[1]*x);
    case 7:return p[3]+(p[0]-p[3])/(1.0+std::pow(x/p[2],p[1]));
    case 8:
      if(p[0]>=x)return 0.0;
      if(p[1]<=0 || p[2]<=0 || p[3]<=0)return std::numeric_limits<double>::quiet_NaN();
      return p[1]*std::pow(x-p[0],p[2])*std::exp((-(x-p[0]))/p[3]);
    case 9:return x-p[2]<=0?std::numeric_limits<double>::quiet_NaN():p[0]+p[1]*std::log(x-p[2]);
    case 10:
      if(p[3]-x<2*std::numeric_limits<double>::denorm_min() || x<p[0])return 0.0;
      return std::pow((x-p[0])/(p[3]-x),1.0/p[1])*p[2];
    case 11:return p[0]*std::exp(-p[1]*x)+p[2];
    case 12:return p[0]+(p[1]-p[0])*std::exp(-(x-p[2])*(x-p[2])/(2.0*p[3]*p[3]));
    case 13:return p[0]*(1-std::exp(-p[1]*x))+p[2];
    default:
      if(inverted_lut){if(x==255.0)x=254.5;return 0.434294481*std::log(255.0/(255.0-x));}
      if(x==0.0)x=0.5;
      return 0.434294481*std::log(255.0/x);
    }
}
} // namespace

ImageJIntensityCalibration::ImageJIntensityCalibration(MetadataValue record,
    ImageJIntensityMode mode, int bits, bool signed16)
    : record_(std::move(record)), mode_(mode), depth_(bits), signed16_(signed16) {}

ImageJIntensityCalibration ImageJIntensityCalibration::uncalibrated(std::string unit) {
  return from_record(metadata_object(base("none", std::move(unit))));
}
ImageJIntensityCalibration ImageJIntensityCalibration::signed16(std::string unit) {
  return from_record(metadata_object(base("signed16", std::move(unit))));
}
ImageJIntensityCalibration ImageJIntensityCalibration::lookup(PixelType type,
    const std::vector<std::uint32_t>& words, std::string unit,
    MetadataObject parameters, bool signed16) {
  const int bits = depth(type);
  if (words.size() != table_size(bits))
    throw std::invalid_argument("Intensity lookup has wrong word count");
  if (signed16 && bits != 16)
    throw std::invalid_argument("Signed16 correction requires16-bit lookup storage");
  auto record = base("lookup", std::move(unit));
  auto provenance = metadata_object(std::move(parameters));
  try { (void)detail::session_metadata::to_json(provenance).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity provenance must be UTF-8 metadata");
  }
  constexpr const char* hex = "0123456789abcdef";
  std::string encoded(words.size() * 8, '0');
  for (std::size_t i = 0; i < words.size(); ++i)
    for (unsigned digit = 0; digit < 8; ++digit)
      encoded[i * 8 + digit] = hex[(words[i] >> (28 - 4 * digit)) & 15];
  record.emplace("bit_depth", std::int64_t(bits));
  record.emplace("table_f32_hex", std::move(encoded));
  record.emplace("source_parameters", std::move(provenance));
  record.emplace("signed16", signed16);
  return {metadata_object(std::move(record)), ImageJIntensityMode::Lookup, bits, signed16};
}
ImageJIntensityCalibration ImageJIntensityCalibration::function(PixelType type,
    const std::string& name_input, const std::vector<double>& p_input, std::string unit,
    bool zero_clip, bool inverted_lut) {
  if(name_input.size()>64)throw std::invalid_argument("Invalid intensity function name");
  const auto name=name_input;
  if(p_input.size()>5)throw std::invalid_argument("Too many intensity coefficients");
  const auto p=p_input;
  kernel_checkpoint();
  const int bits=depth(type);const auto count=table_size(bits);validate_unit(unit);
  const int id=function_id(name);
  if(p.size()!=function_lengths[id])throw std::invalid_argument("Wrong intensity coefficient count");
  if(id==14 && bits!=8)throw std::invalid_argument("Uncalibrated OD requires UInt8 storage");
  for(double value:p)if(!std::isfinite(value))throw std::invalid_argument("Intensity coefficients must be finite");
  std::vector<std::uint32_t> words(count);
  KernelStage table_stage("Calibration table",count);
  for(std::size_t i=0;i<count;++i) {
    table_stage.periodic(i);
    double value=function_value(id,p,double(i),inverted_lut);
    if(bits==8 && id!=14 && zero_clip && value<0.0)value=0.0;
    const float converted=static_cast<float>(value);
    if(std::isnan(converted))words[i]=0x7fc00000U;
    else std::memcpy(&words[i],&converted,sizeof(converted));
  }
  table_stage.finish();
  MetadataArray coefficients;for(double value:p)coefficients.emplace_back(value);
  MetadataObject parameters{{"producer",std::string("imagej154r.function")},{"version",std::int64_t(1)},
    {"function",name},{"coefficients",metadata_array(std::move(coefficients))},
    {"zero_clip",zero_clip},{"inverted_lut",inverted_lut},{"canonical_nan",true}};
  const bool signed16=bits==16 && p.size()>=2 && p[0]==-32768.0 && p[1]==1.0;
  return lookup(type,words,std::move(unit),std::move(parameters),signed16);
}
ImageJIntensityCalibration ImageJIntensityCalibration::fit_polynomial(PixelType type,
    const std::string& name_input,const std::vector<double>& x_input,const std::vector<double>& y_input,
    std::string unit,bool zero_clip) {
  if(name_input.size()>64)throw std::invalid_argument("Invalid intensity function name");
  const auto name=name_input;
  if(x_input.size()>4096 || y_input.size()>4096)throw std::invalid_argument("Too many fit points");
  const auto x=x_input,y=y_input;
  kernel_checkpoint();
  const auto count=table_size(depth(type));validate_unit(unit);
  const int degree=name=="straight_line"?1:name=="poly2"?2:name=="poly3"?3:name=="poly4"?4:0;
  if(!degree)throw std::invalid_argument("Polynomial fit supports straight_line/poly2/poly3/poly4");
  const auto m=std::size_t(degree+1),n=x.size();
  if(n!=y.size() || n<m || n>4096)throw std::invalid_argument("Fit requires matching point counts from degree+1 through4096");
  double lo=x[0],hi=x[0],yscale=1;
  for(std::size_t i=0;i<n;++i) {
    if(!std::isfinite(x[i]) || !std::isfinite(y[i]) || x[i]<0 || x[i]>double(count-1))
      throw std::invalid_argument("Fit points require finite values and measured values in the source range");
    lo=std::min(lo,x[i]);hi=std::max(hi,x[i]);yscale=std::max(yscale,std::abs(y[i]));
  }
  const double center=lo+(hi-lo)/2,scale=(hi-lo)/2;
  if(scale==0)throw std::invalid_argument("Fit measured values are rank deficient");
  std::array<std::vector<double>,5> q;
  std::array<std::array<double,5>,5> r{};
  std::array<double,5> rhs{},a{},coefficients{},basis{};basis[0]=1;
  KernelStage fit_stage("Calibration polynomial fit",m);
  for(std::size_t j=0;j<m;++j) {
    fit_stage.checkpoint(j);
    auto& v=q[j];v.resize(n);
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();const double z=(x[i]-center)/scale;double power=1;for(std::size_t k=0;k<j;++k)power*=z;v[i]=power;}
    // A second MGS pass controls loss of orthogonality for clustered samples.
    for(int pass=0;pass<2;++pass)for(std::size_t k=0;k<j;++k) {
      double dot=0;for(std::size_t i=0;i<n;++i)dot+=q[k][i]*v[i];r[k][j]+=dot;
      for(std::size_t i=0;i<n;++i)v[i]-=dot*q[k][i];
    }
    double norm=0;for(double value:v)norm=std::hypot(norm,value);
    if(norm<=std::numeric_limits<double>::epsilon()*double(n)*std::sqrt(double(n)))
      throw std::invalid_argument("Fit measured values are rank deficient");
    r[j][j]=norm;for(std::size_t i=0;i<n;++i){v[i]/=norm;rhs[j]+=v[i]*(y[i]/yscale);}
  }
  fit_stage.finish();
  for(int j=degree;j>=0;--j){double value=rhs[std::size_t(j)];for(std::size_t k=std::size_t(j)+1;k<m;++k)value-=r[std::size_t(j)][k]*a[k];a[std::size_t(j)]=value/r[std::size_t(j)][std::size_t(j)];}
  for(std::size_t j=0;j<m;++j) {
    for(std::size_t k=0;k<=j;++k)coefficients[k]+=a[j]*basis[k]*yscale;
    std::array<double,5> next{};
    for(std::size_t k=0;k<=j;++k){next[k]-=basis[k]*(center/scale);if(k<4)next[k+1]+=basis[k]/scale;}
    basis=next;
  }
  // Match ImageJ's direct unweighted line regression (not its nonlinear
  // minimizer); exact coefficient comparisons affect signed16 detection.
  if(degree==1) {
    double sx=0,sxx=0,sxy=0,sy=0;
    for(std::size_t i=0;i<n;++i){sx+=x[i];sxx+=x[i]*x[i];sxy+=x[i]*y[i];sy+=y[i];}
    const double denominator=sxx-sx*sx/double(n);
    if(!(denominator>0) || !std::isfinite(denominator))throw std::invalid_argument("Line fit coordinates are numerically singular");
    coefficients[1]=(sxy-sx*sy/double(n))/denominator;
    coefficients[0]=(sy-coefficients[1]*sx)/double(n);
  }
  double sse=0,mean=0;for(double value:y)mean+=(value/yscale)/double(n);
  double tss=0;MetadataArray measured,known;
  for(std::size_t i=0;i<n;++i) {
    double prediction=coefficients[std::size_t(degree)];for(int j=degree-1;j>=0;--j)prediction=prediction*x[i]+coefficients[std::size_t(j)];
    const double z=(x[i]-center)/scale;
    double normalized=a[std::size_t(degree)];for(int j=degree-1;j>=0;--j)normalized=normalized*z+a[std::size_t(j)];
    if(!std::isfinite(prediction) || std::abs(prediction/yscale-normalized)>1e-8*std::max(1.0,std::abs(normalized)))
      throw std::invalid_argument("Fit coefficients are unstable in original coordinates; use a wider measured range");
    const double residual=prediction/yscale-y[i]/yscale;sse+=residual*residual;
    const double centered=y[i]/yscale-mean;tss+=centered*centered;
    measured.emplace_back(x[i]);known.emplace_back(y[i]);
  }
  const double rmse=std::sqrt(sse/double(n))*yscale;
  if(!std::isfinite(rmse))throw std::invalid_argument("Fit residuals exceed the finite range");
  auto result=function(type,name,std::vector<double>(coefficients.begin(),coefficients.begin()+degree+1),std::move(unit),zero_clip);
  auto fields=object(result.record());auto parameters=result.source_parameters();
  parameters["fit"]=metadata_object({{"method",std::string(degree==1?"imagej_linear_regression":"scaled_reorthogonalized_qr")},
    {"version",std::int64_t(1)},{"measured",metadata_array(std::move(measured))},
    {"known",metadata_array(std::move(known))},{"rmse",rmse},
    {"r_squared",tss>0?MetadataValue(1-sse/tss):MetadataValue(std::monostate{})}});
  fields["source_parameters"]=metadata_object(std::move(parameters));
  return from_record(metadata_object(std::move(fields)));
}
ImageJIntensityCalibration ImageJIntensityCalibration::fit_nonlinear(PixelType type,
    const std::string& name_input,const std::vector<double>& x_input,const std::vector<double>& y_input,
    std::string unit,bool zero_clip,const std::vector<double>& initial_input,
    std::size_t max_iterations,double tolerance) {
  if(name_input.size()>64)throw std::invalid_argument("Invalid intensity function name");
  const auto name=name_input;
  if(x_input.size()>4096 || y_input.size()>4096)throw std::invalid_argument("Too many fit points");
  const auto x=x_input,y=y_input;
  if(initial_input.size()>4)throw std::invalid_argument("Too many starting coefficients");
  const auto initial=initial_input;
  kernel_checkpoint();
  const auto count=table_size(depth(type));validate_unit(unit);const int id=function_id(name);
  if(id<4 || id>13)throw std::invalid_argument("Nonlinear fitting requires a non-polynomial calibration model");
  const auto m=function_lengths[id],n=x.size();
  if(n!=y.size() || n<m || n>4096)throw std::invalid_argument("Fit requires matching point counts from parameter count through4096");
  if(!max_iterations || max_iterations>1024 || !std::isfinite(tolerance) || tolerance<=0 || tolerance>0.01)
    throw std::invalid_argument("Fit requires1..1024 iterations and finite tolerance in(0,0.01]");
  double xmin=x[0],xmax=x[0],ymin=y[0],ymax=y[0],yscale=1;std::size_t low=0,high=0,peak=0,trough=0;
  for(std::size_t i=0;i<n;++i) {
    if(!std::isfinite(x[i]) || !std::isfinite(y[i]) || x[i]<0 || x[i]>double(count-1))
      throw std::invalid_argument("Fit points require finite values and measured values in the source range");
    if(id==6 && x[i]<=0)throw std::invalid_argument("Log fitting requires positive measured values");
    if(x[i]<xmin){xmin=x[i];low=i;}if(x[i]>xmax){xmax=x[i];high=i;}
    if(y[i]<ymin){ymin=y[i];trough=i;}if(y[i]>ymax){ymax=y[i];peak=i;}
    yscale=std::max(yscale,std::abs(y[i]));
  }
  const double span=xmax-xmin;if(span==0)throw std::invalid_argument("Fit measured values are rank deficient");
  std::vector<double> p=initial;
  if(p.empty()) {
    double mean=0,xmean=0;for(std::size_t i=0;i<n;++i){mean+=(y[i]/yscale)/double(n);xmean+=x[i]/double(n);}mean*=yscale;
    const double rate=1/span,first=y[low],last=y[high];
    switch(id) {
    case 4:{const double rate0=(last>=first?1.0:-1.0)*(mean>=0?1.0:-1.0)/span;p={mean/std::exp(rate0*xmean),rate0};break;}
    case 5:p={mean/std::max(1.0,xmean),1};break;
    case 6:p={mean!=0?mean:1, std::exp(1.0)/xmax};break;
    case 7:p={first,1,xmin+span/2,last};break;
    case 8:{const double cd=std::max(x[peak]-xmin,0.1*span),shape=std::sqrt(cd);
      p={xmin,ymax/(std::pow(cd,shape)*std::exp(-cd/shape)),shape,shape};break;}
    case 9:p={mean,(ymax-ymin)/span,std::min(0.0,-xmin-0.1*span)};break;
    case 10:p={xmin-0.1*span,last>=first?1.0:-1.0,ymax,xmax+span};break;
    case 11:case 13:{const double amplitude=(id==11?first-last:last-first)/(std::exp(-rate*xmin)-std::exp(-rate*xmax));
      p={amplitude,rate,first-amplitude*(id==11?std::exp(-rate*xmin):1-std::exp(-rate*xmin))};break;}
    default:{const double baseline=(first+last)/2;const bool positive=ymax-baseline>=baseline-ymin;
      p={positive?ymin:ymax,positive?ymax:ymin,x[positive?peak:trough],span/4};break;}
    }
  }
  if(p.size()!=m)throw std::invalid_argument("Wrong initial coefficient count");
  for(double value:p)if(!std::isfinite(value))throw std::invalid_argument("Initial coefficients must be finite");
  const auto resolved=p;
  // NIH Image Rodbard fits measured(known), then publishes the inverse.
  if(id==10)yscale=std::max(1.0,xmax);
  const auto target=[&](std::size_t i){return (id==10?x[i]:y[i])/yscale;};
  const auto evaluate=[&](const std::vector<double>& parameters,std::vector<double>& values){
    values.resize(n);double cost=0;
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();const double value=function_value(id==10?7:id,parameters,id==10?y[i]:x[i])/yscale;
      if(!std::isfinite(value))return std::numeric_limits<double>::infinity();
      values[i]=value;const double r=target(i)-value;cost+=r*r;}
    return std::isfinite(cost)?cost:std::numeric_limits<double>::infinity();
  };
  using Matrix=std::array<std::array<double,4>,4>;using Vector=std::array<double,4>;
  const auto solve=[&](Matrix a,Vector rhs,Vector& delta,double damping){
    for(std::size_t j=0;j<m;++j)a[j][j]+=damping;
    for(std::size_t j=0;j<m;++j){std::size_t pivot=j;for(std::size_t k=j+1;k<m;++k)if(std::abs(a[k][j])>std::abs(a[pivot][j]))pivot=k;
      if(!std::isfinite(a[pivot][j]) || std::abs(a[pivot][j])<1e-12)return false;
      std::swap(a[j],a[pivot]);std::swap(rhs[j],rhs[pivot]);const double d=a[j][j];
      for(std::size_t k=j;k<m;++k){a[j][k]/=d;}rhs[j]/=d;
      for(std::size_t row=0;row<m;++row)if(row!=j){const double factor=a[row][j];for(std::size_t k=j;k<m;++k)a[row][k]-=factor*a[j][k];rhs[row]-=factor*rhs[j];}
    }
    delta=rhs;return true;
  };
  std::vector<double> values,plus,minus,candidate_values;double cost=evaluate(p,values),lambda=0.001;
  if(!std::isfinite(cost))throw std::invalid_argument("Initial model is not finite at the measured values");
  bool converged=false,small_reduction=false;std::string convergence_reason;std::size_t iterations=0;
  KernelStage fit_stage("Calibration nonlinear fit",max_iterations);
  for(;iterations<max_iterations;++iterations) {
    fit_stage.checkpoint(iterations);
    std::array<std::vector<double>,4> jacobian;Vector norm{},gradient{},delta{};Matrix gram{};
    for(std::size_t j=0;j<m;++j) {
      const double h=std::cbrt(std::numeric_limits<double>::epsilon())*std::max({std::abs(p[j]),std::abs(resolved[j]),1e-6});
      auto perturbed=p;perturbed[j]+=h;const bool positive=std::isfinite(evaluate(perturbed,plus));
      perturbed=p;perturbed[j]-=h;const bool negative=std::isfinite(evaluate(perturbed,minus));
      if(!positive && !negative)throw std::invalid_argument("Cannot evaluate finite fit derivatives; adjust starting coefficients");
      auto& column=jacobian[j];column.resize(n);
      for(std::size_t i=0;i<n;++i){column[i]=positive&&negative?(plus[i]-minus[i])/(2*h):positive?(plus[i]-values[i])/h:(values[i]-minus[i])/h;norm[j]=std::hypot(norm[j],column[i]);}
      if(!(norm[j]>0) || !std::isfinite(norm[j]))throw std::invalid_argument("Fit has an insensitive parameter; adjust starting coefficients or data");
      for(std::size_t i=0;i<n;++i){column[i]/=norm[j];gradient[j]+=column[i]*(target(i)-values[i]);}
    }
    for(std::size_t j=0;j<m;++j)for(std::size_t k=0;k<m;++k)for(std::size_t i=0;i<n;++i)gram[j][k]+=jacobian[j][i]*jacobian[k][i];
    double gmax=0;for(std::size_t j=0;j<m;++j)gmax=std::max(gmax,std::abs(gradient[j]));
    const bool small_residual=cost<=double(n)*tolerance*tolerance;
    const bool small_gradient=gmax<=tolerance*std::max(1.0,std::sqrt(cost));
    if(small_residual || small_gradient || small_reduction) {
      if(!solve(gram,gradient,delta,0))throw std::invalid_argument("Fit parameters are not identifiable from the supplied data");
      convergence_reason=small_residual?"residual":small_gradient?"gradient":"cost_reduction";
      converged=true;break;
    }
    if(!solve(gram,gradient,delta,lambda))throw std::invalid_argument("Fit linear step is singular");
    auto candidate=p;for(std::size_t j=0;j<m;++j)candidate[j]+=delta[j]/norm[j];
    const double next=evaluate(candidate,candidate_values);
    // Standard function-tolerance termination: require both actual and model
    // reductions to be small. Exclude tiny steps forced by excessive damping,
    // keep the better parameters, and recheck rank at the next iteration.
    double predicted=0;
    for(std::size_t j=0;j<m;++j){predicted+=2*gradient[j]*delta[j];for(std::size_t k=0;k<m;++k)predicted-=delta[j]*gram[j][k]*delta[k];}
    small_reduction=lambda<=1 && std::isfinite(next) && std::abs(next-cost)<=tolerance*cost &&
        std::abs(predicted)<=tolerance*cost;
    if(next<cost){p=std::move(candidate);values.swap(candidate_values);cost=next;lambda=std::max(1e-12,lambda/3);}
    else {lambda*=10;if(lambda>1e16)break;}
  }
  if(!converged)throw std::invalid_argument("Nonlinear fit did not converge; adjust starting coefficients or iteration limit");
  fit_stage.checkpoint(iterations);
  const double rmse=std::sqrt(cost/double(n))*yscale;
  if(!std::isfinite(rmse))throw std::invalid_argument("Fit residuals exceed the finite range");
  double mean=0,tss=0;for(std::size_t i=0;i<n;++i){mean+=target(i)/double(n);}
  MetadataArray measured,known,requested,start;
  for(std::size_t i=0;i<n;++i){const double d=target(i)-mean;tss+=d*d;measured.emplace_back(x[i]);known.emplace_back(y[i]);}
  for(double value:initial){requested.emplace_back(value);}for(double value:resolved){start.emplace_back(value);}
  auto result=function(type,name,p,std::move(unit),zero_clip);auto fields=object(result.record());auto parameters=result.source_parameters();
  const double r2=tss>0?1-cost/tss:std::numeric_limits<double>::quiet_NaN();
  parameters["fit"]=metadata_object({{"method",std::string("scaled_damped_least_squares")},{"version",std::int64_t(1)},
    {"objective",std::string(id==10?"measured_as_function_of_known":"known_as_function_of_measured")},
    {"measured",metadata_array(std::move(measured))},{"known",metadata_array(std::move(known))},
    {"initial_coefficients",metadata_array(std::move(requested))},{"resolved_initial_coefficients",metadata_array(std::move(start))},
    {"max_iterations",std::uint64_t(max_iterations)},{"relative_tolerance",tolerance},{"iterations",std::uint64_t(iterations)},
    {"convergence_reason",convergence_reason},
    {"rmse",rmse},{"r_squared",std::isfinite(r2)?MetadataValue(r2):MetadataValue(std::monostate{})}});
  fields["source_parameters"]=metadata_object(std::move(parameters));return from_record(metadata_object(std::move(fields)));
}

ImageJIntensityFitPlot imagej_intensity_fit_plot(const ImageJIntensityCalibration& input,
    std::size_t points,std::size_t max_working_bytes,const ExecutionControl& control) {
  const auto calibration=input; // immutable record owner retained across callbacks
  if(points<2 || points>1000)throw std::invalid_argument("Fit plot points must be2..1000");
  if(calibration.mode()!=ImageJIntensityMode::Lookup)throw std::invalid_argument("Fit plot requires fitted function provenance");
  const auto& provenance=calibration.source_parameters();
  const auto field=[](const MetadataObject& fields,const char* name)->const MetadataValue& {
    const auto it=fields.find(name);if(it==fields.end())throw std::invalid_argument(std::string("Missing fit plot field: ")+name);
    return it->second;
  };
  if(text(field(provenance,"producer"))!="imagej154r.function" || wire::unsigned_value(field(provenance,"version"))!=1)
    throw std::invalid_argument("Fit plot requires supported function provenance");
  const auto& name=text(field(provenance,"function"));
  if(name.size()>64)throw std::invalid_argument("Invalid fit plot function");
  const auto id=function_id(name);if(id==14)throw std::invalid_argument("Fit plot requires a fitted numeric function");
  const auto& coefficients=wire::array_value(field(provenance,"coefficients"));
  const auto& fit=object(field(provenance,"fit"));
  const auto& measured=wire::array_value(field(fit,"measured"));const auto& known=wire::array_value(field(fit,"known"));
  const auto n=measured.size();
  if(coefficients.size()!=function_lengths[id] || n!=known.size() || n<function_lengths[id] || n>4096)
    throw std::invalid_argument("Invalid fit plot coefficient or paired-data count");
  const auto count=points==256?256:std::min<std::size_t>(1000,std::max(points,n));
  using detail::contrast_command_record::add;using detail::contrast_command_record::mul;
  const auto bytes=add(add(64*1024,calibration.value_unit().size()),add(mul(n,2*sizeof(double)),mul(count,2*sizeof(float))));
  if(bytes>max_working_bytes)throw std::invalid_argument("Fit plot exceeds working-memory budget");
  ImageJIntensityFitPlot result;result.function=name;result.value_unit=calibration.value_unit();
  result.objective=id==10?"measured_as_function_of_known":"known_as_function_of_measured";
  std::vector<double> parameters;parameters.reserve(coefficients.size());
  for(const auto& v:coefficients){const auto number=wire::number_value(v);if(!std::isfinite(number))throw std::invalid_argument("Fit plot coefficients must be finite");parameters.push_back(number);}
  result.measured.reserve(n);result.known.reserve(n);
  const double raw_max=double(table_size(calibration.bit_depth())-1);
  for(std::size_t i=0;i<n;++i) {
    const auto x=wire::number_value(measured[i]),y=wire::number_value(known[i]);
    if(!std::isfinite(x) || !std::isfinite(y) || x<0 || x>raw_max)throw std::invalid_argument("Fit plot requires finite in-range paired data");
    result.measured.push_back(x);result.known.push_back(y);
  }
  const auto xr=std::minmax_element(result.measured.begin(),result.measured.end());
  const auto yr=std::minmax_element(result.known.begin(),result.known.end());
  result.x_min=points==256?0:*xr.first;result.x_max=points==256?255:*xr.second;
  result.y_min=*yr.first;result.y_max=*yr.second;
  if(result.x_min==result.x_max)throw std::invalid_argument("Fit plot measured range is degenerate");
  const auto range=result.y_max-result.y_min;
  if(!std::isfinite(range))throw std::invalid_argument("Fit plot data range exceeds finite limits");
  ExecutionScope scope(control);kernel_checkpoint();KernelStage stage("Calibration fit plot",count);
  result.curve_x.resize(count);result.curve_y.resize(count);
  const auto increment=(result.x_max-result.x_min)/double(count-1);double x=result.x_min;
  double curve_min=std::numeric_limits<double>::infinity(),curve_max=-curve_min;
  for(std::size_t i=0;i<count;++i) {
    stage.periodic(i);result.curve_x[i]=static_cast<float>(x);x+=increment;
    result.curve_y[i]=static_cast<float>(function_value(id,parameters,double(result.curve_x[i])));
    if(std::isfinite(result.curve_y[i])){curve_min=std::min(curve_min,double(result.curve_y[i]));curve_max=std::max(curve_max,double(result.curve_y[i]));}
  }
  if(curve_min<=curve_max) {
    result.y_min=std::max(result.y_min-range,std::min(result.y_min,curve_min));
    result.y_max=std::min(result.y_max+range,std::max(result.y_max,curve_max));
  }
  if(!std::isfinite(result.y_min) || !std::isfinite(result.y_max))throw std::invalid_argument("Fit plot limits exceed finite range");
  stage.finish();return result;
}
std::size_t estimate_imagej_intensity_fit(const DocumentSnapshot& source,const ImageJIntensityFitRequest& r) {
  using detail::contrast_command_record::add;using detail::contrast_command_record::mul;
  const auto& dataset=source.data().dataset;const auto count=table_size(depth(dataset.pixel_type()));
  if(dataset.shape().s!=1)throw std::invalid_argument("Intensity fitting requires scalar storage");
  const int id=function_id(r.function);if(id==14)throw std::invalid_argument("Optical density has no fitted coefficients");
  const auto n=r.measured.size(),m=function_lengths[id];
  if(n!=r.known.size() || n<m || n>4096)throw std::invalid_argument("Fit requires matching point counts from parameter count through4096");
  if(!r.max_working_bytes || !r.max_iterations || r.max_iterations>1024 || !std::isfinite(r.relative_tolerance) || r.relative_tolerance<=0 || r.relative_tolerance>0.01)
    throw std::invalid_argument("Invalid fitting iteration/tolerance/memory limits");
  if(r.value_unit.size()>1024*1024 || r.value_unit.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid fitting value unit");
  if((id<4 && !r.initial_coefficients.empty()) || (!r.initial_coefficients.empty() && r.initial_coefficients.size()!=m))throw std::invalid_argument("Wrong fitting initial coefficient count");
  for(std::size_t i=0;i<n;++i)if(!std::isfinite(r.measured[i]) || !std::isfinite(r.known[i]) || r.measured[i]<0 || r.measured[i]>double(count-1) || (id==6 && r.measured[i]<=0))
    throw std::invalid_argument("Invalid finite/range/domain fit points");
  for(double value:r.initial_coefficients)if(!std::isfinite(value))throw std::invalid_argument("Initial fit coefficients must be finite");
  detail::contrast_command_record::Budget input{r.max_working_bytes};input.text(r.value_unit);input.text(r.function);
  const auto metadata=detail::contrast_command_record::source_budget(source,r.max_working_bytes);
  const auto bytes=add(add(8ULL*1024*1024,mul(count,64)),add(mul(n,512),mul(add(metadata,input.used),4)));
  if(bytes>r.max_working_bytes)throw std::invalid_argument("Intensity fit exceeds working memory budget");
  return bytes;
}
ImageJIntensityCalibration ImageJIntensityCalibration::from_record(const MetadataValue& value) {
  const auto& fields = object(value);
  const auto found = fields.find("mode");
  if (found == fields.end()) throw std::invalid_argument("Intensity mode is missing");
  const auto& mode = text(found->second);
  if (mode == "lookup")
    wire::exact_fields(fields, {"schema", "version", "mode", "value_unit", "bit_depth",
                                "table_f32_hex", "source_parameters", "signed16"});
  else if (mode == "none" || mode == "signed16")
    wire::exact_fields(fields, {"schema", "version", "mode", "value_unit"});
  else throw std::invalid_argument("Unsupported intensity mode");
  if (text(fields.at("schema")) != schema || wire::unsigned_value(fields.at("version")) != 1)
    throw std::invalid_argument("Unsupported intensity schema/version");
  validate_unit(text(fields.at("value_unit")));
  if (mode != "lookup")
    return {value, mode == "none" ? ImageJIntensityMode::None : ImageJIntensityMode::Signed16,
            mode == "none" ? 0 : 16, mode == "signed16"};
  const auto bits = wire::index_value(fields.at("bit_depth"));
  const auto& encoded = text(fields.at("table_f32_hex"));
  if (encoded.size() != table_size(bits) * 8)
    throw std::invalid_argument("Intensity hexadecimal table has wrong length");
  for (char c : encoded) (void)nibble(c);
  const bool signed16 = boolean(fields.at("signed16"));
  if (signed16 && bits != 16)
    throw std::invalid_argument("Signed16 correction requires16-bit lookup storage");
  const auto& parameters = object(fields.at("source_parameters"));
  validate_metadata(parameters);
  try { (void)detail::session_metadata::to_json(parameters).dump(); }
  catch (const detail::session_metadata::Json::exception&) {
    throw std::invalid_argument("Intensity provenance must be UTF-8 metadata");
  }
  return {value, ImageJIntensityMode::Lookup, bits, signed16};
}
const std::string& ImageJIntensityCalibration::value_unit() const {
  return text(object(record_).at("value_unit"));
}
const MetadataObject& ImageJIntensityCalibration::source_parameters() const {
  static const MetadataObject empty;
  return mode_ == ImageJIntensityMode::Lookup
      ? object(object(record_).at("source_parameters")) : empty;
}
std::vector<std::uint32_t> ImageJIntensityCalibration::table_words() const {
  if (mode_ != ImageJIntensityMode::Lookup) return {};
  const auto& encoded = text(object(record_).at("table_f32_hex"));
  std::vector<std::uint32_t> words(table_size(depth_));
  for (std::size_t i = 0; i < words.size(); ++i)
    for (unsigned digit = 0; digit < 8; ++digit)
      words[i] = (words[i] << 4) | nibble(encoded[i * 8 + digit]);
  return words;
}
void ImageJIntensityCalibration::validate_for(PixelType type, int samples) const {
  const int bits = depth(type);
  if (samples != 1 && samples != 3 && samples != 4)
    throw std::invalid_argument("Invalid intensity sample count");
  if (has_density() && (samples != 1 || (bits != 8 && bits != 16) || bits != depth_))
    throw std::invalid_argument("Intensity calibration differs from source storage");
}
bool ImageJIntensityCalibration::shares_record(const ImageJIntensityCalibration& other) const {
  return std::get<std::shared_ptr<const MetadataTree>>(record_) ==
         std::get<std::shared_ptr<const MetadataTree>>(other.record_);
}
ImageJIntensityCalibration read_imagej_intensity_calibration(
    const DocumentMetadata& metadata, PixelType type, int samples) {
  const auto found = metadata.properties.find(imagej_intensity_property);
  auto calibration = found == metadata.properties.end()
      ? ImageJIntensityCalibration::uncalibrated()
      : ImageJIntensityCalibration::from_record(found->second);
  calibration.validate_for(type, samples);
  return calibration;
}
ImageJIntensityCalibration read_imagej_intensity_calibration(const DocumentSnapshot& source) {
  return read_imagej_intensity_calibration(source.data().metadata,
      source.data().dataset.pixel_type(), source.data().dataset.shape().s);
}
void set_imagej_intensity_calibration(Document& document, const ImageJIntensityCalibration& calibration) {
  const auto source = document.snapshot();
  calibration.validate_for(source.data().dataset.pixel_type(), source.data().dataset.shape().s);
  auto edit = source.begin_edit(Change::Pixels | Change::Metadata);
  if (!calibration.has_density() && calibration.value_unit() == "Gray Value")
    edit.remove_property(imagej_intensity_property);
  else edit.set_property(imagej_intensity_property, calibration.record());
  document.commit(edit);
}
void apply_imagej_intensity_calibration(const ImageJIntensityCalibration& calibration,
    PixelType type, int samples, ImageJDatasetProjectionRequest& request) {
  calibration.validate_for(type, samples);
  auto words = calibration.table_words();
  request.calibration_table = std::move(words);
  request.signed16_density = calibration.signed16_density();
}
ImageJIntensityCalibration projected_imagej_intensity_calibration(
    const ImageJIntensityCalibration& source, PixelType type, int samples, bool preserve) {
  (void)depth(type);
  if (samples != 1 && samples != 3 && samples != 4)
    throw std::invalid_argument("Invalid projection intensity sample count");
  if (preserve) {
    if (!source.has_density()) throw std::invalid_argument("No source density to preserve");
    source.validate_for(type, samples);
    return source;
  }
  if (samples != 1) return ImageJIntensityCalibration::uncalibrated();
  if (source.has_density() && (type == PixelType::UInt8 || type == PixelType::UInt16))
    throw std::invalid_argument("Projection integer output must preserve source density");
  return ImageJIntensityCalibration::uncalibrated(source.value_unit());
}
} // namespace imagejxx
