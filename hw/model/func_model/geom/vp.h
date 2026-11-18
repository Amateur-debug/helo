#pragma once
#include "common/config.h"
#include "common/types.h"

namespace helo::func {
// 完成透视除法与左上原点的视口变换。
class Vp {
public:
    ScreenVertex transform(const Vertex& vertex, const Viewport& viewport) const;
    void validate(const Viewport& viewport) const;
};
}
