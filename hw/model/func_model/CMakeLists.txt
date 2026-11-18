add_library(helo_func_model STATIC
    gpu/gpu.cpp
    cp/cp.cpp
    geom/vfetch.cpp
    geom/pa.cpp
    geom/clip.cpp
    geom/vp.cpp
    geom/geom.cpp
    rast/setup.cpp
    rast/fixed.cpp
    rast/interp.cpp
    rast/rast.cpp
    tex/tex.cpp
    rop/zrop.cpp
    rop/crop.cpp
    rop/rop.cpp
    mem/mem.cpp
    mem/icache.cpp
    mem/dcache.cpp
    shader/shader.cpp
    shader/wmu.cpp
    shader/wsu.cpp
    shader/ifu.cpp
    shader/idu.cpp
    shader/isu.cpp
    shader/ibuffer.cpp
    shader/scoreboard.cpp
    shader/regfile.cpp
    shader/wbu.cpp
    shader/wcu.cpp
    shader/iu.cpp
    shader/fpu.cpp
    shader/lsu.cpp
    shader/sfu.cpp
)
target_include_directories(helo_func_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(helo_func_model PUBLIC helo_protocol PRIVATE helo_softfloat)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(helo_func_model PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
elseif(MSVC)
    target_compile_options(helo_func_model PRIVATE /fp:strict)
endif()
