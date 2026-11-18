#include "dcache_test.h"
#include "icache_test.h"
#include "mem_test.h"
#ifdef HELO_MEMORY_RTL_COMPARE
#include "memory_compare_test.h"
#endif
#include <iostream>
#include <stdexcept>
#include <string>

// 选择存储模块共用测试并向CTest返回真实结果。
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected memory module name");
        }
        const std::string module = argv[1];
        if (module == "mem") {
            helo::verif::mem_test();
        } else if (module == "icache") {
            helo::verif::icache_test();
        } else if (module == "dcache") {
            helo::verif::dcache_test();
#ifdef HELO_MEMORY_RTL_COMPARE
        } else if (module == "compare") {
            helo::verif::memory_compare_test();
#endif
        } else {
            throw std::runtime_error("unknown memory module");
        }
        std::cout << module << ": PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
