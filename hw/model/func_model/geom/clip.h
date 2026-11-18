#pragma once
#include "common/types.h"
#include <vector>

namespace helo::func {
// 按六个齐次裁剪平面裁剪三角形并保留属性。
class Clip {
public:
    std::vector<Triangle> clip(const Triangle& triangle) const;
};
}
