// Independent native snapshot/session assertions. Literal wire records below
// are authored here; encode_session never supplies a decoder golden.
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/session.hpp>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
namespace fs = std::filesystem;
std::size_t assertions = 0;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
  ++assertions;
}
template <class Error = std::exception, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const Error &) { rejected = true; }
  check(rejected, message);
}
std::uint64_t bits(double value) {
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
double number(std::uint64_t value) {
  double result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
std::vector<unsigned char> little(const std::vector<double> &values) {
  std::vector<unsigned char> result;
  for (double value : values)
    for (int shift = 0; shift < 64; shift += 8)
      result.push_back(static_cast<unsigned char>(bits(value) >> shift));
  return result;
}
std::string base64(const std::vector<unsigned char> &raw) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  for (std::size_t i = 0; i < raw.size(); i += 3) {
    const auto count = std::min<std::size_t>(3, raw.size() - i);
    const std::uint32_t value = (std::uint32_t(raw[i]) << 16) |
        (count > 1 ? std::uint32_t(raw[i + 1]) << 8 : 0) |
        (count > 2 ? raw[i + 2] : 0);
    result += alphabet[(value >> 18) & 63];
    result += alphabet[(value >> 12) & 63];
    result += count > 1 ? alphabet[(value >> 6) & 63] : '=';
    result += count > 2 ? alphabet[value & 63] : '=';
  }
  return result;
}
std::vector<unsigned char> frame(const std::string &json, int version = 3) {
  const std::string magic = version == 3 ? "IMAGEJXX3\n" : "IMAGEJXX2\n";
  std::vector<unsigned char> result(magic.begin(), magic.end());
  if (version == 3) {
    for (int shift = 0; shift < 64; shift += 8)
      result.push_back(static_cast<unsigned char>(std::uint64_t(json.size()) >> shift));
  } else {
    for (int shift = 24; shift >= 0; shift -= 8)
      result.push_back(static_cast<unsigned char>(json.size() >> shift));
  }
  uLongf size = compressBound(static_cast<uLong>(json.size()));
  const auto start = result.size();
  result.resize(start + size);
  check(compress2(result.data() + start, &size,
      reinterpret_cast<const Bytef *>(json.data()), static_cast<uLong>(json.size()),
      Z_DEFAULT_COMPRESSION) == Z_OK, "Independent fixture compression failed");
  result.resize(start + size);
  return result;
}
std::string unframe(const std::vector<unsigned char> &data) {
  check(data.size() >= 18 && std::string(data.begin(), data.begin() + 10) == "IMAGEJXX3\n",
        "Encoder did not emit the explicit V3 frame");
  std::uint64_t declared = 0;
  for (int byte = 0; byte < 8; ++byte) declared |= std::uint64_t(data[10 + byte]) << (8 * byte);
  check(declared < 32ULL * 1024 * 1024, "Test archive exceeded 32 MiB");
  std::string text(static_cast<std::size_t>(declared), '\0');
  uLongf size = static_cast<uLongf>(declared);
  uLong consumed = static_cast<uLong>(data.size() - 18);
  check(uncompress2(reinterpret_cast<Bytef *>(text.data()), &size, data.data() + 18, &consumed) == Z_OK &&
        size == declared && consumed == data.size() - 18,
        "V3 encoder frame has an incorrect declared size or trailing stream");
  return text;
}
SessionLimits limits() {
  SessionLimits result;
  result.max_documents = 16;
  result.max_json_bytes = result.max_encoded_bytes = 32ULL * 1024 * 1024;
  result.max_working_bytes = 128ULL * 1024 * 1024;
  return result;
}
std::string literal_document(const std::vector<double> &values,
                             const std::string &type = "uint8",
                             const std::string &policy = "finite_only") {
  return std::string(R"({"axes":"TCZYXS","shape":[1,1,1,2,3,1],"pixel_type":")") + type +
      R"(","sample_policy":")" + policy + R"(","storage":"Float64LE","planes":[")" + base64(little(values)) +
      R"("],"calibration":{"spacing":[1.0,1.0,1.0],"origin":[0.0,0.0,0.0],"direction":[1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0],"unit":"px","time_unit":"s","frame_interval":1.0,"time_origin":0.0},"metadata":{"title":"handwritten","properties":{"integer":1,"double":1.0,"negative_zero":-0.0,"unsigned_max":18446744073709551615},"extensions":{"native_document_state":{"opaque":true}},"record_layout":{}},"display":{"t":0,"c":0,"crosshair":[2,1,0],"orthogonal":false,"linked_zoom":true,"zoom":0.0,"plane_zoom":[0.0,0.0,0.0],"pan":[[0.0,0.0],[0.0,0.0],[0.0,0.0]],"properties":{"lut":"Fire"}},"selection":null,"previous_selection":null,"rois":[],"overlays":[],"fourier":null})";
}
std::string envelope(const std::string &document) {
  return R"({"format":"imagejxx-session","version":3,"documents":[)" + document +
      R"(],"results":[{"value":7}],"workflow":[],"extensions":{"future":{"version":1}}})";
}
void replace_once(std::string &value, const std::string &from, const std::string &to) {
  const auto at = value.find(from);
  check(at != std::string::npos, "Independent fixture patch locator missing");
  value.replace(at, from.size(), to);
}
void exact_pixels(const Dataset &dataset, const DatasetShape &shape,
                   PixelType type, const std::vector<double> &expected) {
  check(dataset.shape().dimensions() == shape.dimensions() && dataset.pixel_type() == type,
        "Session changed explicit axes or declared pixel type");
  std::size_t index = 0;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto plane = dataset.plane({t, c, z});
        for (double value : plane->pixels)
          check(index < expected.size() && bits(value) == bits(expected[index++]),
                "Session changed native binary64 sample bits or plane/sample ordering");
      }
  check(index == expected.size(), "Session lost native samples");
}
void clean_owner(const std::shared_ptr<Document> &document) {
  const auto value = document->snapshot();
  check(!value.dirty() && document->undo_steps() == 0 && document->redo_steps() == 0 &&
        value.revisions().pixels == 0 && value.revisions().metadata == 0 &&
        value.revisions().selection == 0 && value.revisions().display == 0 &&
        value.display().revision == 0, "Loaded owner is a synthetic dirty/history edit");
}

void handwritten_v3() {
  const std::vector<double> noncanonical{-1, .5, 300, 0, -0.0, 255};
  auto decoded = decode_session(frame(envelope(literal_document(noncanonical))), limits());
  check(decoded.source_version == 3 && decoded.documents.size() == 1,
        "Handwritten V3 was not admitted as exactly one owner");
  exact_pixels(decoded.documents[0]->snapshot().data().dataset, {1,1,1,2,3,1},
                 PixelType::UInt8, noncanonical);
  clean_owner(decoded.documents[0]);
  const auto first = decoded.documents[0]->snapshot();
  const auto &properties = first.data().metadata.properties;
  check(std::holds_alternative<double>(properties.at("double")) &&
        !std::holds_alternative<double>(properties.at("integer")) &&
        bits(std::get<double>(properties.at("negative_zero"))) == 0x8000000000000000ULL &&
        std::get<std::uint64_t>(properties.at("unsigned_max")) == std::numeric_limits<std::uint64_t>::max(),
        "Typed metadata lost number kind, signed zero or uint64 range");
  SessionCapture capture{{decoded.documents[0]->snapshot()}, decoded.results, decoded.workflow, decoded.extensions};
  auto bytes = encode_session(capture, limits());
  const auto json = unframe(bytes);
  check(json.find(base64(little(noncanonical))) != std::string::npos,
        "Re-encoding quantized the admitted UInt8 native backing");
  const std::vector<double> f32{1 + std::ldexp(1.0, -40), -0.0, .125, -3, 2, 0};
  decoded = decode_session(frame(envelope(literal_document(f32, "float32"))), limits());
  exact_pixels(decoded.documents[0]->snapshot().data().dataset, {1,1,1,2,3,1}, PixelType::Float32, f32);
  const std::vector<double> quiet{0, -0.0, number(0x36a0000000000000ULL),
      number(0x7ff8000020000000ULL), number(0xfff8000020000000ULL), 1};
  decoded = decode_session(frame(envelope(literal_document(quiet, "float32", "preserve_quiet_nan_float32"))), limits());
  exact_pixels(decoded.documents[0]->snapshot().data().dataset, {1,1,1,2,3,1}, PixelType::Float32, quiet);
  check(decoded.documents[0]->snapshot().data().dataset.sample_policy() ==
        FloatingSamplePolicy::PreserveQuietNaNFloat32, "Quiet-NaN source policy changed");
}

void scientific_axes() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64}) {
    const DatasetShape shape{2,3,4,5,7,1};
    Stack planes;
    std::vector<double> expected;
    for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z) {
      Image plane(7,5,1,type);
      for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
        double value = type == PixelType::UInt8 ? (73*t+41*c+17*z+7*y+x)%251 :
            10000*t+1000*c+100*z+10*y+x;
        if (type == PixelType::Float32) value += .125;
        plane.at(x,y) = value;
        expected.push_back(value);
      }
      planes.push_back(std::move(plane));
    }
    auto owner = std::make_shared<Document>(Dataset::from_planes(shape, std::move(planes)), "axes");
    const auto before = owner->snapshot();
    const SessionCapture capture{{before}, {}, {}, {}};
    const auto estimate = estimate_session(capture, limits());
    check(estimate.documents == 1 && estimate.planes == 24 && estimate.samples == 840 &&
          estimate.source_bytes >= 840*8, "Scientific estimate flattened or omitted axes");
    const auto decoded = decode_session(encode_session(capture, limits()), limits());
    exact_pixels(decoded.documents[0]->snapshot().data().dataset, shape, type, expected);
    check(decoded.documents[0]->id != owner->id && !decoded.documents[0]->snapshot().same_document(before),
          "Loaded session reused the captured live owner identity");
    clean_owner(decoded.documents[0]);
    check(owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
          "Codec changed its source document");
  }
  for (int samples : {3,4}) {
    DatasetShape shape{2,3,1,2,3,samples};
    Stack planes;
    std::vector<double> expected;
    for (int t=0;t<2;++t) for (int c=0;c<3;++c) {
      Image image(3,2,samples,PixelType::UInt8);
      for(int y=0;y<2;++y) for(int x=0;x<3;++x) for(int s=0;s<samples;++s) {
        const double value=(73*t+41*c+7*y+3*x+s)%251;
        image.at(x,y,s)=value; expected.push_back(value);
      }
      planes.push_back(std::move(image));
    }
    Document owner(Dataset::from_planes(shape,std::move(planes)));
    const auto decoded=decode_session(encode_session({{owner.snapshot()}, {}, {}, {}},limits()),limits());
    exact_pixels(decoded.documents[0]->snapshot().data().dataset,shape,PixelType::UInt8,expected);
  }
}

void annotations_and_capture() {
  Stack planes(24,Image(7,5,1,PixelType::Float64));
  SpatialCalibration cal;
  cal.spacing={.25,1.5,3}; cal.origin={-7,11,-13};
  cal.direction={-1,.5,-0.0,0,1,.25,0,0,1}; cal.unit="um";
  cal.frame_interval=.75; cal.time_origin=2.5;
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,3,4,5,7,1},std::move(planes),cal));
  auto edit=owner->begin_edit();
  auto selected=Selection::from_packed_mask(7,5,{0x00,0xe1,0x43,0x80,0x3f});
  selected.attributes["vendor"]=metadata_object({{"negative_zero",-0.0}});
  edit.select(selected);
  edit.set_previous_selection(Selection("rectangle",{{0,0},{99,77}}));
  edit.set_rois({Roi{"unpositioned",selected,{},{},{},{}}, Roi{"zero",selected,0,0,0,{}},
                 Roi{"last",selected,1,2,3,{{"vendor",std::string("kept")}}}});
  edit.set_property("integer",std::int64_t(1)); edit.set_property("double",1.0);
  edit.set_property("negative_zero",-0.0);
  edit.set_extension("native_document_state",metadata_object({{"opaque",true}}));
  edit.set_record_layout({{"opaque",metadata_array({std::monostate{},-0.0})}});
  edit.set_overlays({{{"kind",std::string("text")},{"text",std::string("kept")}}});
  owner->commit(edit);
  auto display=owner->snapshot().display();
  display.t=1;display.c=2;display.crosshair={6,4,3};display.orthogonal=true;display.linkedZoom=false;
  display.zoom=2.5;display.planeZoom={1.25,0,4};display.pan={{{3,-4},{-.5,.25},{0,-0.0}}};
  display.properties["lut"]=std::string("Fire");
  set_manual_threshold_state(display,{2,3,4,5,7,1},{{-0.0,12346.125},{0,1,2}});
  owner->set_display(display);
  const auto captured=owner->snapshot();
  SessionCapture capture{{captured},{metadata_object({{"value",std::int64_t(7)}})}, {},
                          {{"future_channels",metadata_object({{"version",std::int64_t(1)}})}}};
  auto newer=owner->begin_edit();newer.set_title("newer live title");owner->commit(newer);
  const auto decoded=decode_session(encode_session(capture,limits()),limits());
  const auto loaded=decoded.documents[0]->snapshot();
  check(loaded.data().metadata.title==captured.data().metadata.title && owner->snapshot().dirty(),
        "Serialization retargeted newer live state or marked it saved");
  const auto &data=loaded.data();
  check(data.selection && data.selection->packed_mask()==std::vector<unsigned char>({0,0xe1,0x43,0x80,0x3f}) &&
        data.previous_selection && data.previous_selection->points[1]==std::array<double,2>{99,77},
        "Selection holes, unused tail bits or previous larger geometry changed");
  check(data.rois.size()==3 && !data.rois[0].t && !data.rois[0].c && !data.rois[0].z &&
        data.rois[1].t==0 && data.rois[1].c==0 && data.rois[1].z==0 &&
        data.rois[2].t==1 && data.rois[2].c==2 && data.rois[2].z==3,
        "Scientific ROI optional positions were flattened");
  check(metadata_equal(data.metadata.properties,captured.data().metadata.properties) &&
        metadata_equal(data.metadata.extensions,captured.data().metadata.extensions) &&
        metadata_equal(data.metadata.record_layout,captured.data().metadata.record_layout) &&
        metadata_equal(data.overlays[0],captured.data().overlays[0]) &&
        metadata_equal(decoded.results,capture.results) && metadata_equal(decoded.extensions,capture.extensions),
        "Native metadata/workspace compartments changed");
  const auto &view=loaded.display();
  check(view.t==1 && view.c==2 && view.crosshair==display.crosshair && view.orthogonal && !view.linkedZoom &&
        view.zoom==2.5 && view.planeZoom==display.planeZoom && view.pan==display.pan &&
        bits(view.pan[2][1])==0x8000000000000000ULL,
        "Scientific view/camera state changed");
  const auto threshold=manual_threshold_state(view,data.dataset.shape());
  check(threshold && threshold->position.t==0 && threshold->position.c==1 && threshold->position.z==2 &&
        bits(threshold->range.lower)==0x8000000000000000ULL,
        "Threshold captured position or signed zero changed");
  const auto &restored_cal=data.dataset.calibration();
  check(restored_cal.voxel_to_world({6,4,3})==std::array<double,3>{-5.5,19.25,-4} &&
        restored_cal.time(1)==3.25 && bits(restored_cal.direction[2])==0x8000000000000000ULL,
        "Scientific geometry/time or direction bits changed");
  clean_owner(decoded.documents[0]);
}

void malformed_and_limits() {
  const auto literal=envelope(literal_document({0,1,2,3,4,5}));
  const auto valid=frame(literal);
  for(const auto &replacement:std::vector<std::pair<std::string,std::string>>{
      {"\"version\":3","\"version\":4"},{"\"axes\":\"TCZYXS\"","\"axes\":\"ZYX\""},
      {"[1,1,1,2,3,1]","[1,1,1,0,3,1]"},{"[1,1,1,2,3,1]","[1,1,1,2,3,2]"},
      {"\"storage\":\"Float64LE\"","\"storage\":\"Float64BE\""},
      {"\"sample_policy\":\"finite_only\"","\"sample_policy\":\"unknown\""},
      {"\"t\":0,\"c\":0","\"t\":1,\"c\":0"},
      {"\"version\":3","\"version\":3,\"version\":3"}}) {
    auto bad=literal;replace_once(bad,replacement.first,replacement.second);
    rejects([&]{decode_session(frame(bad),limits());},"Malformed V3 record accepted");
  }
  auto truncated=valid;truncated.pop_back();
  rejects([&]{decode_session(truncated,limits());},"Truncated stream accepted");
  auto trailing=valid;trailing.push_back(0);
  rejects([&]{decode_session(trailing,limits());},"Trailing compressed bytes accepted");
  auto extra=valid;extra[10]++;
  rejects([&]{decode_session(extra,limits());},"Wrong declared JSON length accepted");
  auto wrong=literal;replace_once(wrong,base64(little({0,1,2,3,4,5})),"AAAA");
  rejects([&]{decode_session(frame(wrong),limits());},"Wrong sample payload length accepted");
  auto nonfinite=envelope(literal_document({0,1,2,3,4,std::numeric_limits<double>::infinity()}));
  rejects([&]{decode_session(frame(nonfinite),limits());},"Nonfinite finite-only payload accepted");
  auto deep=literal;
  replace_once(deep,"\"extensions\":{\"future\":{\"version\":1}}",
      "\"extensions\":{\"deep\":"+std::string(200,'[')+"0"+std::string(200,']')+"}");
  rejects([&]{decode_session(frame(deep),limits());},"Excessive metadata depth accepted");
  for(int cap=0;cap<5;++cap) {
    auto bounded=limits();
    if(cap==0)bounded.max_documents=0;
    if(cap==1)bounded.max_samples=5;
    if(cap==2)bounded.max_json_bytes=1;
    if(cap==3)bounded.max_encoded_bytes=valid.size()-1;
    if(cap==4)bounded.max_working_bytes=1;
    rejects([&]{decode_session(valid,bounded);},"Session admission limit ignored");
  }
  auto token=std::make_shared<CancellationToken>();token->cancel();
  rejects<OperationCancelled>([&]{decode_session(valid,limits(),{token,{}});},"Pre-canceled decode accepted");
}

void v2_migration() {
  const auto payload=base64(little({0,1,2,3,4,5}));
  const auto json=std::string(R"({"format":"imagejxx-session","version":2,"documents":[{"title":"legacy","is_stack":false,"dtype":"uint16","shape":[2,3],"pixels":")")+
      payload+R"(","slice_index":0,"rois":[],"overlays":[]}],"results":[],"workflow":[]})";
  const auto decoded=decode_session(frame(json,2),limits());
  check(decoded.source_version==2 && decoded.documents.size()==1,"V2 migration lost source version");
  exact_pixels(decoded.documents[0]->snapshot().data().dataset,{1,1,1,2,3,1},PixelType::UInt16,{0,1,2,3,4,5});
  clean_owner(decoded.documents[0]);
  const auto output=encode_session({{decoded.documents[0]->snapshot()}, {}, {}, {}},limits());
  check(std::string(output.begin(),output.begin()+10)=="IMAGEJXX3\n","Scientific encoder masqueraded as V2");
}

class UnreadSource final : public PlaneSource {
public:
  mutable int reads=0;
  DatasetShape shape() const override{return {1,1,1,2,3,1};}
  PixelType pixel_type() const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("Session must reject this provider before reading");
  }
};
void source_and_fourier_admission() {
  const auto source=std::make_shared<UnreadSource>();
  Document virtual_owner{Dataset(source)};
  const SessionCapture lazy{{virtual_owner.snapshot()}, {}, {}, {}};
  rejects([&]{estimate_session(lazy,limits());},"Lazy estimate was silently materialized");
  rejects([&]{encode_session(lazy,limits());},"Lazy source was silently serialized");
  check(source->reads==0,"Unsupported-source admission read pixels");
  Image plane(3,2,1,PixelType::Float64);
  Document owner(Dataset::from_stack({plane}));
  Spectrum spectrum{3,2,{{1,-0.0},{2,.5},{-3,1.25},{0,-2},{.25,4},{5,0}}};
  auto edit=owner.begin_edit();edit.set_spectrum(FourierState(spectrum,std::array<int,2>{1,2}));owner.commit(edit);
  const auto decoded=decode_session(encode_session({{owner.snapshot()}, {}, {}, {}},limits()),limits());
  const auto state=decoded.documents[0]->snapshot();
  check(state.data().spectrum && state.data().spectrum->original_shape()==std::array<int,2>{1,2},
        "Eager-Z Fourier original crop changed");
  const auto &actual=state.data().spectrum->spectrum();
  check(actual.width==3 && actual.height==2 && actual.pixels.size()==6,"Fourier layout changed");
  for(std::size_t i=0;i<6;++i)
    check(bits(actual.pixels[i].real())==bits(spectrum.pixels[i].real()) &&
          bits(actual.pixels[i].imag())==bits(spectrum.pixels[i].imag()),"Fourier complex bits changed");
  DocumentData scientific{Dataset::from_planes({2,1,1,2,3,1},{plane,plane})};
  scientific.spectrum=FourierState(spectrum);
  Document unsupported(std::move(scientific));
  rejects([&]{estimate_session({{unsupported.snapshot()}, {}, {}, {}},limits());},
          "Scientific Fourier association was silently invented");
}

void atomic_batch_adoption() {
  const auto fresh=[] {
    return std::make_shared<Document>(Dataset::from_stack({Image(3,2,1,PixelType::UInt8)}));
  };
  Application application;
  const auto first=fresh(),second=fresh(),third=fresh();
  check(application.adopt_all({first,second},false)==std::vector<std::uint64_t>({first->id,second->id}) &&
        application.document_ids()==std::vector<std::uint64_t>({first->id,second->id}) &&
        application.active_document_id()==first->id,"Initial inactive batch order/activation changed");
  application.adopt_all({third},false);
  check(application.active_document_id()==first->id,"Inactive append changed active owner");
  const auto fourth=fresh();application.adopt_all({fourth});
  check(application.active_document_id()==fourth->id,"Active batch did not select its last new owner");
  const auto closed=fresh();application.adopt(closed);application.close(closed->id,true);
  const auto before=application.document_ids();const auto active=application.active_document_id();
  check(application.adopt_all({}).empty() && application.document_ids()==before &&
        application.active_document_id()==active,"Empty adoption changed registry");
  for(int bad=0;bad<4;++bad) {
    const auto incoming=fresh();
    std::vector<std::shared_ptr<Document>> batch{incoming};
    batch.push_back(bad==0?nullptr:bad==1?incoming:bad==2?first:closed);
    rejects([&]{application.adopt_all(batch);},"Invalid late batch member partially attached");
    check(application.document_ids()==before && application.active_document_id()==active &&
          !application.contains(incoming->id) && !incoming->snapshot().dirty(),
          "Rejected batch changed membership/order/active/incoming ownership");
    auto edit=incoming->begin_edit();edit.set_title("still usable");incoming->commit(edit);
    check(incoming->snapshot().data().metadata.title=="still usable","Rejected batch damaged independent owner");
  }
  const auto decoded=decode_session(frame(R"({"format":"imagejxx-session","version":3,"documents":[)"+
      literal_document({0,1,2,3,4,5})+","+literal_document({6,7,8,9,10,11})+
      R"(],"results":[],"workflow":[],"extensions":{}})"),limits());
  const auto adopted=application.adopt_all(decoded.documents);
  check(adopted==std::vector<std::uint64_t>({decoded.documents[0]->id,decoded.documents[1]->id}) &&
        application.active_document_id()==decoded.documents[1]->id &&
        application.document_ids().size()==before.size()+2,"Decoded owners were not atomically appended in order");
  clean_owner(decoded.documents[0]);clean_owner(decoded.documents[1]);
}

std::vector<unsigned char> read_file(const fs::path &path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void write_file(const fs::path &path,const std::vector<unsigned char> &bytes) {
  std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  check(bool(stream),"Owned fixture file write failed");
}
std::set<fs::path> files(const fs::path &directory) {
  std::set<fs::path> result;
  for(const auto &entry:fs::directory_iterator(directory))result.insert(entry.path().filename());
  return result;
}
void atomic_io(const fs::path &directory) {
  Image plane(3,2,1,PixelType::UInt16);plane.pixels={0,1,2,3,4,5};
  Document owner(Dataset::from_stack({plane}));
  const auto captured=owner.snapshot();
  const SessionCapture capture{{captured}, {}, {}, {}};
  const auto destination=directory/fs::u8path(u8"session-\u03bc.ijx");
  const auto unrelated=directory/"unrelated.ijx.tmp";
  const std::vector<unsigned char> sentinel{0,255,1,2,3};
  write_file(destination,sentinel);write_file(unrelated,{7,8,9});
  const auto expected_files=files(directory);
  for(const auto *phase:{"Atomic file write","Atomic file replace"}) {
    ExecutionControl control;
    bool reached=false;
    control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t) {
      if(stage==phase && (stage=="Atomic file replace" || done>0)) {
        reached=true;throw std::runtime_error("Owned pre-publication failure");
      }
    };
    rejects([&]{save_session(destination.u8string(),capture,limits(),control);},"Injected save failure was ignored");
    check(reached && read_file(destination)==sentinel && read_file(unrelated)==std::vector<unsigned char>({7,8,9}) &&
          files(directory)==expected_files && owner.snapshot().shares_data(captured),
          "Failed save changed destination, source or unrelated temporary");
  }
  auto token=std::make_shared<CancellationToken>();
  ExecutionControl cancel;cancel.cancellation=token;
  cancel.stage_progress=[token](std::string_view stage,std::size_t,std::size_t) {
    if(stage=="Atomic file replace")token->cancel();
  };
  rejects<OperationCancelled>([&]{save_session(destination.u8string(),capture,limits(),cancel);},"Pre-replace cancellation published");
  check(read_file(destination)==sentinel && files(directory)==expected_files,"Canceled save replaced destination or leaked its temp");
  save_session(destination.u8string(),capture,limits());
  const auto decoded=load_session(destination.u8string(),limits());
  exact_pixels(decoded.documents[0]->snapshot().data().dataset,{1,1,1,2,3,1},PixelType::UInt16,{0,1,2,3,4,5});
  check(files(directory)==expected_files && !owner.snapshot().dirty(),"Successful save leaked temp or mutated owner");
  auto read_token=std::make_shared<CancellationToken>();
  ExecutionControl read_cancel;read_cancel.cancellation=read_token;
  read_cancel.stage_progress=[read_token](std::string_view stage,std::size_t done,std::size_t) {
    if(stage=="Session file read" && done>0)read_token->cancel();
  };
  const auto published_bytes=read_file(destination);
  rejects<OperationCancelled>([&]{load_session(destination.u8string(),limits(),read_cancel);},
                              "Canceled input read returned owners");
  check(read_file(destination)==published_bytes && files(directory)==expected_files &&
        owner.snapshot().shares_data(captured),"Canceled read modified source, files or owner");
  const auto missing=directory/"missing-parent"/"session.ijx";
  rejects([&]{save_session(missing.u8string(),capture,limits());},"Save silently created parent directories");
  check(!fs::exists(missing.parent_path()),"Failed save created the absent parent");
}
} // namespace

int main(int argc,char **argv) {
  try {
    check(argc==2 || argc==3,"Supply a fresh session artifact directory and optional group");
    const auto directory=fs::absolute(fs::u8path(argv[1]));
    check(!fs::exists(directory),"Session test artifact directory must be fresh");
    fs::create_directories(directory);
    const std::string group=argc==3?argv[2]:"all";
    bool matched=false;
    const auto run=[&](const char *name,auto function) {
      if(group=="all" || group==name){matched=true;function();std::cout<<"PASS session."<<name<<'\n';}
    };
    run("literal",handwritten_v3);run("axes",scientific_axes);run("state",annotations_and_capture);
    run("malformed",malformed_and_limits);run("v2",v2_migration);
    run("admission",source_and_fourier_admission);run("batch",atomic_batch_adoption);
    run("io",[&]{atomic_io(directory);});
    check(matched,"Unknown session test group");
    std::cout<<"session assertions="<<assertions<<'\n';return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL session: "<<error.what()<<'\n';return 1;}
}
