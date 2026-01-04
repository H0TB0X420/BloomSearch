#pragma once
#include <string>
#include <vector>
#include "common/search_types.h"

namespace search {

class ResultFormatter {
public:
    std::string format(SearchResponse& results, int start_rank = 1);
};

} // namespace search
