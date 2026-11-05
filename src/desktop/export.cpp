#include "desktop.hpp"
#include "display_export_io.hpp"
#include "display_export_record.hpp"
#include "render_memory.hpp"
#include "../native/kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <variant>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <zlib.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <commdlg.h>
#endif

namespace imagejxx::desktop {
namespace {
constexpr std::size_t export_record_string_limit=131064;
constexpr std::size_t export_record_total_strings=524288;
void exportRecordRequire(bool condition,const char* message) {
  if(!condition)throw std::invalid_argument(message);
}
void exportRecordFields(const Json& value,std::initializer_list<const char*> names) {
  exportRecordRequire(value.is_object() && value.size()==names.size(),"Invalid display export record fields");
  for(const auto* name:names)exportRecordRequire(value.contains(name),"Missing display export record field");
}
void exportRecordScan(const Json& value,unsigned depth,std::size_t& nodes,std::size_t& strings) {
  exportRecordRequire(depth<=8 && ++nodes<=4096,"Display export record exceeds structural limit");
  const auto charge=[&](const std::string& text) {
    exportRecordRequire(text.size()<=export_record_string_limit && text.find('\0')==std::string::npos,"Display export record string exceeds limit");
    exportRecordRequire(text.size()<=export_record_total_strings-strings,"Display export record exceeds string budget");
    strings+=text.size();
  };
  if(value.is_string())charge(value.get_ref<const std::string&>());
  else if(value.is_array())for(const auto& child:value)exportRecordScan(child,depth+1,nodes,strings);
  else if(value.is_object())for(const auto& child:value.items()){charge(child.key());exportRecordScan(child.value(),depth+1,nodes,strings);}
  else exportRecordRequire(value.is_number() || value.is_boolean(),"Invalid display export record value");
}
std::uint64_t exportRecordUnsigned(const Json& value) {
  exportRecordRequire(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>()>=0),"Display export record needs a nonnegative integer");
  return value.get<std::uint64_t>();
}
double exportRecordNumber(const Json& value) {
  exportRecordRequire(value.is_number(),"Display export record needs a finite number");
  const auto number=value.get<double>();exportRecordRequire(std::isfinite(number),"Display export record needs a finite number");return number;
}
bool exportRecordBool(const Json& value) {
  exportRecordRequire(value.is_boolean(),"Display export record needs a boolean");return value.get<bool>();
}
const std::string& exportRecordString(const Json& value) {
  exportRecordRequire(value.is_string(),"Display export record needs a string");return value.get_ref<const std::string&>();
}
void exportRecordProperties(const Json& values,PixelType type,int samples) {
  exportRecordRequire(values.is_object() && values.size()<=3,"Invalid recorded display properties");
  for(const auto& item:values.items()) {
    if(item.key()=="lut") {exportRecordString(item.value());continue;}
    if(item.key()=="display_range") {
      const auto& range=item.value();exportRecordRequire(range.is_array() && range.size()==2,"Invalid recorded display range");
      const auto low=exportRecordNumber(range[0]),high=exportRecordNumber(range[1]);
      exportRecordRequire(low<high || (low==high && samples==1 && (type==PixelType::UInt16 || type==PixelType::Float32)),"Invalid recorded display range");continue;
    }
    exportRecordRequire(item.key()=="custom_lut" && item.value().is_array() && item.value().size()==256,"Invalid recorded custom LUT");
    for(const auto& row:item.value()) {
      exportRecordRequire(row.is_array() && row.size()==3,"Invalid recorded custom LUT row");
      for(const auto& entry:row){const auto number=exportRecordNumber(entry);exportRecordRequire(number>=0 && number<=255,"Invalid recorded custom LUT color");}
    }
  }
}
}
bool isDisplayExportRecord(const Json& value) {return value.is_object() && get(value,"schema")=="imagejxx.display-export-command";}
std::size_t displayExportRecordWorkingBytes(const Json& value) {
  std::size_t nodes=0,strings=0;exportRecordScan(value,0,nodes,strings);
  // Fixed grammar bounds one 256x3 LUT's JSON/native nodes and control fields;
  // variable strings include decoding, worker controls and validation copies.
  return RenderMemory::add(131072,RenderMemory::mul(strings,8));
}
std::size_t displayExportRecordWorkingBytes(const DocumentSnapshot& source,const std::string& path,const DisplayExportFontPolicy& font) {
  std::size_t strings=1024;
  const auto charge=[&](const std::string& value) {
    exportRecordRequire(value.size()<=export_record_string_limit && value.find('\0')==std::string::npos,"Display export record string exceeds limit");
    exportRecordRequire(value.size()<=export_record_total_strings-strings,"Display export record exceeds string budget");strings+=value.size();
  };
  charge(path);charge(font.default_path);
  const auto& directory=font.directory.native();
#ifdef _WIN32
  const auto directory_bytes=RenderMemory::mul(directory.size(),3); // UTF16 -> UTF8 upper bound.
#else
  const auto directory_bytes=directory.size();
#endif
  exportRecordRequire(directory_bytes<=export_record_string_limit && directory.find(std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos,"Display export record directory exceeds limit");
  exportRecordRequire(directory_bytes<=export_record_total_strings-strings,"Display export record exceeds string budget");
  strings+=directory_bytes;
  const auto lut=source.display().properties.find("lut");
  if(lut!=source.display().properties.end())if(const auto* value=std::get_if<std::string>(&lut->second))charge(*value);
  return RenderMemory::add(131072,RenderMemory::mul(strings,8));
}
DisplayExportRecord decodeDisplayExportRecord(const Json& raw) {
  displayExportRecordWorkingBytes(raw);
  exportRecordFields(raw,{"schema","version","action","source","display","render","file","font"});
  exportRecordRequire(isDisplayExportRecord(raw) && exportRecordUnsigned(raw.at("version"))==1,"Unsupported display export record version");
  const auto& action=exportRecordString(raw.at("action"));
  exportRecordRequire(action=="flatten" || action=="export_display" || action=="export_pdf","Unsupported recorded display export action");
  const auto& source=raw.at("source");exportRecordFields(source,{"document_id","revisions","shape","pixel_type"});
  const auto id=exportRecordUnsigned(source.at("document_id"));exportRecordRequire(id>0,"Recorded source identity is missing");
  const auto& revisions=source.at("revisions");exportRecordRequire(revisions.is_array() && revisions.size()==3,"Invalid recorded source revisions");
  std::array<std::uint64_t,3> revision{};for(std::size_t i=0;i<3;++i)revision[i]=exportRecordUnsigned(revisions[i]);
  const auto& shape=source.at("shape");exportRecordRequire(shape.is_array() && shape.size()==6,"Invalid recorded source shape");
  std::array<int,6> dimensions{};for(std::size_t i=0;i<6;++i){const auto size=exportRecordUnsigned(shape[i]);exportRecordRequire(size>0 && size<=std::uint64_t(std::numeric_limits<int>::max()),"Invalid recorded source extent");dimensions[i]=int(size);}
  exportRecordRequire(dimensions[0]==1 && dimensions[1]==1 && (dimensions[5]==1 || dimensions[5]==3 || dimensions[5]==4),"Recorded export needs one legacy T/C source");
  const auto pixel=exportRecordUnsigned(source.at("pixel_type"));exportRecordRequire(pixel<=3,"Invalid recorded pixel type");
  const auto& display=raw.at("display");exportRecordFields(display,{"position","properties"});
  const auto& position=display.at("position");exportRecordRequire(position.is_array() && position.size()==3,"Invalid recorded export position");
  std::array<int,3> plane{};for(std::size_t i=0;i<3;++i){const auto n=exportRecordUnsigned(position[i]);exportRecordRequire(n<std::uint64_t(dimensions[i]),"Recorded export position is outside source");plane[i]=int(n);}
  exportRecordProperties(display.at("properties"),static_cast<PixelType>(pixel),dimensions[5]);
  const auto& render=raw.at("render");exportRecordFields(render,{"point_show_all","show_rois"});
  const auto all=exportRecordBool(render.at("point_show_all")),rois=exportRecordBool(render.at("show_rois"));
  const auto& file=raw.at("file");exportRecordFields(file,{"path","dpi","landscape","publication"});
  const auto& path=exportRecordString(file.at("path"));const auto dpi=exportRecordNumber(file.at("dpi"));const auto landscape=exportRecordBool(file.at("landscape"));
  exportRecordRequire((action=="flatten" ? path.empty():!path.empty()) && dpi>=1 && dpi<=2400 && get(file,"publication")=="replace","Invalid recorded export destination/options");
  const auto& font=raw.at("font");exportRecordFields(font,{"policy","default_path","directory"});
  exportRecordRequire(get(font,"policy")=="reacquire" && !exportRecordString(font.at("directory")).empty(),"Unsupported recorded font acquisition policy");
  exportRecordString(font.at("default_path"));
  DisplayExportRecord result;result.action=action;result.path=path;result.source_id=id;
  result.source_revisions={revision[0],revision[1],revision[2],0};
  result.shape={dimensions[0],dimensions[1],dimensions[2],dimensions[3],dimensions[4],dimensions[5]};result.pixel_type=static_cast<PixelType>(pixel);
  result.display.t=plane[0];result.display.c=plane[1];result.display.crosshair={0,0,plane[2]};
  result.display.properties=metadataObjectFromJson(display.at("properties"));
  result.point_show_all=all;result.show_rois=rois;result.file={action=="export_pdf",dpi,landscape};
  result.font={font.at("default_path").get<std::string>(),std::filesystem::u8path(font.at("directory").get<std::string>())};return result;
}
Json captureDisplayExportRecord(const DocumentSnapshot& source,const std::string& action,const std::string& path,bool all,bool rois,const DisplayExportFileOptions& file,const DisplayExportFontPolicy& font) {
  displayExportRecordWorkingBytes(source,path,font);
  Json properties=Json::object();for(const auto* key:{"display_range","lut","custom_lut"}) {
    const auto found=source.display().properties.find(key);if(found!=source.display().properties.end() && !std::holds_alternative<std::monostate>(found->second))properties[key]=metadataJson(found->second);
  }
  const auto shape=source.data().dataset.shape();const auto revision=source.revisions();
  Json result={{"schema","imagejxx.display-export-command"},{"version",1},{"action",action},
    {"source",{{"document_id",source.id()},{"revisions",{revision.pixels,revision.metadata,revision.selection}},
      {"shape",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"pixel_type",int(source.data().dataset.pixel_type())}}},
    {"display",{{"position",{source.display().t,source.display().c,source.display().crosshair[2]}},{"properties",std::move(properties)}}},
    {"render",{{"point_show_all",all},{"show_rois",rois}}},
    {"file",{{"path",path},{"dpi",file.dpi},{"landscape",file.landscape},{"publication","replace"}}},
    {"font",{{"policy","reacquire"},{"default_path",font.default_path},{"directory",font.directory.u8string()}}}};
  // Validate the emitted wire grammar without retaining another decoded copy.
  displayExportRecordWorkingBytes(result);return result;
}
void validateDisplayExportReplaySource(const DisplayExportRecord& record,const DocumentSnapshot& source,bool rebind) {
  const auto shape=source.data().dataset.shape();const auto expected=record.shape;
  exportRecordRequire(shape.t==expected.t && shape.c==expected.c && shape.z==expected.z && shape.y==expected.y && shape.x==expected.x && shape.s==expected.s && source.data().dataset.pixel_type()==record.pixel_type,"Recorded export source shape/type differs; choose a compatible source");
  if(!rebind && (record.source_id!=source.id() || !source.revisions().matches(record.source_revisions,Change::Data)))
    throw std::invalid_argument("Recorded export source changed or is unavailable; explicitly rebind to the current source");
}

void validateDisplayExportFormat(const std::string& path,int width,int height,const DisplayExportFileOptions& options) {
  if(width<=0 || height<=0)throw std::invalid_argument("Display export needs positive dimensions");
  const auto suffix=extension(path);
  if(!options.pdf && (suffix==".jpg" || suffix==".jpeg" || suffix==".tga") && (width>65535 || height>65535))
    throw std::invalid_argument("JPEG/TGA dimensions must be at most65535");
  if(!options.pdf && suffix==".bmp") {
    const auto row=(std::uint64_t(width)*3+3)&~std::uint64_t(3);
    if(row*std::uint64_t(height)+54>std::uint64_t(std::numeric_limits<int>::max()))
      throw std::invalid_argument("BMP exceeds encoder byte range");
  }
  if(options.pdf) {
    require_finite(options.dpi,"Print DPI");
    if(options.dpi<1 || options.dpi>2400)throw std::invalid_argument("DPI must be1..2400");
    if(std::uint64_t(width)*std::uint64_t(height)>std::numeric_limits<uLong>::max()/3)
      throw std::length_error("PDF RGB samples exceed compression byte range");
  }
}
void writePdfImpl(const std::string &path,const Image &image,double dpi,bool landscape,
    const ImageByteSink* sink,bool normalized) {
  require_finite(dpi, "Print DPI");
  if (dpi < 1 || dpi > 2400)
    throw std::invalid_argument("DPI must be 1..2400");
  std::optional<Image> mapped;if(!normalized)mapped=displayImage(image);
  const auto& rgb=mapped?*mapped:image;
  std::vector<unsigned char> samples(rgb.pixels.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    kernel_checkpoint_periodic();samples[i] = static_cast<unsigned char>(rgb.pixels[i]);
  }
  uLongf size = compressBound(static_cast<uLong>(samples.size()));
  std::string compressed(size, '\0');
  if (compress2(reinterpret_cast<Bytef *>(compressed.data()), &size,
                samples.data(), static_cast<uLong>(samples.size()),
                Z_DEFAULT_COMPRESSION) != Z_OK)
    throw std::runtime_error("PDF image compression failed");
  compressed.resize(size);
  double pageW = landscape ? 792 : 612, pageH = landscape ? 612 : 792,
         scale = std::min(
             {72 / dpi, (pageW - 72) / rgb.width, (pageH - 72) / rgb.height});
  double w = rgb.width * scale, h = rgb.height * scale;
  std::ostringstream content;
  content << std::fixed << std::setprecision(6) << "q\n"
          << w << " 0 0 " << h << ' ' << (pageW - w) / 2 << ' '
          << (pageH - h) / 2 << " cm\n/Im0 Do\nQ\n";
  auto drawing = content.str();
  std::ostringstream pdf;
  pdf << "%PDF-1.4\n%imagejxx\n";
  std::vector<std::streamoff> offsets{0};
  auto object = [&](int id, const std::string &body) {
    offsets.push_back(pdf.tellp());
    pdf << id << " 0 obj\n" << body << "\nendobj\n";
  };
  object(1, "<< /Type /Catalog /Pages 2 0 R >>");
  object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  object(3,
         "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
             std::to_string(pageW) + " " + std::to_string(pageH) +
             "] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>");
  object(4, "<< /Type /XObject /Subtype /Image /Width " +
                std::to_string(rgb.width) + " /Height " +
                std::to_string(rgb.height) +
                " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter "
                "/FlateDecode /Length " +
                std::to_string(compressed.size()) + " >>\nstream\n" +
                compressed + "\nendstream");
  object(5, "<< /Length " + std::to_string(drawing.size()) + " >>\nstream\n" +
                drawing + "endstream");
  auto xref = pdf.tellp();
  pdf << "xref\n0 6\n0000000000 65535 f \n";
  for (size_t i = 1; i < offsets.size(); ++i)
    pdf << std::setfill('0') << std::setw(10) << offsets[i] << " 00000 n \n";
  pdf << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
      << xref << "\n%%EOF\n";
  auto bytes = pdf.str();
  if(sink)(*sink)(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());
  else writeBytes(path,{bytes.begin(),bytes.end()});
}
void writePdf(const std::string& path,const Image& image,double dpi,bool landscape) {
  writePdfImpl(path,image,dpi,landscape,nullptr,false);
}
void writePreparedPdfToSink(const std::string& path,const Image& image,double dpi,bool landscape,const ImageByteSink& sink) {
  if(!sink)throw std::invalid_argument("PDF encoder needs a byte sink");
  writePdfImpl(path,image,dpi,landscape,&sink,true);
}
std::unique_ptr<imagejxx::detail::StagedAtomicStream> stageDisplayExport(
    const std::string& path,Image image,const DisplayExportFileOptions& options,const ExecutionControl& control) {
  ExecutionScope scope(control);kernel_checkpoint();
  validateDisplayExportFormat(path,image.width,image.height,options);
  if(image.width<=0 || image.height<=0 || image.type!=PixelType::UInt8 || image.channels!=3 ||
      image.pixels.size()!=std::size_t(image.width)*image.height*3)
    throw std::invalid_argument("Display export needs complete normalized uint8 RGB");
  auto staged=std::make_unique<imagejxx::detail::StagedAtomicStream>(std::filesystem::u8path(path));
  staged->open(control);
  std::size_t encoded=0;
  const ImageByteSink sink=[&](const unsigned char* data,std::size_t count) {
    if(count>std::numeric_limits<std::size_t>::max()-encoded)throw std::length_error("Encoded display bytes overflow");
    staged->append(data,count,control);encoded+=count;
    imagejxx::detail::file_stage_progress(control,"Display export staged bytes",encoded,encoded);
  };
  if(options.pdf)writePreparedPdfToSink(path,image,options.dpi,options.landscape,sink);
  else {Stack frames;frames.push_back(std::move(image));writePreparedDisplayToSink(path,frames,sink);}
  staged->close(control);return staged;
}
void publishDisplayExport(std::unique_ptr<imagejxx::detail::StagedAtomicStream> staged,
    const std::function<void()>& validate,const ExecutionControl& control) {
  if(!staged || !validate)throw std::invalid_argument("Display publication needs a stage and owner guard");
  imagejxx::detail::file_checkpoint(control);
  validate();
  if(control.cancellation && control.cancellation->canceled())throw OperationCancelled();
  staged->publish(true);
}
#ifdef _WIN32
namespace {
std::vector<unsigned char> dib(const Image &image) {
  auto rgb = displayImage(image);
  size_t stride = (size_t(rgb.width) * 3 + 3) & ~size_t(3);
  std::vector<unsigned char> data(
      sizeof(BITMAPINFOHEADER) + stride * rgb.height, 0);
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = rgb.width;
  info.biHeight = rgb.height;
  info.biPlanes = 1;
  info.biBitCount = 24;
  info.biCompression = BI_RGB;
  info.biSizeImage = DWORD(stride * rgb.height);
  std::memcpy(data.data(), &info, sizeof(info));
  for (int y = 0; y < rgb.height; ++y)
    for (int x = 0; x < rgb.width; ++x)
      for (int c = 0; c < 3; ++c)
        data[sizeof(info) + (rgb.height - 1 - y) * stride + x * 3 + c] =
            static_cast<unsigned char>(rgb.at(x, y, 2 - c));
  return data;
}
struct ClipboardLock {
  bool open = OpenClipboard(nullptr) != 0;
  ~ClipboardLock() {
    if (open)
      CloseClipboard();
  }
};
} // namespace
void copySystemImage(const Image &image) {
  auto data = dib(image);
  ClipboardLock clipboard;
  if (!clipboard.open)
    return;
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, data.size());
  if (!memory)
    return;
  void *destination = GlobalLock(memory);
  if (!destination) {
    GlobalFree(memory);
    return;
  }
  std::memcpy(destination, data.data(), data.size());
  GlobalUnlock(memory);
  if (!EmptyClipboard() || !SetClipboardData(CF_DIB, memory))
    GlobalFree(memory);
}
std::optional<Image> pasteSystemImage() {
  ClipboardLock clipboard;
  if (!clipboard.open)
    return {};
  auto memory = GetClipboardData(CF_DIB);
  if (!memory)
    return {};
  size_t size = GlobalSize(memory);
  auto source = static_cast<const unsigned char *>(GlobalLock(memory));
  if (!source)
    return {};
  struct Unlock {
    HANDLE memory;
    ~Unlock() { GlobalUnlock(memory); }
  } unlock{memory};
  if (size < sizeof(BITMAPINFOHEADER))
    return {};
  BITMAPINFOHEADER info;
  std::memcpy(&info, source, sizeof(info));
  if (info.biSize < 40 || info.biSize > size || info.biWidth <= 0 ||
      info.biHeight == 0 || info.biHeight == LONG_MIN || info.biPlanes != 1 ||
      info.biCompression != BI_RGB ||
      (info.biBitCount != 8 && info.biBitCount != 24 && info.biBitCount != 32))
    return {};
  int w = info.biWidth, h = std::abs(info.biHeight);
  if (double(w) * h * 3 > 1024ULL * 1024 * 1024 / 8)
    return {};
  size_t stride = (size_t(w) * info.biBitCount / 8 + 3) & ~size_t(3),
         colors =
             info.biBitCount == 8 ? (info.biClrUsed ? info.biClrUsed : 256) : 0;
  if (colors > 256)
    return {};
  size_t start = info.biSize + colors * 4;
  if (start > size || stride > (size - start) / h)
    return {};
  Image image(w, h, 3, PixelType::UInt8);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t offset = start + (info.biHeight > 0 ? h - 1 - y : y) * stride +
                      x * (info.biBitCount / 8);
      const auto *p = source + offset;
      if (info.biBitCount == 8) {
        if (*p >= colors)
          return {};
        p = source + info.biSize + *p * 4;
      }
      for (int c = 0; c < 3; ++c)
        image.at(x, y, c) = p[2 - c];
    }
  return image;
}
bool printImage(const Image &image) {
  auto data = dib(image);
  PRINTDLGW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
  bool selected = PrintDlgW(&dialog) != 0;
  struct Release {
    PRINTDLGW &dialog;
    ~Release() {
      if (dialog.hDC)
        DeleteDC(dialog.hDC);
      if (dialog.hDevMode)
        GlobalFree(dialog.hDevMode);
      if (dialog.hDevNames)
        GlobalFree(dialog.hDevNames);
    }
  } release{dialog};
  if (!selected)
    return false;
  DOCINFOW document{};
  document.cbSize = sizeof(document);
  document.lpszDocName = L"imagejxx";
  if (StartDocW(dialog.hDC, &document) <= 0)
    throw std::runtime_error("Printer could not start document");
  try {
    if (StartPage(dialog.hDC) <= 0)
      throw std::runtime_error("Printer could not start page");
    int width = GetDeviceCaps(dialog.hDC, HORZRES),
        height = GetDeviceCaps(dialog.hDC, VERTRES);
    double scale =
        std::min(double(width) / image.width, double(height) / image.height);
    int w = int(image.width * scale), h = int(image.height * scale);
    if (StretchDIBits(dialog.hDC, (width - w) / 2, (height - h) / 2, w, h, 0, 0,
                      image.width, image.height,
                      data.data() + sizeof(BITMAPINFOHEADER),
                      reinterpret_cast<const BITMAPINFO *>(data.data()),
                      DIB_RGB_COLORS, SRCCOPY) == GDI_ERROR)
      throw std::runtime_error("Printer raster transfer failed");
    if (EndPage(dialog.hDC) <= 0 || EndDoc(dialog.hDC) <= 0)
      throw std::runtime_error("Printer output failed");
    return true;
  } catch (...) {
    AbortDoc(dialog.hDC);
    throw;
  }
}
#else
void copySystemImage(const Image &) {}
std::optional<Image> pasteSystemImage() { return {}; }
bool printImage(const Image &) {
  throw std::runtime_error("Use PDF export on this platform");
}
#endif
} // namespace imagejxx::desktop
