//
//  cppsocket
//

#pragma once

#include <string>

namespace cppsocket
{
    class Log
    {
    public:
        enum class Level
        {
            OFF,
            ERR,
            WARN,
            INFO,
            ALL
        };

        static Level threshold;

        Log()
        {
        }

        Log(Level aLevel): level(aLevel)
        {
        }

        Log(const Log& other)
        {
            level = other.level;
            s = other.s;
        }

        Log(Log&& other)
        {
            level = other.level;
            s = std::move(other.s);
        }

        Log& operator=(const Log& other)
        {
            flush();
            level = other.level;
            s = other.s;

            return *this;
        }

        Log& operator=(Log&& other)
        {
            flush();
            level = other.level;
            s = std::move(other.s);

            return *this;
        }

        ~Log()
        {
            flush();
        }

        template<typename T> Log& operator<<(T val)
        {
            s += std::to_string(val);

            return *this;
        }

        Log& operator<<(const std::string& val)
        {
            s += val;

            return *this;
        }

        Log& operator<<(const char* val)
        {
            s += val;

            return *this;
        }

    private:
        void flush();
        
        Level level = Level::INFO;
        std::string s;
    };
}
