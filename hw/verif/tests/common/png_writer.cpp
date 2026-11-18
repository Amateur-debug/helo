#include "png_writer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace helo::verif {
namespace {
// 按PNG要求追加大端32位整数。
void big_endian(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

// 使用标准IEEE多项式独立计算PNG块校验和。
std::uint32_t crc32(const std::uint8_t* data, std::size_t count) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

// 追加带长度、类型及校验和的完整PNG块。
void chunk(
    std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    big_endian(png, static_cast<std::uint32_t>(data.size()));
    const auto first = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    big_endian(png, crc32(png.data() + first, data.size() + 4));
}
}

// 将实际帧缓冲编码为无需外部压缩库的RGBA8 PNG文件。
void write_png(const std::string& path,
    unsigned width,
    unsigned height,
    const std::vector<std::uint8_t>& rgba) {
    if (!width || !height || rgba.size() != std::size_t(width) * height * 4) {
        throw std::invalid_argument("invalid PNG dimensions");
    }
    std::vector<std::uint8_t> raw;
    raw.reserve(rgba.size() + height);
    for (unsigned y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(),
            rgba.begin() + std::size_t(y) * width * 4,
            rgba.begin() + std::size_t(y + 1) * width * 4);
    }
    std::vector<std::uint8_t> compressed{0x78, 0x01};
    for (std::size_t first = 0; first < raw.size();) {
        const auto count = static_cast<unsigned>(std::min<std::size_t>(65535, raw.size() - first));
        compressed.push_back(first + count == raw.size() ? 1 : 0);
        compressed.push_back(count & 255);
        compressed.push_back(count >> 8);
        compressed.push_back((~count) & 255);
        compressed.push_back(((~count) >> 8) & 255);
        compressed.insert(compressed.end(), raw.begin() + first, raw.begin() + first + count);
        first += count;
    }
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    big_endian(compressed, (b << 16) | a);
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    big_endian(header, width);
    big_endian(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", compressed);
    chunk(png, "IEND", {});
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!output) {
        throw std::runtime_error("failed writing PNG");
    }
}
}
