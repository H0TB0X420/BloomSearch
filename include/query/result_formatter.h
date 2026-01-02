#pragma once
#include <string>
#include <vector>
#include "common/search_types.h"

namespace search {

class ResultFormatter {
public:
    std::string format(SearchResponse& results);
};

} // namespace search
