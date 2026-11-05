#pragma once
#include <imagejxx/image.hpp>
#include "../native/atomic_file.hpp"
#include <functional>
namespace imagejxx::desktop {
using ImageByteSink=std::function<void(const unsigned char*,std::size_t)>;
// Internal precondition: one already validated normalized uint8 RGB plane.
void writePreparedDisplayToSink(const std::string&,const Stack&,const ImageByteSink&);
void writePreparedPdfToSink(const std::string&,const Image&,double,bool,const ImageByteSink&);
struct DisplayExportFileOptions { bool pdf=false;double dpi=150;bool landscape=false; };
// Consume owned already normalized rendered pixels, without a second component
// scan. Encoder runs once; destination stays untouched.
std::unique_ptr<imagejxx::detail::StagedAtomicStream> stageDisplayExport(
    const std::string&,Image,const DisplayExportFileOptions&,const ExecutionControl&);
// Final checkpoint precedes owner validation; no callbacks after validation.
void publishDisplayExport(std::unique_ptr<imagejxx::detail::StagedAtomicStream>,
    const std::function<void()>& validate,const ExecutionControl&);
}
