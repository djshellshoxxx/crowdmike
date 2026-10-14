#include "TestHarness.h"
#include <cstring>

// Usage: <exe> [name-substring-filter]
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : cmtest::registry()) {
        if (filter != nullptr && c.name.find(filter) == std::string::npos) continue;
        cmtest::currentTest() = c.name.c_str();
        c.fn();
        ++ran;
    }
    if (cmtest::failures() != 0) { std::cerr << cmtest::failures() << " failures in " << ran << " tests\n"; return 1; }
    std::cout << "CrowdMike: " << ran << " tests passed\n";
    return 0;
}
