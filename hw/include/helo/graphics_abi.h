#pragma once

namespace helo::graphics_abi {
// 为图形调用寄存器提供共享名称，编号语义由图形ABI文档规定。
constexpr unsigned VS_POSITION = 1;
constexpr unsigned VS_COLOR = 5;
constexpr unsigned VS_UV = 9;
constexpr unsigned VS_VERTEX_ID = 11;
constexpr unsigned VS_REGISTER_COUNT = 12;
constexpr unsigned FS_COLOR = 1;
constexpr unsigned FS_TEXTURE = 5;
constexpr unsigned FS_UV = 9;
constexpr unsigned FS_PIXEL_X = 11;
constexpr unsigned FS_PIXEL_Y = 12;
constexpr unsigned FS_DEPTH = 13;
constexpr unsigned FS_DISCARD = 14;
constexpr unsigned FS_FRAGMENT_ID = 15;
constexpr unsigned FS_REGISTER_COUNT = 16;
}
