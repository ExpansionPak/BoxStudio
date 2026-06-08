#ifndef LOG_H
#define LOG_H

#include <string>
class Log {
public:
    static void Info(const std::string& message);
    static void Warning(const std::string& message);
    static void Error(const std::string& message);
};

#endif // LOG_H