//
//  cppsocket
//

#pragma once

#include <sstream>

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
            s << other.s.rdbuf();
        }

        Log(Log&& other)
        {
            level = other.level;
            s << other.s.rdbuf();
            other.s.clear();
        }

        Log& operator=(const Log& other)
        {
            flush();
            level = other.level;
            s << other.s.rdbuf();

            return *this;
        }

        Log& operator=(Log&& other)
        {
            flush();
            level = other.level;
            s << other.s.rdbuf();
            other.s.clear();

            return *this;
        }

        ~Log()
        {
            flush();
        }

        template <typename T> Log& operator << (T val)
        {
            s << val;

            return *this;
        }

    private:
        void flush();
        
        Level level = Level::INFO;
        std::stringstream s;
    };
}
