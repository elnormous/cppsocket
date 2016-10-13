//
//  cppsocket
//

#include <iostream>
#include <string>
#ifdef _MSC_VER
#include <windows.h>
#include <strsafe.h>
#else
    #if defined(LOG_SYSLOG)
    #include <sys/syslog.h>
    #endif
#endif

#include "Log.h"

namespace cppsocket
{
#ifndef DEBUG
    Log::Level Log::threshold = Log::Level::INFO;
#else
    Log::Level Log::threshold = Log::Level::ALL;
#endif

    Log::~Log()
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
        StringCchCatW(szBuffer, sizeof(szBuffer), L"\n");
        OutputDebugStringW(szBuffer);
#else
#if defined(LOG_SYSLOG)
        int priority = 0;
        switch (level)
        {
            case ERR: priority = LOG_ERR; break;
            case WARN: priority = LOG_WARNING; break;
            case INFO: priority = LOG_INFO; break;
            case DEBUG: priority = LOG_DEBUG; break;
            default: break;
        }
        syslog(prio, "%s", s.str().c_str());
#endif
#endif
    }
}
