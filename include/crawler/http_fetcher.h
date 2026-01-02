#pragma once
#include <string>

namespace search {

class HTTPFetcher {
public:
    bool fetch(const std::string& url, std::string& content);
};

} // namespace search
