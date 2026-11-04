#include <imagejxx/point_annotations.hpp>
#include <limits>
#include <stdexcept>
namespace imagejxx {
namespace {
[[noreturn]] void invalid(const char* message){throw std::invalid_argument(message);}
template<class T> const T& container(const MetadataValue& value) {
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!node || !*node || !std::holds_alternative<T>((*node)->value))invalid("Invalid point annotation container");
  return std::get<T>((*node)->value);
}
std::uint64_t integer(const MetadataValue& value) {
  if(const auto n=std::get_if<std::uint64_t>(&value))return *n;
  if(const auto n=std::get_if<std::int64_t>(&value))if(*n>=0)return std::uint64_t(*n);
  invalid("Point annotation requires nonnegative integers");
}
void layout(const Selection& selection,std::size_t maximum) {
  if((selection.kind!="point" && selection.kind!="multipoint") || !maximum ||
      maximum>1000000 || selection.points.empty() || selection.points.size()>maximum)
    invalid("Point annotations require a nonempty bounded point selection");
}
void annotation(const PointAnnotation& entry) {
  if(entry.counter>=100)invalid("Point counter must be in 0..99");
  if(entry.position && (entry.position->t<0 || entry.position->c<0 || entry.position->z<0))
    invalid("Point position must be nonnegative");
}
}
PointAnnotation point_creation_annotation(DatasetShape shape,PlanePosition position,
    unsigned counter,bool capture_position) {
  shape.validate();PointAnnotation value{counter,position};annotation(value);
  if(position.t>=shape.t || position.c>=shape.c || position.z>=shape.z)
    invalid("Point position outside dataset extent");
  if(!capture_position || (shape.t==1 && shape.c==1 && shape.z==1))value.position.reset();
  return value;
}
std::vector<PointAnnotation> point_annotations(const Selection& selection,std::size_t maximum) {
  const auto found=selection.attributes.find(point_annotations_attribute);
  if(found==selection.attributes.end())return {};
  layout(selection,maximum);const auto& record=container<MetadataObject>(found->second);
  if(record.size()!=2 || !record.count("version") || !record.count("points") || integer(record.at("version"))!=1)
    invalid("Unsupported point annotation record");
  const auto& values=container<MetadataArray>(record.at("points"));
  if(values.size()!=selection.points.size())invalid("Point annotation count differs from vertices");
  std::vector<PointAnnotation> out;out.reserve(values.size());
  for(const auto& value:values) {
    const auto& row=container<MetadataObject>(value);
    if(row.size()!=2 || !row.count("counter") || !row.count("position"))invalid("Invalid point annotation fields");
    PointAnnotation entry;const auto counter=integer(row.at("counter"));
    if(counter>=100)invalid("Point counter must be in 0..99");
    entry.counter=unsigned(counter);
    if(!std::holds_alternative<std::monostate>(row.at("position"))) {
      const auto& position=container<MetadataArray>(row.at("position"));
      if(position.size()!=3)invalid("Point position requires T/C/Z");
      std::array<int,3> axes{};
      for(int i=0;i<3;++i){const auto n=integer(position[i]);if(n>std::uint64_t(std::numeric_limits<int>::max()))invalid("Point position exceeds integer range");axes[i]=int(n);}
      entry.position=PlanePosition{axes[0],axes[1],axes[2]};
    }
    out.push_back(entry);
  }
  return out;
}
Selection with_point_annotations(const Selection& selection,const std::vector<PointAnnotation>& values,std::size_t maximum) {
  layout(selection,maximum);
  if(values.size()!=selection.points.size())invalid("Point annotation count differs from vertices");
  MetadataArray rows;rows.reserve(values.size());
  for(const auto& entry:values) {
    annotation(entry);MetadataValue position;
    if(entry.position)position=metadata_array({entry.position->t,entry.position->c,entry.position->z});
    rows.push_back(metadata_object({{"counter",std::uint64_t(entry.counter)},{"position",std::move(position)}}));
  }
  auto out=selection;out.attributes[point_annotations_attribute]=metadata_object({{"version",std::int64_t(1)},{"points",metadata_array(std::move(rows))}});return out;
}
Selection without_point_annotations(const Selection& selection) {
  auto out=selection;out.attributes.erase(point_annotations_attribute);return out;
}
void validate_point_annotations(const std::vector<PointAnnotation>& values,DatasetShape shape) {
  shape.validate();
  for(const auto& entry:values){annotation(entry);
    if(entry.position && (entry.position->t>=shape.t || entry.position->c>=shape.c || entry.position->z>=shape.z))
      invalid("Point position outside dataset extent");}
}
Selection with_imagej_point_counters(const Selection& selection,const std::vector<std::uint32_t>& words,
    DatasetShape shape,std::size_t maximum) {
  shape.validate();layout(selection,maximum);
  if(words.empty())return without_point_annotations(selection);
  if(words.size()!=selection.points.size())invalid("ImageJ point counter count differs from vertices");
  std::vector<PointAnnotation> entries;entries.reserve(words.size());
  for(const auto word:words) {
    PointAnnotation entry;entry.counter=word&255u;auto position=std::size_t(word>>8);
    if(position) {
      if(position>0x7fffff || position>shape.plane_count())invalid("ImageJ point position outside declared extent or signed range");
      --position;const int c=int(position%std::size_t(shape.c));position/=std::size_t(shape.c);
      entry.position=PlanePosition{int(position/std::size_t(shape.z)),c,int(position%std::size_t(shape.z))};
    }
    annotation(entry);entries.push_back(entry);
  }
  return with_point_annotations(selection,entries,maximum);
}
std::vector<std::uint32_t> imagej_point_counters(const Selection& selection,DatasetShape shape,std::size_t maximum) {
  shape.validate();const auto entries=point_annotations(selection,maximum);validate_point_annotations(entries,shape);
  std::vector<std::uint32_t> out;out.reserve(entries.size());
  for(const auto& entry:entries) {
    std::size_t position=0;
    if(entry.position){const auto p=*entry.position;position=(std::size_t(p.t)*shape.z+p.z)*shape.c+p.c+1;}
    if(position>0x7fffff)invalid("ImageJ point position exceeds signed wire range");
    out.push_back((std::uint32_t(position)<<8)|entry.counter);
  }
  return out;
}
}
