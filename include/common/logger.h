#pragma once

#include <string>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace search {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);

private:
    static void log(LogLevel level, const std::string& msg);
    static std::string current_timestamp();
    static std::string level_to_string(LogLevel level);
};

} // namespace search
