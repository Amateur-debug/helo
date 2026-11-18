#include "tests/geom/geom_test.h"
#include "tests/rast/rast_test.h"
#include "tests/rop/rop_test.h"
#include "tests/tex/tex_test.h"
#include <iostream>
#include <stdexcept>
#include <string>

// 根据统一入口选择模块测试并向 CTest 返回真实结果。
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected graphics module name");
        }
        const std::string name = argv[1];
        if (name == "geom") {
            helo::verif::geom_test();
        } else if (name == "rast") {
            helo::verif::rast_test();
        } else if (name == "tex") {
            helo::verif::tex_test();
        } else if (name == "rop") {
            helo::verif::rop_test();
        } else {
            throw std::runtime_error("unknown graphics module");
        }
        std::cout << name << ": PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
