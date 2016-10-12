//
//  cppsocket
//

#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace cppsocket
{
    class Log
    {
    public:
        enum class Level
        {
            ERROR,
            WARNING,
            INFO,
            VERBOSE
        };

        static Level threshold;

        Log()
        {
        }

        Log(Level pLevel): level(pLevel)
        {
        }

        ~Log()
        {
            if (level >= threshold)
            {
                if (level == Level::ERROR ||
                    level == Level::WARNING)
                {
                    std::cerr << std::endl;
                }
                else
                {
                    std::cout << std::endl;
                }
            }
        }

        template <typename T> Log& operator << (T* val)
        {
            std::stringstream ss;
            ss << val;

            logString(ss.str());

            return *this;
        }

        template <typename T> Log& operator << (T val)
        {
            std::string str = std::to_string(val);
            logString(str);

            return *this;
        }

        Log& operator << (std::string str)
        {
            logString(str);

            return *this;
        }

        Log& operator << (const char* str)
        {
            logString(str);

            return *this;
        }

    private:
        void logString(const std::string& str)
        {
            if (level <= threshold)
            {
                if (level == Level::ERROR ||
                    level == Level::WARNING)
                {
                    std::cerr << str;
                }
                else
                {
                    std::cout << str;
                }
            }
        }

        Level level = Level::INFO;
    };
}
