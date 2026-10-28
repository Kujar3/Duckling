#include "TestFramework.h"

#include <cstring>

// Usage: CoreTests [substring-filter]
int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;

    for (auto& t : dktest::registry())
    {
        if (filter != nullptr && std::strstr (t.name, filter) == nullptr)
            continue;

        ++run;
        const int before = dktest::failureCount();
        std::cout << "[ RUN  ] " << t.name << "\n";

        try
        {
            t.fn();
        }
        catch (const dktest::RequireFailed&)
        {
        }
        catch (const std::exception& e)
        {
            dktest::reportFailure (t.file, 0, std::string ("uncaught exception: ") + e.what());
        }

        if (dktest::failureCount() != before)
            ++failedCases;
    }

    std::cout << "\n" << run << " test cases, " << failedCases << " failed, " << dktest::failureCount() << " failed checks\n";
    return failedCases == 0 ? 0 : 1;
}
