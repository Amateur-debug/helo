if(NOT HELO_DUT STREQUAL "func" AND NOT HELO_DUT STREQUAL "rtl")
    message(FATAL_ERROR "DUT '${HELO_DUT}' is not implemented; no placeholder tests are available")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(HELO_CLANG_FORMAT NAMES clang-format-18 clang-format REQUIRED)
add_custom_target(format-check
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
        --name cpp_style_check --dut host --logs "${PROJECT_SOURCE_DIR}/logs"
        --build "${PROJECT_BINARY_DIR}" --timeout 30 --
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/format_cpp.py"
        --clang-format "${HELO_CLANG_FORMAT}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
add_test(NAME cpp_style_test
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
        --name cpp_style_test --dut host --logs "${PROJECT_SOURCE_DIR}/logs"
        --build "${PROJECT_BINARY_DIR}" --timeout 30 --
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/format_cpp.py"
        --clang-format "${HELO_CLANG_FORMAT}")
set_tests_properties(cpp_style_test PROPERTIES TIMEOUT 40 LABELS "style")

if(HELO_DUT STREQUAL "rtl")
    find_program(HELO_VERILATOR verilator REQUIRED)
    file(GLOB_RECURSE rtl_systemverilog_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/hw/rtl/*.sv")
    if(rtl_systemverilog_sources)
        message(FATAL_ERROR "RTL must use Verilog-2005 .v files: ${rtl_systemverilog_sources}")
    endif()
    set(rtl_iu_ops "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu_ops.vh")
    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_memory_dir "${PROJECT_BINARY_DIR}/verilated_memory/$<CONFIG>")
        set(rtl_memory_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/memory_tests_rtl")
    else()
        set(rtl_memory_dir "${PROJECT_BINARY_DIR}/verilated_memory")
        set(rtl_memory_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/memory_tests_rtl")
    endif()
    set(rtl_memory_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/dut/rtl_dut.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mem/mem_test_main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mem/mem_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mem/icache_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mem/dcache_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mem/memory_compare_test.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/mem.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/icache.cpp")
    add_custom_command(OUTPUT "${rtl_memory_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_memory -GMAX_BYTES=4096
            --prefix Vhelo_memory --Mdir "${rtl_memory_dir}" -o "${rtl_memory_bin}"
            -CFLAGS "-std=c++17 -DHELO_MEMORY_RTL_COMPARE -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_CURRENT_SOURCE_DIR}/tests -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_memory_sources}
        DEPENDS ${rtl_memory_sources}
        VERBATIM)
    add_custom_target(memory_tests_rtl ALL DEPENDS "${rtl_memory_bin}")
    foreach(unit mem icache dcache)
        add_test(NAME ${unit}_test_rtl
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
                --name ${unit}_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
                --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_memory_bin}" ${unit})
        set_tests_properties(${unit}_test_rtl PROPERTIES TIMEOUT 40 LABELS "memory;rtl;${unit}")
    endforeach()
    add_test(NAME memory_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name memory_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_memory_bin}" compare)
    set_tests_properties(memory_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "memory;rtl;compare")
    add_test(NAME rtl_memory_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_memory_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_memory
                "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v")
    set_tests_properties(rtl_memory_lint_test PROPERTIES TIMEOUT 40 LABELS "memory;rtl;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_iu_dir "${PROJECT_BINARY_DIR}/verilated_iu/$<CONFIG>")
        set(rtl_iu_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/iu_tests_rtl")
    else()
        set(rtl_iu_dir "${PROJECT_BINARY_DIR}/verilated_iu")
        set(rtl_iu_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/iu_tests_rtl")
    endif()
    set(rtl_iu_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/iu_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/shader/iu.cpp")
    add_custom_command(OUTPUT "${rtl_iu_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_iu
            --prefix Vhelo_iu --Mdir "${rtl_iu_dir}" -o "${rtl_iu_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_iu_sources}
        DEPENDS ${rtl_iu_sources} "${rtl_iu_ops}"
        VERBATIM)
    add_custom_target(iu_tests_rtl ALL DEPENDS "${rtl_iu_bin}")
    add_test(NAME iu_datapath_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name iu_datapath_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_iu_bin}")
    set_tests_properties(iu_datapath_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;iu")
    add_test(NAME rtl_iu_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_iu_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_iu
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v")
    set_tests_properties(rtl_iu_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;iu;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_idu_dir "${PROJECT_BINARY_DIR}/verilated_idu/$<CONFIG>")
        set(rtl_idu_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/idu_tests_rtl")
    else()
        set(rtl_idu_dir "${PROJECT_BINARY_DIR}/verilated_idu")
        set(rtl_idu_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/idu_tests_rtl")
    endif()
    set(rtl_idu_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/idu_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/shader/idu.cpp")
    add_custom_command(OUTPUT "${rtl_idu_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_idu
            --prefix Vhelo_idu --Mdir "${rtl_idu_dir}" -o "${rtl_idu_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_idu_sources}
        DEPENDS ${rtl_idu_sources} "${rtl_iu_ops}"
        VERBATIM)
    add_custom_target(idu_tests_rtl ALL DEPENDS "${rtl_idu_bin}")
    add_test(NAME idu_decode_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name idu_decode_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_idu_bin}")
    set_tests_properties(idu_decode_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;idu")
    add_test(NAME rtl_idu_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_idu_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_idu
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v")
    set_tests_properties(rtl_idu_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;idu;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_wcu_dir "${PROJECT_BINARY_DIR}/verilated_wcu/$<CONFIG>")
        set(rtl_wcu_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/wcu_tests_rtl")
    else()
        set(rtl_wcu_dir "${PROJECT_BINARY_DIR}/verilated_wcu")
        set(rtl_wcu_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/wcu_tests_rtl")
    endif()
    set(rtl_wcu_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/wcu_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/shader/wcu.cpp")
    add_custom_command(OUTPUT "${rtl_wcu_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_wcu
            --prefix Vhelo_wcu --Mdir "${rtl_wcu_dir}" -o "${rtl_wcu_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_wcu_sources}
        DEPENDS ${rtl_wcu_sources}
        VERBATIM)
    add_custom_target(wcu_tests_rtl ALL DEPENDS "${rtl_wcu_bin}")
    add_test(NAME wcu_datapath_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name wcu_datapath_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_wcu_bin}")
    set_tests_properties(wcu_datapath_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;wcu")
    add_test(NAME rtl_wcu_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_wcu_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_wcu
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v")
    set_tests_properties(rtl_wcu_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;wcu;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_lane_dir "${PROJECT_BINARY_DIR}/verilated_shader_lane/$<CONFIG>")
        set(rtl_lane_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/shader_lane_tests_rtl")
    else()
        set(rtl_lane_dir "${PROJECT_BINARY_DIR}/verilated_shader_lane")
        set(rtl_lane_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shader_lane_tests_rtl")
    endif()
    set(rtl_lane_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_lsu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/shader_lane_rtl_test.cpp")
    add_custom_command(OUTPUT "${rtl_lane_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_shader_lane
            --prefix Vhelo_shader_lane --Mdir "${rtl_lane_dir}" -o "${rtl_lane_bin}"
            -CFLAGS "-std=c++17"
            ${rtl_lane_sources}
        DEPENDS ${rtl_lane_sources} "${rtl_iu_ops}"
        VERBATIM)
    add_custom_target(shader_lane_tests_rtl ALL DEPENDS "${rtl_lane_bin}")
    add_test(NAME shader_lane_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name shader_lane_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_lane_bin}")
    set_tests_properties(shader_lane_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;lane")
    add_test(NAME rtl_shader_lane_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_shader_lane_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_shader_lane
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_lsu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane.v")
    set_tests_properties(rtl_shader_lane_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;lane;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_lsu_dir "${PROJECT_BINARY_DIR}/verilated_lsu/$<CONFIG>")
        set(rtl_lsu_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/lsu_tests_rtl")
    else()
        set(rtl_lsu_dir "${PROJECT_BINARY_DIR}/verilated_lsu")
        set(rtl_lsu_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lsu_tests_rtl")
    endif()
    set(rtl_lsu_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_lsu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane_memory.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/lsu_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/shader/lsu.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/dcache.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/mem.cpp")
    add_custom_command(OUTPUT "${rtl_lsu_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_shader_lane_memory
            --prefix Vhelo_shader_lane_memory --Mdir "${rtl_lsu_dir}" -o "${rtl_lsu_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_lsu_sources}
        DEPENDS ${rtl_lsu_sources} "${rtl_iu_ops}"
        VERBATIM)
    add_custom_target(lsu_tests_rtl ALL DEPENDS "${rtl_lsu_bin}")
    add_test(NAME lsu_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name lsu_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_lsu_bin}")
    set_tests_properties(lsu_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;lsu")
    add_test(NAME rtl_lsu_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_lsu_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_shader_lane_memory
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_lsu.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane_memory.v"
                "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v")
    set_tests_properties(rtl_lsu_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;lsu;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_core_dir "${PROJECT_BINARY_DIR}/verilated_shader_core/$<CONFIG>")
        set(rtl_core_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/shader_core_tests_rtl")
    else()
        set(rtl_core_dir "${PROJECT_BINARY_DIR}/verilated_shader_core")
        set(rtl_core_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shader_core_tests_rtl")
    endif()
    set(rtl_core_modules
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_idu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_iu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_wcu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_lsu.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_lane.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_regfile.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_shader_core.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v")
    set(rtl_core_sources ${rtl_core_modules}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/shader_core_rtl_compare.cpp")
    add_custom_command(OUTPUT "${rtl_core_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_shader_core
            --prefix Vhelo_shader_core --Mdir "${rtl_core_dir}" -o "${rtl_core_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model -I${PROJECT_SOURCE_DIR}/hw/include"
            ${rtl_core_sources}
            "$<TARGET_FILE:helo_func_model>"
            "$<TARGET_FILE:helo_softfloat>"
        DEPENDS ${rtl_core_sources} "${rtl_iu_ops}" helo_func_model
        VERBATIM)
    add_custom_target(shader_core_tests_rtl ALL DEPENDS "${rtl_core_bin}")
    add_test(NAME shader_core_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name shader_core_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_core_bin}")
    set_tests_properties(shader_core_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;core")
    add_test(NAME rtl_shader_core_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_shader_core_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_shader_core ${rtl_core_modules})
    set_tests_properties(rtl_shader_core_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;core;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_regfile_dir "${PROJECT_BINARY_DIR}/verilated_regfile/$<CONFIG>")
        set(rtl_regfile_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/regfile_tests_rtl")
    else()
        set(rtl_regfile_dir "${PROJECT_BINARY_DIR}/verilated_regfile")
        set(rtl_regfile_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/regfile_tests_rtl")
    endif()
    set(rtl_regfile_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_regfile.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader/regfile_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/shader/regfile.cpp")
    add_custom_command(OUTPUT "${rtl_regfile_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_regfile
            --prefix Vhelo_regfile --Mdir "${rtl_regfile_dir}" -o "${rtl_regfile_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model"
            ${rtl_regfile_sources}
        DEPENDS ${rtl_regfile_sources}
        VERBATIM)
    add_custom_target(regfile_tests_rtl ALL DEPENDS "${rtl_regfile_bin}")
    add_test(NAME regfile_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name regfile_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_regfile_bin}")
    set_tests_properties(regfile_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "shader;rtl;regfile")
    add_test(NAME rtl_regfile_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_regfile_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_regfile
                "${PROJECT_SOURCE_DIR}/hw/rtl/shader/helo_regfile.v")
    set_tests_properties(rtl_regfile_lint_test PROPERTIES TIMEOUT 40 LABELS "shader;rtl;regfile;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_rast_dir "${PROJECT_BINARY_DIR}/verilated_rast_coverage/$<CONFIG>")
        set(rtl_rast_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/rast_coverage_tests_rtl")
    else()
        set(rtl_rast_dir "${PROJECT_BINARY_DIR}/verilated_rast_coverage")
        set(rtl_rast_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rast_coverage_tests_rtl")
    endif()
    set(rtl_rast_sources
        "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_coverage.v"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rast/rast_coverage_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/rast.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/fixed.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/setup.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/interp.cpp")
    add_custom_command(OUTPUT "${rtl_rast_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_rast_coverage
            --prefix Vhelo_rast_coverage --Mdir "${rtl_rast_dir}" -o "${rtl_rast_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model -I${PROJECT_SOURCE_DIR}/hw/include"
            ${rtl_rast_sources}
        DEPENDS ${rtl_rast_sources}
        VERBATIM)
    add_custom_target(rast_coverage_tests_rtl ALL DEPENDS "${rtl_rast_bin}")
    add_test(NAME rast_coverage_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rast_coverage_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_rast_bin}")
    set_tests_properties(rast_coverage_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "rast;rtl;coverage")
    add_test(NAME rtl_rast_coverage_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_rast_coverage_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_rast_coverage
                "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_coverage.v")
    set_tests_properties(rtl_rast_coverage_lint_test PROPERTIES TIMEOUT 40 LABELS "rast;rtl;coverage;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_rop_dir "${PROJECT_BINARY_DIR}/verilated_rop/$<CONFIG>")
        set(rtl_rop_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/rop_tests_rtl")
    else()
        set(rtl_rop_dir "${PROJECT_BINARY_DIR}/verilated_rop")
        set(rtl_rop_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rop_tests_rtl")
    endif()
    set(rtl_rop_modules
        "${PROJECT_SOURCE_DIR}/hw/rtl/rop/helo_rop_fragment.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rop/helo_rop_memory.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v")
    set(rtl_rop_sources ${rtl_rop_modules}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rop/rop_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/rop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/zrop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/crop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/mem.cpp")
    add_custom_command(OUTPUT "${rtl_rop_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_rop_memory
            --prefix Vhelo_rop_memory --Mdir "${rtl_rop_dir}" -o "${rtl_rop_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model -I${PROJECT_SOURCE_DIR}/hw/include"
            ${rtl_rop_sources}
        DEPENDS ${rtl_rop_sources}
        VERBATIM)
    add_custom_target(rop_tests_rtl ALL DEPENDS "${rtl_rop_bin}")
    add_test(NAME rop_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rop_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_rop_bin}")
    set_tests_properties(rop_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "rop;rtl;compare")
    add_test(NAME rtl_rop_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_rop_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_rop_memory ${rtl_rop_modules})
    set_tests_properties(rtl_rop_lint_test PROPERTIES TIMEOUT 40 LABELS "rop;rtl;lint")

    if(CMAKE_CONFIGURATION_TYPES)
        set(rtl_draw_dir "${PROJECT_BINARY_DIR}/verilated_flat_draw/$<CONFIG>")
        set(rtl_draw_bin "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/flat_draw_tests_rtl")
    else()
        set(rtl_draw_dir "${PROJECT_BINARY_DIR}/verilated_flat_draw")
        set(rtl_draw_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/flat_draw_tests_rtl")
    endif()
    set(rtl_draw_modules
        "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_coverage.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_depth_q16.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_uv_q16.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rast/helo_rast_color_q8.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/tex/helo_tex_nearest_q16.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rop/helo_rop_fragment.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/rop/helo_rop_memory.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/mem/helo_memory.v"
        "${PROJECT_SOURCE_DIR}/hw/rtl/gpu/helo_flat_draw.v")
    set(rtl_draw_sources ${rtl_draw_modules}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu/flat_draw_rtl_compare.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/rast.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/fixed.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/setup.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rast/interp.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/tex/tex.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/rop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/zrop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/rop/crop.cpp"
        "${PROJECT_SOURCE_DIR}/hw/model/func_model/mem/mem.cpp")
    add_custom_command(OUTPUT "${rtl_draw_bin}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
            --cc --exe --build -j 4 --top-module helo_flat_draw
            --prefix Vhelo_flat_draw --Mdir "${rtl_draw_dir}" -o "${rtl_draw_bin}"
            -CFLAGS "-std=c++17 -I${PROJECT_SOURCE_DIR}/hw/model/func_model -I${PROJECT_SOURCE_DIR}/hw/include"
            ${rtl_draw_sources}
        DEPENDS ${rtl_draw_sources}
        VERBATIM)
    add_custom_target(flat_draw_tests_rtl ALL DEPENDS "${rtl_draw_bin}")
    add_test(NAME flat_draw_compare_test_rtl
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name flat_draw_compare_test_rtl --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- "${rtl_draw_bin}")
    set_tests_properties(flat_draw_compare_test_rtl PROPERTIES TIMEOUT 40 LABELS "gpu;rast;rop;rtl;flat")
    add_test(NAME rtl_flat_draw_lint_test
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name rtl_flat_draw_lint_test --dut rtl --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 --
            "${CMAKE_COMMAND}" -E env LC_ALL=C "${HELO_VERILATOR}" --language 1364-2005 "-I${PROJECT_SOURCE_DIR}/hw/rtl/shader"
                --lint-only -Wall --top-module helo_flat_draw ${rtl_draw_modules})
    set_tests_properties(rtl_flat_draw_lint_test PROPERTIES TIMEOUT 40 LABELS "gpu;rast;rop;rtl;flat;lint")
    return()
endif()

add_executable(func_resources_tests tests/func/func_resources_test.cpp)
target_include_directories(func_resources_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(func_resources_tests PRIVATE helo_func_model)
add_test(NAME func_resources_test
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
        --name func_resources_test --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
        --build "${PROJECT_BINARY_DIR}" --timeout 30 -- $<TARGET_FILE:func_resources_tests>)
set_tests_properties(func_resources_test PROPERTIES TIMEOUT 40 LABELS "func;implementation")
add_library(protocol_headers_check OBJECT tests/common/protocol_headers_test.cpp)
target_include_directories(protocol_headers_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(protocol_headers_check PRIVATE helo_protocol)
add_executable(shader_tests_func
    dut/func_dut.cpp
    tests/shader/test_main.cpp
    tests/shader/shader_test.cpp
    tests/shader/iu_test.cpp
    tests/shader/fpu_test.cpp
    tests/shader/lsu_test.cpp
    tests/shader/wcu_test.cpp
    tests/shader/regfile_test.cpp
    tests/shader/scoreboard_test.cpp
    tests/shader/idu_test.cpp
    tests/shader/ibuffer_test.cpp
    tests/shader/isu_test.cpp
    tests/shader/wbu_test.cpp
    tests/shader/ifu_test.cpp
    tests/shader/wmu_test.cpp
    tests/shader/wsu_test.cpp)
target_include_directories(shader_tests_func PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shader_tests_func PRIVATE helo_func_model)
foreach(unit shader iu fpu lsu wcu regfile scoreboard idu ibuffer isu wbu ifu wmu wsu)
    add_test(NAME ${unit}_test_func
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name ${unit}_test_func --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- $<TARGET_FILE:shader_tests_func> ${unit})
    set_tests_properties(${unit}_test_func PROPERTIES TIMEOUT 40 LABELS "shader;func;${unit}")
endforeach()

add_executable(graphics_tests_func
    dut/func_geom_dut.cpp
    dut/func_rast_dut.cpp
    dut/func_tex_dut.cpp
    dut/func_rop_dut.cpp
    tests/graphics_test_main.cpp
    tests/geom/geom_test.cpp
    tests/rast/rast_test.cpp
    tests/tex/tex_test.cpp
    tests/rop/rop_test.cpp)
target_include_directories(graphics_tests_func PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(graphics_tests_func PRIVATE helo_func_model)
foreach(unit geom rast tex rop)
    add_test(NAME ${unit}_test_func
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name ${unit}_test_func --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- $<TARGET_FILE:graphics_tests_func> ${unit})
    set_tests_properties(${unit}_test_func PROPERTIES TIMEOUT 40 LABELS "graphics;func;${unit}")
endforeach()

add_executable(memory_tests_func
    dut/func_memory_dut.cpp
    tests/mem/mem_test_main.cpp
    tests/mem/mem_test.cpp
    tests/mem/icache_test.cpp
    tests/mem/dcache_test.cpp)
target_include_directories(memory_tests_func PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(memory_tests_func PRIVATE helo_func_model)
foreach(unit mem icache dcache)
    add_test(NAME ${unit}_test_func
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name ${unit}_test_func --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 30 -- $<TARGET_FILE:memory_tests_func> ${unit})
    set_tests_properties(${unit}_test_func PROPERTIES TIMEOUT 40 LABELS "memory;func;${unit}")
endforeach()

add_executable(gpu_tests_func
    dut/func_gpu_dut.cpp
    tests/gpu/gpu_test_main.cpp
    tests/gpu/gpu_test.cpp
    tests/gpu/raster_outputs_test.cpp
    tests/gpu/shader_texture_test.cpp
    tests/cp/cp_test.cpp
    tests/common/png_writer.cpp
    tests/common/command_buffer.cpp)
target_include_directories(gpu_tests_func PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gpu_tests_func PRIVATE helo_func_model)
foreach(unit gpu shader_texture raster_outputs cp)
    add_test(NAME ${unit}_test_func
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
            --name ${unit}_test_func --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
            --build "${PROJECT_BINARY_DIR}" --timeout 120 -- $<TARGET_FILE:gpu_tests_func> ${unit})
    set_tests_properties(${unit}_test_func PROPERTIES TIMEOUT 130 LABELS "graphics;func;${unit}")
endforeach()
set(HELO_RENDER_IMAGE "${PROJECT_BINARY_DIR}/artifacts/gpu_scene.png")
add_test(NAME gpu_render_test_func
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/run_test.py"
        --name gpu_render_test_func --dut func --logs "${PROJECT_SOURCE_DIR}/logs"
        --build "${PROJECT_BINARY_DIR}" --timeout 180 -- $<TARGET_FILE:gpu_tests_func> render "${HELO_RENDER_IMAGE}")
set_tests_properties(gpu_render_test_func PROPERTIES TIMEOUT 190 RUN_SERIAL TRUE LABELS "graphics;func;gpu;render")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(func_resources_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(graphics_tests_func PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(memory_tests_func PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gpu_tests_func PRIVATE -Wall -Wextra -Wpedantic)
endif()
