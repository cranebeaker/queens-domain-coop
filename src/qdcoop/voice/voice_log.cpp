// Knights' Domain: nvcoop::Log for the ported voice code.
#include "common/nvcoop_api.h"

#include "qdcoop/core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace nvcoop {

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    QD_INFO("voice", "{}", buf);
}

}  // namespace nvcoop
