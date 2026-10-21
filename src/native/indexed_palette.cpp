#include <imagejxx/indexed_palette.hpp>
#include <cmath>
#include <imagejxx/document.hpp>
#include "axis_command_record.hpp"
namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
void validate(const IndexedPalette &p){
  if(p.color_count<1||p.color_count>256)throw std::invalid_argument("Indexed palette count must be 1..256");
  for(int i=p.color_count;i<256;++i)if(p.palette[i]!=std::array<std::uint8_t,3>{0,0,0})throw std::invalid_argument("Unused indexed palette rows must be black");
}
}
MetadataObject encode_indexed_palette(const IndexedPalette &p){
  validate(p);MetadataArray rows;for(const auto &row:p.palette)rows.push_back(metadata_array({int(row[0]),int(row[1]),int(row[2])}));
  return {{"schema",std::string("imagejxx.indexed-color")},{"version",1},{"policy",std::string("all-byte-indices-padded-black")},{"color_count",p.color_count},{"palette",metadata_array(std::move(rows))}};
}
IndexedPalette indexed_palette_from_record(const MetadataObject &record){
  wire::exact_fields(record,{"schema","version","policy","color_count","palette"});
  if(wire::text_value(record.at("schema"))!="imagejxx.indexed-color"||wire::unsigned_value(record.at("version"))!=1||wire::text_value(record.at("policy"))!="all-byte-indices-padded-black")throw std::invalid_argument("Unsupported indexed palette schema/policy");
  IndexedPalette p;p.color_count=wire::index_value(record.at("color_count"),true);if(p.color_count>256)throw std::invalid_argument("Indexed palette count exceeds 256");
  const auto &rows=wire::array_value(record.at("palette"));if(rows.size()!=256)throw std::invalid_argument("Indexed palette requires 256 rows");
  for(int i=0;i<256;++i){const auto &row=wire::array_value(rows[i]);if(row.size()!=3)throw std::invalid_argument("Indexed palette requires RGB rows");for(int c=0;c<3;++c){const auto value=wire::unsigned_value(row[c]);if(value>255)throw std::invalid_argument("Indexed palette entries require bytes");p.palette[i][c]=std::uint8_t(value);}}
  validate(p);return p;
}
IndexedPaletteClassification classify_indexed_palette(const IndexedPalette &p){
  (void)encode_indexed_palette(p);IndexedPaletteClassification out;
  for(int i=0;i<p.color_count;++i)out.color=out.color||p.palette[i][0]!=p.palette[i][1]||p.palette[i][1]!=p.palette[i][2];
  if(p.color_count==256){
    double sum=0,sum2=0;auto previous=p.palette[0];
    for(const auto &rgb:p.palette){for(int c=0;c<3;++c){const int d=int(rgb[c])-previous[c];sum+=d;sum2+=d*d;}previous=rgb;}
    const auto variance=(768*sum2-sum*sum)/768.;const auto deviation=variance>0?std::sqrt(variance/767.):0.;out.pseudo=deviation<20.||int(deviation)==67;
  }
  out.inverted=true;
  for(int i=1;i<255;++i){const auto &a=p.palette[i-1],&b=p.palette[i];if(int(a[0])+a[1]+a[2]<int(b[0])+b[1]+b[2]){out.inverted=false;break;}}
  return out;
}
std::optional<IndexedPalette> indexed_palette(const DocumentData &data){
  const auto it=data.metadata.extensions.find(indexed_palette_extension);if(it==data.metadata.extensions.end())return {};
  return indexed_palette_from_record(wire::object_value(it->second));
}
void validate_known_indexed_palette(const MetadataObject &extensions){
  const auto it=extensions.find(indexed_palette_extension);if(it==extensions.end())return;
  const auto *tree=std::get_if<std::shared_ptr<const MetadataTree>>(&it->second);if(!tree||!*tree)return;
  const auto *object=std::get_if<MetadataObject>(&(*tree)->value);if(!object)return;
  const auto &r=*object;const auto schema=r.find("schema"),version=r.find("version");
  if(schema==r.end()||version==r.end())return;
  const auto *name=std::get_if<std::string>(&schema->second);if(!name||*name!="imagejxx.indexed-color")return;
  bool known=false;if(const auto p=std::get_if<std::int64_t>(&version->second))known=*p==1;
  if(const auto p=std::get_if<std::uint64_t>(&version->second))known=*p==1;
  if(known)(void)indexed_palette_from_record(r);
}
}
