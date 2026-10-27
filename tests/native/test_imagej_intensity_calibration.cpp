#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/imagej_dataset_projection.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/session.hpp>
#include "axis_command_record.hpp"
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Intensity metadata assertion"); }
template<class F> void rejects(F call) {
  try { call(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("Invalid intensity metadata accepted");
}
ImageJIntensityCalibration mutate(const ImageJIntensityCalibration& source,
                                  const std::string& key, MetadataValue value) {
  auto object = detail::axis_command_record::object_value(source.record());
  object[key] = std::move(value);
  return ImageJIntensityCalibration::from_record(metadata_object(std::move(object)));
}
Dataset dataset(PixelType type, int samples=1) {
  Image image(1,1,samples,type); image.pixels.assign(samples,17);
  return Dataset::from_planes({1,1,1,1,1,samples},{image});
}
}
int main() {
  const auto none = ImageJIntensityCalibration::uncalibrated();
  check(!none.has_density() && none.mode()==ImageJIntensityMode::None);
  check(none.value_unit()=="Gray Value" && none.table_words().empty());
  const auto signed16 = ImageJIntensityCalibration::signed16("counts");
  check(signed16.has_density() && signed16.signed16_density() && signed16.bit_depth()==16);
  signed16.validate_for(PixelType::UInt16,1);
  rejects([&]{signed16.validate_for(PixelType::UInt8,1);});
  rejects([&]{signed16.validate_for(PixelType::UInt16,3);});
  rejects([&]{none.validate_for(static_cast<PixelType>(99),1);});
  rejects([&]{none.validate_for(PixelType::UInt8,2);});
  std::vector<std::uint32_t> words(256);
  for(std::size_t i=0;i<words.size();++i) words[i]=std::uint32_t(i*0x01010101U);
  words[1]=0x7fa12345U;words[17]=0x7f800000U;words[255]=0xffc12345U;
  const auto lookup=ImageJIntensityCalibration::lookup(PixelType::UInt8,words,
      "\xc2\xb5" "A",{{"function",std::int64_t(22)},{"note",std::string("owned provenance")}});
  check(lookup.mode()==ImageJIntensityMode::Lookup && !lookup.signed16_density());
  check(lookup.table_words()==words && lookup.value_unit()=="\xc2\xb5" "A");
  const auto decoded=ImageJIntensityCalibration::from_record(lookup.record());
  check(decoded.shares_record(lookup) && decoded.table_words()==words);
  check(metadata_equal(decoded.source_parameters(),lookup.source_parameters()));
  rejects([&]{ImageJIntensityCalibration::lookup(PixelType::UInt8,{1,2});});
  rejects([&]{ImageJIntensityCalibration::lookup(PixelType::Float32,words);});
  rejects([&]{ImageJIntensityCalibration::lookup(PixelType::UInt8,words,"units",{},true);});
  rejects([&]{ImageJIntensityCalibration::uncalibrated(std::string("bad\0unit",8));});
  rejects([&]{ImageJIntensityCalibration::uncalibrated(std::string(1,char(0xff)));});
  rejects([&]{ImageJIntensityCalibration::uncalibrated(std::string(1024*1024+1,'u'));});
  rejects([&]{mutate(lookup,"version",true);});
  rejects([&]{mutate(lookup,"version",double(1));});
  rejects([&]{mutate(lookup,"version",std::int64_t(2));});
  rejects([&]{mutate(lookup,"schema",std::string("other"));});
  rejects([&]{mutate(lookup,"extra",false);});
  rejects([&]{mutate(lookup,"bit_depth",true);});
  rejects([&]{mutate(lookup,"bit_depth",std::int64_t(32));});
  rejects([&]{mutate(lookup,"table_f32_hex",std::string("00"));});
  auto encoded=std::get<std::string>(detail::axis_command_record::object_value(lookup.record()).at("table_f32_hex"));
  encoded[0]='A';rejects([&]{mutate(lookup,"table_f32_hex",encoded);});
  rejects([&]{mutate(lookup,"source_parameters",std::monostate{});});
  rejects([&]{mutate(lookup,"signed16",std::int64_t(0));});
  rejects([&]{mutate(signed16,"table_f32_hex",std::string());});
  auto doc=std::make_shared<Document>(dataset(PixelType::UInt8),"calibrated");
  const auto original=doc->snapshot();
  check(!read_imagej_intensity_calibration(original).has_density());
  set_imagej_intensity_calibration(*doc,lookup);
  const auto set=doc->snapshot();
  check(set.revisions().pixels==original.revisions().pixels);
  check(set.revisions().metadata==original.revisions().metadata+1);
  check(set.revisions().selection==original.revisions().selection && set.revisions().display==original.revisions().display);
  check(set.data().dataset.shares_pixels(original.data().dataset));
  check(read_imagej_intensity_calibration(set).shares_record(lookup));
  check(doc->undo_steps()==1);
  set_imagej_intensity_calibration(*doc,decoded);
  check(doc->undo_steps()==1 && doc->snapshot().revisions().metadata==set.revisions().metadata);
  rejects([&]{set_imagej_intensity_calibration(*doc,signed16);});
  check(doc->snapshot().shares_data(set));
  set_imagej_intensity_calibration(*doc,none);
  check(doc->snapshot().data().metadata.properties.count(imagej_intensity_property)==0);
  doc->undo();check(read_imagej_intensity_calibration(doc->snapshot()).shares_record(lookup));
  doc->redo();check(!read_imagej_intensity_calibration(doc->snapshot()).has_density());
  doc->undo();
  SessionCapture capture;capture.documents.push_back(doc->snapshot());
  const auto bytes=encode_session(capture);
  const auto reopened=decode_session(bytes);
  check(reopened.documents.size()==1);
  const auto restored=read_imagej_intensity_calibration(reopened.documents[0]->snapshot());
  check(restored.table_words()==words && restored.value_unit()==lookup.value_unit());
  check(metadata_equal(restored.record(),lookup.record()));
  ImageJDatasetProjectionRequest request;
  request.first=0;request.last=0;request.method="median";
  apply_imagej_intensity_calibration(restored,PixelType::UInt8,1,request);
  check(request.calibration_table==words && !request.signed16_density);
  const auto projected=project_imagej_dataset(doc->snapshot().data().dataset,request);
  const auto retained=projected_imagej_intensity_calibration(restored,projected.dataset.pixel_type(),1,
                                                           projected.preserve_density_calibration);
  check(retained.shares_record(restored) && projected.dataset.plane()->pixels[0]==255);
  const auto cleared=projected_imagej_intensity_calibration(restored,PixelType::Float32,1,false);
  check(!cleared.has_density() && cleared.value_unit()==lookup.value_unit());
  check(projected_imagej_intensity_calibration(none,PixelType::UInt8,3,false).value_unit()=="Gray Value");
  rejects([&]{projected_imagej_intensity_calibration(restored,PixelType::UInt8,1,false);});
  rejects([&]{projected_imagej_intensity_calibration(none,PixelType::UInt8,1,true);});
  rejects([&]{projected_imagej_intensity_calibration(restored,PixelType::UInt16,1,true);});
  const auto unchanged=request.calibration_table;
  rejects([&]{apply_imagej_intensity_calibration(signed16,PixelType::UInt8,1,request);});
  check(request.calibration_table==unchanged);
  apply_imagej_intensity_calibration(none,PixelType::UInt8,1,request);
  check(request.calibration_table.empty() && !request.signed16_density);
  apply_imagej_intensity_calibration(signed16,PixelType::UInt16,1,request);
  check(request.calibration_table.empty() && request.signed16_density);
  auto future=set.data().metadata;
  auto record=detail::axis_command_record::object_value(lookup.record());record["version"]=std::int64_t(99);
  future.properties[imagej_intensity_property]=metadata_object(record);
  const auto before=future.properties;
  rejects([&]{read_imagej_intensity_calibration(future,PixelType::UInt8,1);});
  check(metadata_equal(before,future.properties));
  std::cout<<"Intensity immutable metadata, strict records, source edits/Undo, session roundtrip and projection intent passed\n";
}
