#pragma once
#include <string>

namespace search {

class AIDetector {
public:
    double calculate_score(const std::string& text);
};

} // namespace search
