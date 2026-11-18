#include "gpu_test.h"
#include "tests/cp/cp_test.h"
#include <iostream>

// 选择独立整机、命令处理或真实机器码渲染验证入口。
int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument("expected gpu, cp, or render selector");
        }
        const std::string selector = argv[1];
        if (selector == "gpu") {
            helo::verif::gpu_test();
        } else if (selector == "cp") {
            helo::verif::cp_test();
        } else if (selector == "render") {
            if (argc < 3) {
                throw std::invalid_argument("render requires PNG output path");
            }
            helo::verif::gpu_render_test(argv[2]);
        } else {
            throw std::invalid_argument("unknown test selector");
        }
        std::cout << "PASS " << selector << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
