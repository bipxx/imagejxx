#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/roi_style.hpp>
#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx {
namespace {
using Json = nlohmann::json;
namespace codec = detail::session_metadata;
constexpr std::size_t max_rows=4096, max_vertices=65536, max_bytes=16*1024*1024, max_nodes=1024*1024;
void require(bool condition,const char *message) {
  if(!condition)throw std::invalid_argument(message);
}
void fields(const Json &v,std::initializer_list<const char *> keys) {
  require(v.is_object() && v.size()==keys.size(),"Invalid planar annotation fields");
  for(auto key:keys)require(v.contains(key),"Missing planar annotation field");
}
int index(const Json &v,bool positive=false) {
  require(v.is_number_integer() && v>=0 && v<=std::numeric_limits<int>::max() && (!positive || v>0),
          "Planar annotation index must be an exact nonnegative native integer");
  return v.get<int>();
}
std::optional<int> optional_index(const Json &v) {return v.is_null()?std::optional<int>{}:index(v);}
const char *plane_name(Plane plane) {
  switch(plane) {case Plane::XY:return "XY";case Plane::XZ:return "XZ";case Plane::YZ:return "YZ";}
  throw std::invalid_argument("Invalid annotation plane");
}
void charge(const MetadataValue &value,std::size_t &nodes,std::size_t &bytes) {
  require(++nodes<=max_nodes,"Planar annotation metadata node cap");
  auto add=[&](std::size_t n) {require(n<=max_bytes-bytes,"Planar annotation metadata byte cap");bytes+=n;};
  add(32);
  if(auto text=std::get_if<std::string>(&value))add(text->size());
  else if(auto strings=std::get_if<std::vector<std::string>>(&value)) {
    require(strings->size()<=max_nodes-nodes,"Planar annotation string-list cap");nodes+=strings->size();
    for(const auto &text:*strings) {add(32);add(text.size());}
  } else if(auto tree=std::get_if<std::shared_ptr<const MetadataTree>>(&value);tree && *tree) {
    if(auto list=std::get_if<MetadataArray>(&(*tree)->value))
      for(const auto &item:*list)charge(item,nodes,bytes);
    else for(const auto &[key,item]:std::get<MetadataObject>((*tree)->value)) {add(key.size());charge(item,nodes,bytes);}
  }
}
void preflight(const MetadataValue &value) {
  validate_metadata(value);std::size_t nodes=0,bytes=0;charge(value,nodes,bytes);
}
Json extent(DatasetShape shape) {shape.validate();return Json::array({shape.t,shape.c,shape.z,shape.y,shape.x});}
Json row_json(const PlanarAnnotation &row) {
  return {{"name",row.name},{"selection",detail::selection_codec::encode_selection(row.selection)},
      {"plane",plane_name(row.plane)},{"coordinate",row.coordinate},
      {"t",row.t?Json(*row.t):Json()},{"c",row.c?Json(*row.c):Json()},
      {"attributes",codec::to_json(row.attributes)}};
}
} // namespace

PlanarAnnotationGeometry planar_annotation_geometry(DatasetShape shape,Plane plane) {
  shape.validate();
  switch(plane) {
  case Plane::XY:return {shape.x,shape.y,0,1,2};
  case Plane::XZ:return {shape.x,shape.z,0,2,1};
  case Plane::YZ:return {shape.y,shape.z,1,2,0};
  }
  throw std::invalid_argument("Invalid annotation plane");
}
void PlanarAnnotation::validate(DatasetShape shape) const {
  const auto geometry=planar_annotation_geometry(shape,plane);
  const std::array<int,3> dimensions{shape.x,shape.y,shape.z};
  require(coordinate>=0 && coordinate<dimensions[geometry.fixed_axis],"Annotation fixed coordinate outside source");
  require(!t || (*t>=0 && *t<shape.t),"Annotation time outside source");
  require(!c || (*c>=0 && *c<shape.c),"Annotation channel outside source");
  require(selection.points.size()<=max_vertices,"Annotation vertex cap");
  selection.validate(geometry.width,geometry.height);
  roi_style(selection);validate_metadata(attributes);
}
bool PlanarAnnotation::operator==(const PlanarAnnotation &v) const {
  return name==v.name && selection==v.selection && plane==v.plane && coordinate==v.coordinate &&
         t==v.t && c==v.c && metadata_equal(attributes,v.attributes);
}
std::array<double,3> planar_annotation_voxel(const PlanarAnnotation &row,std::array<double,2> point) {
  require(row.coordinate>=0,"Annotation fixed coordinate must be nonnegative");
  for(double value:point)require(std::isfinite(value),"Annotation boundary must be finite");
  // Descriptor supplies only the axis permutation here, without a dataset/read.
  const auto axes=planar_annotation_geometry({1,1,1,1,1,1},row.plane);
  std::array<double,3> result{};result[axes.fixed_axis]=row.coordinate;
  result[axes.horizontal_axis]=point[0]-.5;result[axes.vertical_axis]=point[1]-.5;
  return result;
}
std::array<double,3> planar_annotation_world(const SpatialCalibration &cal,const PlanarAnnotation &row,
                                           std::array<double,2> point) {
  cal.validate();return cal.voxel_to_world(planar_annotation_voxel(row,point));
}
std::array<double,2> planar_annotation_boundary(const SpatialCalibration &cal,const PlanarAnnotation &row,
                                              std::array<double,3> point) {
  cal.validate();const auto voxel=cal.world_to_voxel(point);
  const auto axes=planar_annotation_geometry({1,1,1,1,1,1},row.plane);
  require(std::abs(voxel[axes.fixed_axis]-row.coordinate)<=1e-8,"World point is outside the annotation plane");
  return {voxel[axes.horizontal_axis]+.5,voxel[axes.vertical_axis]+.5};
}
bool planar_annotation_visible(const PlanarAnnotation &row,Plane plane,int coordinate,int t,int c) {
  return row.plane==plane && row.coordinate==coordinate && (!row.t || *row.t==t) && (!row.c || *row.c==c);
}
MetadataValue encode_planar_annotations(DatasetShape shape,const std::vector<PlanarAnnotation> &rows) {
  require(rows.size()<=max_rows,"Planar annotation row cap");
  Json list=Json::array();
  // Preflight borrowed geometry/metadata before serializing owned record copies.
  std::size_t nodes=0,bytes=0;
  for(const auto &row:rows) {
    row.validate(shape);require(row.name.size()<=max_bytes-bytes,"Annotation name cap");bytes+=row.name.size();
    require(row.selection.points.size()<=(max_bytes-bytes)/64,"Annotation geometry byte cap");bytes+=row.selection.points.size()*64;
    require(row.selection.packed_mask().size()<=max_bytes-bytes,"Annotation mask byte cap");bytes+=row.selection.packed_mask().size();
    for(const auto *object:{&row.attributes,&row.selection.attributes})
      for(const auto &[key,value]:*object) {require(key.size()<=max_bytes-bytes,"Annotation key cap");bytes+=key.size();charge(value,nodes,bytes);}
  }
  for(const auto &row:rows)list.push_back(row_json(row));
  auto result=codec::from_json({{"schema","imagejxx.planar-annotations"},{"version",1},{"extent",extent(shape)},{"rows",std::move(list)}});
  preflight(result);return result;
}
std::vector<PlanarAnnotation> planar_annotations(const DocumentData &data) {
  const auto found=data.metadata.extensions.find(planar_annotations_extension);
  if(found==data.metadata.extensions.end())return {};
  return decode_planar_annotations(data.dataset.shape(),found->second);
}
std::vector<PlanarAnnotation> decode_planar_annotations(DatasetShape shape,const MetadataValue &value) {
  preflight(value);const auto record=codec::to_json(value);
  fields(record,{"schema","version","extent","rows"});
  require(record.at("schema")=="imagejxx.planar-annotations" && index(record.at("version"))==1,"Unsupported planar annotation schema");
  const auto &dimensions=record.at("extent");require(dimensions.is_array() && dimensions.size()==5,"Invalid annotation extent");
  for(const auto &v:dimensions)(void)index(v,true);
  require(dimensions==extent(shape),"Planar annotations require explicit remapping for this extent change");
  const auto &rows=record.at("rows");require(rows.is_array() && rows.size()<=max_rows,"Invalid annotation rows");
  std::vector<PlanarAnnotation> result;result.reserve(rows.size());SessionLimits limits;limits.max_working_bytes=max_bytes;
  for(const auto &row:rows) {
    fields(row,{"name","selection","plane","coordinate","t","c","attributes"});
    require(row.at("name").is_string() && row.at("plane").is_string(),"Invalid annotation text");
    const auto name=row.at("plane").get<std::string>();
    require(name=="XY" || name=="XZ" || name=="YZ","Invalid annotation plane name");
    PlanarAnnotation value{row.at("name").get<std::string>(),detail::selection_codec::decode_selection(row.at("selection"),limits),
      parse_plane(name),index(row.at("coordinate")),optional_index(row.at("t")),optional_index(row.at("c")),codec::object_from_json(row.at("attributes"))};
    value.validate(shape);result.push_back(std::move(value));
  }
  return result;
}
void validate_planar_annotations(const DocumentData &data) {(void)planar_annotations(data);}
DocumentTransaction prepare_store_planar_annotation(const DocumentSnapshot &source,const PlanarAnnotation &value) {
  value.validate(source.data().dataset.shape());auto rows=planar_annotations(source.data());
  require(rows.size()<max_rows,"Planar annotation row cap");rows.push_back(value);
  auto encoded=encode_planar_annotations(source.data().dataset.shape(),rows);
  auto edit=source.begin_edit(Change::Pixels|Change::Metadata);edit.set_extension(planar_annotations_extension,std::move(encoded));return edit;
}
DocumentTransaction prepare_update_planar_annotation(const DocumentSnapshot &source,std::size_t index,const PlanarAnnotation &value) {
  value.validate(source.data().dataset.shape());auto rows=planar_annotations(source.data());
  if(index>=rows.size())throw std::out_of_range("Select a planar annotation");
  auto edit=source.begin_edit(Change::Pixels|Change::Metadata);
  if(!(rows[index]==value)) {rows[index]=value;edit.set_extension(planar_annotations_extension,encode_planar_annotations(source.data().dataset.shape(),rows));}
  return edit;
}
DocumentTransaction prepare_delete_planar_annotation(const DocumentSnapshot &source,std::size_t index) {
  auto rows=planar_annotations(source.data());if(index>=rows.size())throw std::out_of_range("Select a planar annotation");
  rows.erase(rows.begin()+index);auto edit=source.begin_edit(Change::Pixels|Change::Metadata);
  if(rows.empty())edit.remove_extension(planar_annotations_extension);
  else edit.set_extension(planar_annotations_extension,encode_planar_annotations(source.data().dataset.shape(),rows));
  return edit;
}
} // namespace imagejxx
