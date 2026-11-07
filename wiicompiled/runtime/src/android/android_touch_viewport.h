// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cmath>

namespace BrawlAndroid {
struct TouchViewport {
    float left=0, top=0, width=0, height=0;
    bool operator==(const TouchViewport& other) const {
        return left==other.left && top==other.top && width==other.width && height==other.height;
    }
    bool Map(float& x,float& y) const {
        if (!std::isfinite(x) || !std::isfinite(y) || width<=0 || height<=0 ||
            x<left || y<top || x>left+width || y>top+height) return false;
        x=(x-left)/width; y=(y-top)/height;
        return std::isfinite(x) && std::isfinite(y);
    }
};
}
