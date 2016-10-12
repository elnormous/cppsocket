//
//  cppsocket
//

#include "Log.h"

namespace cppsocket
{
#ifndef DEBUG
    Log::Level Log::threshold = Log::Level::INFO;
#else
    Log::Level Log::threshold = Log::Level::ALL;
#endif
}
