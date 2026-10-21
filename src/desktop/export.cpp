#include "desktop.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <zlib.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <commdlg.h>
#endif

namespace imagejxx::desktop {
void writePdf(const std::string &path, const Image &image, double dpi,
              bool landscape) {
  require_finite(dpi, "Print DPI");
  if (dpi < 1 || dpi > 2400)
    throw std::invalid_argument("DPI must be 1..2400");
  auto rgb = displayImage(image);
  std::vector<unsigned char> samples(rgb.pixels.size());
  for (size_t i = 0; i < samples.size(); ++i)
    samples[i] = static_cast<unsigned char>(rgb.pixels[i]);
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
  writeBytes(path, {bytes.begin(), bytes.end()});
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
