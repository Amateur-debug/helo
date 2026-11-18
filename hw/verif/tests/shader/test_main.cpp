#include "fpu_test.h"
#include "ibuffer_test.h"
#include "idu_test.h"
#include "ifu_test.h"
#include "isu_test.h"
#include "iu_test.h"
#include "lsu_test.h"
#include "regfile_test.h"
#include "scoreboard_test.h"
#include "shader_test.h"
#include "wbu_test.h"
#include "wcu_test.h"
#include "wmu_test.h"
#include "wsu_test.h"
#include <iostream>
#include <string>

// 按 CTest 参数执行一个共用功能用例并保留失败状态。
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected test name");
        }
        const std::string name = argv[1];
        using namespace helo::verif;
        if (name == "shader") {
            shader_test();
        } else if (name == "iu") {
            iu_test();
        } else if (name == "fpu") {
            fpu_test();
        } else if (name == "lsu") {
            lsu_test();
        } else if (name == "wcu") {
            wcu_test();
        } else if (name == "regfile") {
            regfile_test();
        } else if (name == "scoreboard") {
            scoreboard_test();
        } else if (name == "idu") {
            idu_test();
        } else if (name == "ibuffer") {
            ibuffer_test();
        } else if (name == "isu") {
            isu_test();
        } else if (name == "wbu") {
            wbu_test();
        } else if (name == "ifu") {
            ifu_test();
        } else if (name == "wmu") {
            wmu_test();
        } else if (name == "wsu") {
            wsu_test();
        } else {
            throw std::runtime_error("unknown test name");
        }
        std::cout << name << ": PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
