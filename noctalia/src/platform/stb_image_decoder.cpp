#include "platform/stb_image_decoder.h"

#include "render/core/image_decoder.h"

#include <climits>
#include <fstream>
#include <iterator>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#include <stb/stb_image.h>

namespace {
  class StbImageDecoder final : public ImageDecoder {
  public:
    std::expected<DecodedRasterImage, std::string> decode(const std::uint8_t* data, std::size_t size) override {
      if (size > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected("image too large");
      }
      int width = 0, height = 0, channels = 0;
      stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
      if (pixels == nullptr) {
        return std::unexpected(stbi_failure_reason() ? stbi_failure_reason() : "cannot decode the image");
      }
      std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
      DecodedRasterImage image;
      image.width = width;
      image.height = height;
      image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
      return image;
    }

    std::expected<LoadedImageFile, std::string> loadFile(const std::string& path, int /*targetSize*/, bool /*crop*/) override {
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        return std::unexpected(path + ": cannot be read");
      }
      const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      auto image = decode(data.data(), data.size());
      if (!image) {
        return std::unexpected(image.error());
      }
      return LoadedImageFile{
          .rgba = std::move(image->pixels),
          .width = image->width,
          .height = image->height,
          .sourceWidth = image->width,
          .sourceHeight = image->height,
      };
    }
  };
} // namespace

void tandem::installStbImageDecoder() { setImageDecoder(std::make_unique<StbImageDecoder>()); }
