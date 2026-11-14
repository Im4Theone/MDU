#include "windows/image_io.h"

#include "utilities/strings.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace mdu {

namespace {

constexpr UINT maxDimension = 16384;

}  // namespace

std::optional<wallpaper::Image> loadImage(IWICImagingFactory* factory, const std::filesystem::path& path, std::string& error)
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        error = "cannot decode " + toUtf8(path.wstring());
        return std::nullopt;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > maxDimension ||
        height > maxDimension) {
        error = "unsupported image size";
        return std::nullopt;
    }

    wallpaper::Image image{static_cast<int>(width), static_cast<int>(height),
                std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) {
        error = "cannot read image pixels";
        return std::nullopt;
    }
    return image;
}

bool savePng(IWICImagingFactory* factory, const wallpaper::Image& image, const std::filesystem::path& path, std::string& error)
{
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    const auto width = static_cast<UINT>(image.width);
    const auto height = static_cast<UINT>(image.height);

    bool ok = SUCCEEDED(factory->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
              format == GUID_WICPixelFormat32bppBGRA;
    ok = ok &&
         SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(image.pixels.size()),
                                      const_cast<BYTE*>(image.pixels.data()))) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    if (!ok) {
        error = "cannot write PNG " + toUtf8(path.wstring());
    }
    return ok;
}

}  // namespace mdu
