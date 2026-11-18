# 编译 common 中自带的 Zfinx FP32 SoftFloat 子集，不依赖 Spike 子模块。
set(HELO_SOFTFLOAT_DIR "${PROJECT_SOURCE_DIR}/hw/model/func_model/common/softfloat")
include(TestBigEndian)
test_big_endian(HELO_BIG_ENDIAN)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/softfloat_include")
if(HELO_BIG_ENDIAN)
    file(WRITE "${PROJECT_BINARY_DIR}/softfloat_include/softfloat_config.h" "#define WORDS_BIGENDIAN 1\n")
else()
    file(WRITE "${PROJECT_BINARY_DIR}/softfloat_include/softfloat_config.h" "/* Little-endian host. */\n")
endif()

set(HELO_SOFTFLOAT_SOURCES
    f32_add.c f32_sub.c f32_mul.c f32_div.c f32_sqrt.c f32_mulAdd.c
    f32_eq.c f32_lt.c f32_le.c f32_lt_quiet.c f32_le_quiet.c
    f32_isSignalingNaN.c f32_classify.c
    f32_to_i32.c f32_to_ui32.c i32_to_f32.c ui32_to_f32.c
    s_addMagsF32.c s_subMagsF32.c s_mulAddF32.c
    s_roundPackToF32.c s_normRoundPackToF32.c s_normSubnormalF32Sig.c
    s_propagateNaNF32UI.c s_roundToI32.c s_roundToUI32.c
    s_approxRecipSqrt32_1.c s_approxRecipSqrt_1Ks.c
    s_countLeadingZeros8.c s_countLeadingZeros32.c s_countLeadingZeros64.c
    softfloat_state.c softfloat_raiseFlags.c
)
list(TRANSFORM HELO_SOFTFLOAT_SOURCES PREPEND "${HELO_SOFTFLOAT_DIR}/")
add_library(helo_softfloat STATIC ${HELO_SOFTFLOAT_SOURCES})
set_target_properties(helo_softfloat PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(helo_softfloat SYSTEM PUBLIC "${HELO_SOFTFLOAT_DIR}")
target_include_directories(helo_softfloat PRIVATE "${PROJECT_BINARY_DIR}/softfloat_include")
