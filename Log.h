//
//  cppsocket
//

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#ifdef _MSC_VER
#include <windows.h>
#include <strsafe.h>
#else
    #if defined(LOG_SYSLOG)
    #include <syslog.h>
    #endif
#endif

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

        Log(Level pLevel): level(pLevel)
        {
        }

        ~Log()
        {
            if (level <= threshold)
            {
                if (level == Level::ERR ||
                    level == Level::WARN)
                {
                    std::cerr << s.str() << std::endl;
                }
                else
                {
                    std::cout << s.str() << std::endl;
                }
            }

#ifdef _MSC_VER
            wchar_t szBuffer[MAX_PATH];
            MultiByteToWideChar(CP_UTF8, 0, s.str().c_str(), -1, szBuffer, MAX_PATH);
            StringCchCat(szBuffer, sizeof(szBuffer), L"\n");
            OutputDebugString(szBuffer);
#else
    #if defined(LOG_SYSLOG)
            int priority = 0;
            switch (level)
            {
                case ERR: priority = LOG_ERR; break;
                case WARN: priority = LOG_WARNING; break;
                case INFO: priority = LOG_INFO; break;
                case DEBUG: priority = LOG_DEBUG; break;
            }
            syslog(prio, "%s", s.str().c_str());
    #endif
#endif
        }

        template <typename T> Log& operator << (T val)
        {
            s << val;

            return *this;
        }

    private:
        Level level = Level::INFO;
        std::stringstream s;
    };
}
