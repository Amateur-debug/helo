#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace helo::verif {
void write_png(const std::string& path,
    unsigned width,
    unsigned height,
    const std::vector<std::uint8_t>& rgba);
}
