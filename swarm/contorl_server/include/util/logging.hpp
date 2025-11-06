#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

namespace util::log {

enum class Level {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(Level level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    void write(Level level, std::string_view message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (level < level_) {
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &now_time_t);
#else
        localtime_r(&now_time_t, &tm_buf);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%F %T") << '.'
            << std::setfill('0') << std::setw(3) << millis.count();

        std::string level_str;
        switch (level) {
            case Level::Debug: level_str = "DEBUG"; break;
            case Level::Info: level_str = "INFO"; break;
            case Level::Warn: level_str = "WARN"; break;
            case Level::Error: level_str = "ERROR"; break;
        }

        std::cout << '[' << oss.str() << ']'
                  << '[' << level_str << ']'
                  << "[tid:" << std::this_thread::get_id() << "] "
                  << message << std::endl;
    }

private:
    std::mutex mutex_;
    Level level_{Level::Info};
};

inline void set_level(Level level) {
    Logger::instance().set_level(level);
}

inline void write(Level level, std::string_view message) {
    Logger::instance().write(level, message);
}

inline void debug(std::string_view message) {
    write(Level::Debug, message);
}

inline void info(std::string_view message) {
    write(Level::Info, message);
}

inline void warn(std::string_view message) {
    write(Level::Warn, message);
}

inline void error(std::string_view message) {
    write(Level::Error, message);
}

}  // namespace util::log
