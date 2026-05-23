#include <iostream>

#if defined(_WIN32)
#include "client/windows/handler/exception_handler.h"
#elif defined(__APPLE__)
#include "client/mac/handler/exception_handler.h"
#elif defined(__linux__)
#include "client/linux/handler/exception_handler.h"
#endif

int main() {
    std::cout << "Starting Breakpad link verification..." << std::endl;

#if defined(_WIN32)
    google_breakpad::ExceptionHandler* handler = nullptr;
    (void)handler;
#elif defined(__linux__)
    google_breakpad::MinidumpDescriptor descriptor("/tmp");
    (void)descriptor;
#elif defined(__APPLE__)
    google_breakpad::ExceptionHandler* handler = nullptr;
    (void)handler;
#endif

    std::cout << "Linkage successful." << std::endl;
    return 0;
}
