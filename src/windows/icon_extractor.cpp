#include "windows/icon_extractor.h"

#include "utilities/base64.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

namespace mdu {

namespace {

// Owns an HBITMAP returned by the shell.
class BitmapHandle {
public:
    BitmapHandle() = default;
    ~BitmapHandle()
    {
        if (handle_) {
            DeleteObject(handle_);
        }
    }
    BitmapHandle(const BitmapHandle&) = delete;
    BitmapHandle& operator=(const BitmapHandle&) = delete;

    HBITMAP* put() { return &handle_; }
    HBITMAP get() const { return handle_; }

private:
    HBITMAP handle_ = nullptr;
};

std::vector<std::uint8_t> encodePng(IWICImagingFactory* factory, IWICBitmapSource* source)
{
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    if (!stream) {
        return {};
    }

    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    UINT width = 0;
    UINT height = 0;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(source->GetSize(&width, &height)) || FAILED(frame->SetSize(width, height)) ||
        FAILED(frame->SetPixelFormat(&format)) || FAILED(frame->WriteSource(source, nullptr)) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return {};
    }

    ULARGE_INTEGER size{};
    if (FAILED(IStream_Size(stream.Get(), &size)) || size.QuadPart > 4 * 1024 * 1024 ||
        FAILED(IStream_Reset(stream.Get()))) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    if (FAILED(IStream_Read(stream.Get(), bytes.data(), static_cast<ULONG>(bytes.size())))) {
        return {};
    }
    return bytes;
}

}  // namespace

std::string extractIconDataUrl(IWICImagingFactory* factory, const std::wstring& target, int sizePixels)
{
    if (!factory || target.empty()) {
        return {};
    }

    ComPtr<IShellItemImageFactory> imageFactory;
    if (FAILED(SHCreateItemFromParsingName(target.c_str(), nullptr, IID_PPV_ARGS(&imageFactory)))) {
        return {};
    }

    BitmapHandle bitmap;
    if (FAILED(imageFactory->GetImage(SIZE{sizePixels, sizePixels}, SIIGBF_ICONONLY, bitmap.put()))) {
        return {};
    }

    // Shell icons are 32-bit premultiplied; convert to straight alpha for PNG.
    ComPtr<IWICBitmap> wicBitmap;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapFromHBITMAP(bitmap.get(), nullptr, WICBitmapUsePremultipliedAlpha, &wicBitmap)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(wicBitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                     0.0, WICBitmapPaletteTypeCustom))) {
        return {};
    }

    const auto png = encodePng(factory, converter.Get());
    if (png.empty()) {
        return {};
    }
    return "data:image/png;base64," + base64Encode(png);
}

}  // namespace mdu
