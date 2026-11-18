#include "gpu_test.h"
#include "tests/common/png_writer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>

namespace helo::verif {
using helo::Color;
using helo::Vertex;

namespace {
constexpr std::uint32_t VERTEX_BASE = 0x10000;
constexpr std::uint32_t INDEX_BASE = 0x8000;
constexpr std::uint32_t VS_BASE = 0x1000;
constexpr std::uint32_t FS_BASE = 0x2000;
constexpr std::uint32_t TEXTURE_BASE = 0x20000;

// 返回独立主机浮点常量对应的存储位模式。
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 构造带统一颜色及标准UV的全屏四顶点方形。
std::vector<Vertex> quad(float depth, Color color) {
    return {{{-1, -1, depth, 1}, color, {0, 1}},
        {{1, -1, depth, 1}, color, {1, 1}},
        {{1, 1, depth, 1}, color, {1, 0}},
        {{-1, 1, depth, 1}, color, {0, 0}}};
}

// 装载四顶点方形的独立三角形索引。
void quad_indices(GpuDut& dut) {
    const unsigned indices[] = {0, 1, 2, 0, 2, 3};
    for (unsigned i = 0; i < 6; ++i) {
        dut.write(INDEX_BASE + 4 * i, 4, indices[i]);
    }
}

// 装载真正执行浮点纹理调制的片元程序。
void texture_program(GpuDut& dut) {
    dut.upload_program(
        FS_BASE, {fp(2, 1, 1, 5), fp(2, 2, 2, 6), fp(2, 3, 3, 7), fp(2, 4, 4, 8), EXIT});
}

// 按每通道至多一个UNORM码值的误差检查指定像素。
void pixel_is(const std::vector<std::uint8_t>& pixels,
    unsigned width,
    unsigned x,
    unsigned y,
    std::array<unsigned, 4> expected,
    const char* description) {
    const auto offset = (std::size_t(y) * width + x) * 4;
    for (unsigned channel = 0; channel < 4; ++channel) {
        require(std::abs(int(pixels[offset + channel]) - int(expected[channel])) <= 1, description);
    }
}
}

// 将协议顶点逐字段编码为设备定义的40字节小端记录。
void upload_vertices(GpuDut& dut, std::uint32_t address, const std::vector<Vertex>& vertices) {
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        auto cursor = address + static_cast<std::uint32_t>(i * 40);
        for (const auto value : vertices[i].m_position) {
            dut.write(cursor, 4, bits(value));
            cursor += 4;
        }
        for (const auto value : vertices[i].m_color) {
            dut.write(cursor, 4, bits(value));
            cursor += 4;
        }
        for (const auto value : vertices[i].m_uv) {
            dut.write(cursor, 4, bits(value));
            cursor += 4;
        }
    }
}

// 配置互不重叠的颜色和深度附件。
helo::FramebufferDesc test_framebuffer(unsigned width, unsigned height) {
    helo::FramebufferDesc framebuffer;
    framebuffer.m_width = width;
    framebuffer.m_height = height;
    framebuffer.m_color_address = 0x400000;
    framebuffer.m_depth_address = 0x800000;
    return framebuffer;
}

// 创建带标准视口和真实片元程序的索引绘制命令。
helo::DrawCommand test_draw(const helo::FramebufferDesc& framebuffer) {
    helo::DrawCommand draw;
    draw.m_framebuffer = framebuffer;
    draw.m_geometry.m_viewport.m_width = static_cast<float>(framebuffer.m_width);
    draw.m_geometry.m_viewport.m_height = static_cast<float>(framebuffer.m_height);
    draw.m_vertex_address = VERTEX_BASE;
    draw.m_vertex_count = 4;
    draw.m_indexed = true;
    draw.m_index_address = INDEX_BASE;
    draw.m_index_count = 6;
    return draw;
}

// 完成队列并将设备失败转为测试失败。
void complete(GpuDut& dut) {
    const auto result = dut.run();
    if (!result.m_fault.m_message.empty()) {
        throw std::runtime_error(result.m_fault.m_message);
    }
    require(result.completed() && dut.pending() == 0, "GPU queue did not complete");
}

// 验证顶层配置传递、较小驻留容量下的图形分批和计算任务容量边界。
static void check_gpu_configuration() {
    GpuConfig config;
    config.m_memory_bytes = 32 * 1024 * 1024;
    config.m_shader.m_max_warps = 2;
    config.m_shader.m_max_threads = 64;
    config.m_shader.m_regfile_bytes = 8192;
    config.m_shader.m_execution.m_fpu_count = 3;
    auto configured = make_gpu_dut(config);
    auto reference = make_gpu_dut();
    configured->write(0x1000000, 4, 0x12345678);
    require(configured->read(0x1000000, 4) == 0x12345678, "GPU memory configuration was ignored");
    const auto framebuffer = test_framebuffer();
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    auto draw = test_draw(framebuffer);
    draw.m_vertex_shader = {true, VS_BASE};
    draw.m_fragment_shader = {true, FS_BASE};
    for (auto* dut : {configured.get(), reference.get()}) {
        quad_indices(*dut);
        upload_vertices(*dut, VERTEX_BASE, quad(0, {1, 0, 0, 1}));
        dut->upload_program(VS_BASE, {EXIT});
        texture_program(*dut);
        dut->submit_commands({clear, draw});
        complete(*dut);
    }
    require(configured->read_color(framebuffer) == reference->read_color(framebuffer),
        "GPU configuration changed rendered pixels");
    configured->reset();
    helo::ComputeCommand compute;
    compute.m_entry = VS_BASE;
    compute.m_thread_count = 65;
    configured->submit(compute);
    complete(*configured);
    require(configured->counters().m_shader_steps == 3,
        "GPU did not execute both resident batches of a 65-thread dispatch");
    configured->reset();
    compute.m_thread_count = 64;
    configured->submit(compute);
    complete(*configured);
    // 命令暂存区也必须跟随小内存配置，不能保留默认16MiB布局。
    config.m_memory_bytes = 65536;
    auto small = make_gpu_dut(config);
    small->upload_program(VS_BASE, {EXIT});
    small->submit(compute);
    complete(*small);
    require(
        small->counters().m_dispatches == 1, "small GPU used hard-coded command staging address");
    config.m_shader.m_execution.m_lsu_count = 0;
    bool rejected = false;
    try {
        auto invalid = make_gpu_dut(config);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "GPU execution configuration was not forwarded");
}

// 在第二个片元批次触发陷阱，验证首批写回保留且失败批次不进入ROP。
static void check_fragment_batch_fault() {
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer();
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    auto draw = test_draw(framebuffer);
    draw.m_indexed = false;
    draw.m_vertex_count = 3;
    draw.m_fragment_shader = {true, FS_BASE};
    upload_vertices(*dut,
        VERTEX_BASE,
        {{{-1, 1, 0, 1}, {1, 0, 0, 1}, {0, 0}},
            {{1, 1, 0, 1}, {1, 0, 0, 1}, {0, 0}},
            {{-1, -1, 0, 1}, {1, 0, 0, 1}, {0, 0}}});
    // condition=4为BLTU，首批跳到EXIT，后续批次陷阱。
    dut->upload_program(
        FS_BASE, {imm(2, 16, 0, 0, 128), branch(15, 16, 4, 16), imm(12, 0, 2, 0, 7), EXIT});
    const auto address = dut->submit_commands({clear, draw});
    const auto result = dut->run();
    require(result.m_status == RunStatus::FAULT && result.m_commands == 1 &&
                result.m_fault.m_command_address == address + 256 &&
                result.m_fault.m_pc == FS_BASE + 16,
        "fragment fault lost command or instruction location");
    const auto pixels = dut->read_color(framebuffer);
    unsigned committed = 0;
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 32; ++x) {
            const auto offset = (y * 32 + x) * 4;
            if (pixels[offset]) {
                ++committed;
            }
        }
    }
    require(
        committed == 128 && dut->counters().m_passed == 128 && dut->counters().m_fragments == 128,
        "fragment fault changed successful batch boundary");
}

// 用介于浮点深度和量化深度之间的目标值检验整机RENDER精度选择。
static void check_fixed_depth_compare() {
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer(2, 2);
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    clear.m_depth = .500001f;
    auto draw = test_draw(framebuffer);
    quad_indices(*dut);
    upload_vertices(*dut, VERTEX_BASE, quad(1.f / 131072, {1, 0, 0, 1}));
    dut->submit_commands({clear, draw});
    complete(*dut);
    require(dut->counters().m_passed == 0, "unquantized depth should fail LESS");
    draw.m_precision = helo::RasterPrecision::Q4_Q16_V1;
    dut->submit_commands({clear, draw});
    complete(*dut);
    require(dut->counters().m_passed == 4 && dut->read(framebuffer.m_depth_address, 4) == bits(.5f),
        "Q16 depth must pass LESS and commit its exact binary32 representation");
}

// 通过顶点流偏移表、实际VS和FS验证可选属性、跨阶段写回与透视插值。
static void check_vertex_layout() {
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer(4, 4);
    auto draw = test_draw(framebuffer);
    draw.m_indexed = false;
    draw.m_vertex_count = 3;
    draw.m_vertex_stride = 20;
    draw.m_vertex_layout_address = INDEX_BASE;
    draw.m_varying_count = 2;
    // 记录首字为输入属性，位置位于后四字；第二个属性仅由VS生成。
    const std::array<std::uint32_t, 5> offsets{4, 0xffffffff, 0xffffffff, 0, 0xffffffff};
    for (unsigned i = 0; i < offsets.size(); ++i) {
        dut->write(INDEX_BASE + 4 * i, 4, offsets[i]);
    }
    const std::array<std::array<float, 5>, 3> records{
        {{0, -1, 1, 0, 1}, {1, 2, 2, 0, 2}, {0, -4, -4, 0, 4}}};
    for (unsigned i = 0; i < records.size(); ++i) {
        for (unsigned j = 0; j < records[i].size(); ++j) {
            dut->write(VERTEX_BASE + 20 * i + 4 * j, 4, bits(records[i][j]));
        }
    }
    draw.m_vertex_shader = {true, VS_BASE, 0, 50};
    draw.m_fragment_shader = {true, FS_BASE, 0, 50};
    dut->upload_program(VS_BASE, {fp(0, 49, 48, 48), EXIT});
    dut->upload_program(
        FS_BASE, {imm(2, 1, 0, 48, 0), imm(2, 2, 0, 49, 0), imm(2, 3, 0, 9, 0), EXIT});
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    const auto packet = encode_commands({draw});
    require(packet[57] == INDEX_BASE && packet[58] == 2, "vertex layout word indices");
    dut->submit_commands({clear, draw});
    complete(*dut);
    // 屏幕重心(3/4,1/8,1/8)，倒数w为(1,1/2,1/4)，属性期望为2/27。
    pixel_is(dut->read_color(framebuffer),
        4,
        0,
        0,
        {19, 38, 0, 255},
        "custom layout / VS-to-FS varying");
    draw.m_vertex_shader.m_enabled = false;
    dut->submit_commands({clear, draw});
    complete(*dut);
    pixel_is(dut->read_color(framebuffer),
        4,
        0,
        0,
        {19, 0, 0, 255},
        "disabled VS should forward vertex stream and default missing varying to zero");
    // 每次故障必须在执行顶点着色或写附件之前报告。
    const auto reject = [&](const helo::DrawCommand& bad) {
        dut->reset();
        dut->submit(bad);
        require(dut->run().m_status == RunStatus::FAULT && dut->counters().m_vertices == 0,
            "invalid vertex layout accepted or executed shader");
    };
    auto bad = draw;
    bad.m_varying_count = 17;
    reject(bad);
    bad = draw;
    bad.m_fragment_shader.m_register_count = 49;
    reject(bad);
    bad = draw;
    bad.m_vertex_layout_address = 0;
    reject(bad);
    bad.m_vertex_layout_address = 0xfffffffcU;
    reject(bad);
    dut->write(INDEX_BASE + 12, 4, 0xfffffffcU);
    reject(draw);
    dut->write(INDEX_BASE + 12, 4, 2);
    reject(draw);
    dut->write(INDEX_BASE + 12, 4, 0);
    dut->write(INDEX_BASE, 4, 0xffffffffU);
    reject(draw);
}

// 检验可编程顶点和片元、深度顺序、纹理、丢弃及跨批次ABI。
void gpu_test() {
    check_gpu_configuration();
    check_fragment_batch_fault();
    check_vertex_layout();
    check_fixed_depth_compare();
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer();
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    auto draw = test_draw(framebuffer);
    quad_indices(*dut);
    upload_vertices(*dut, VERTEX_BASE, quad(0, {1, 1, 1, 1}));
    texture_program(*dut);
    draw.m_fragment_shader = {true, FS_BASE};
    draw.m_texture_enabled = true;
    draw.m_texture = {TEXTURE_BASE, 2, 2};
    dut->write(TEXTURE_BASE, 4, 0xff0000ffU);
    dut->write(TEXTURE_BASE + 4, 4, 0xff00ff00U);
    dut->write(TEXTURE_BASE + 8, 4, 0xffff0000U);
    dut->write(TEXTURE_BASE + 12, 4, 0xffffffffU);
    dut->submit(clear);
    dut->submit(draw);
    complete(*dut);
    const auto textured = dut->read_color(framebuffer);
    pixel_is(textured, 32, 4, 4, {255, 0, 0, 255}, "top-left texture sample incorrect");
    pixel_is(textured, 32, 27, 4, {0, 255, 0, 255}, "top-right texture sample incorrect");
    pixel_is(textured, 32, 4, 27, {0, 0, 255, 255}, "bottom-left texture sample incorrect");
    require(dut->counters().m_fragments == 1024 && dut->counters().m_passed == 1024,
        "quad seam or fragment batching incorrect");

    // 改变真正的FS机器码，应改变相同资源生成的像素。
    dut->upload_program(FS_BASE, {fp(0, 1, 0, 0), fp(0, 2, 0, 0), fp(0, 3, 0, 0), EXIT});
    dut->submit(clear);
    dut->submit(draw);
    complete(*dut);
    pixel_is(dut->read_color(framebuffer),
        32,
        4,
        4,
        {0, 0, 0, 255},
        "fragment machine code did not affect output");
    texture_program(*dut);
    dut->upload_program(VS_BASE, {upper(16, 0, 0x3f000), fp(2, 1, 1, 16), EXIT});
    draw.m_vertex_shader = {true, VS_BASE};
    dut->submit(clear);
    dut->submit(draw);
    complete(*dut);
    pixel_is(dut->read_color(framebuffer),
        32,
        3,
        4,
        {0, 0, 0, 255},
        "vertex FP32 transform did not shrink coverage");
    pixel_is(dut->read_color(framebuffer),
        32,
        10,
        4,
        {255, 0, 0, 255},
        "vertex transformed interior missing");

    // discard应在任何颜色或深度写入之前生效。
    dut->upload_program(FS_BASE, {imm(0x02, 14, 0, 0, 1), EXIT});
    dut->submit(clear);
    dut->submit(draw);
    complete(*dut);
    pixel_is(dut->read_color(framebuffer), 32, 16, 16, {0, 0, 0, 255}, "discard wrote color");
    require(dut->read(framebuffer.m_depth_address + (16 * 32 + 16) * 4, 4) == bits(1),
        "discard wrote depth");
    draw.m_vertex_shader.m_enabled = false;
    draw.m_fragment_shader.m_enabled = false;
    draw.m_texture_enabled = false;
    const auto near_vertices = quad(-0.5F, {1, 0, 0, 1});
    const auto far_vertices = quad(0.5F, {0, 0, 1, 1});
    upload_vertices(*dut, VERTEX_BASE, near_vertices);
    upload_vertices(*dut, VERTEX_BASE + 0x1000, far_vertices);
    auto far_draw = draw;
    far_draw.m_vertex_address += 0x1000;
    dut->submit(clear);
    dut->submit(draw);
    dut->submit(far_draw);
    complete(*dut);
    const auto near_first = dut->read_color(framebuffer);
    dut->submit(clear);
    dut->submit(far_draw);
    dut->submit(draw);
    complete(*dut);
    require(
        dut->read_color(framebuffer) == near_first, "opaque depth result depends on draw order");
    pixel_is(near_first, 32, 16, 16, {255, 0, 0, 255}, "near fragment lost depth test");

    // 多批顶点与片元将各自全局ABI编号写回独立哨兵区域。
    dut->reset();
    std::vector<Vertex> vertices;
    const auto source = quad(0, {1, 1, 1, 1});
    for (unsigned i = 0; i < 44; ++i) {
        vertices.insert(vertices.end(), source.begin(), source.begin() + 3);
    }
    upload_vertices(*dut, VERTEX_BASE, vertices);
    const std::uint32_t vertex_ids = 0x30000, fragment_ids = 0x50000;
    dut->upload_program(VS_BASE,
        {imm(0x0c, 16, 8, 0, 0x15),
            imm(0x02, 17, 4, 11, 2),
            reg(0x01, 0, 16, 16, 17),
            store(16, 11, 2),
            EXIT});
    dut->upload_program(FS_BASE,
        {imm(0x0c, 16, 8, 0, 0x15),
            imm(0x02, 17, 4, 15, 2),
            reg(0x01, 0, 16, 16, 17),
            store(16, 15, 2),
            EXIT});
    draw.m_indexed = false;
    draw.m_vertex_count = 132;
    draw.m_vertex_shader = {true, VS_BASE, vertex_ids};
    draw.m_fragment_shader = {true, FS_BASE, fragment_ids};
    draw.m_rop.m_depth_stencil.m_depth_test = false;
    for (unsigned i = 0; i < 133; ++i) {
        dut->write(vertex_ids + i * 4, 4, 0xdeadbeefU);
    }
    const unsigned fragment_capacity = 44 * framebuffer.m_width * framebuffer.m_height;
    for (unsigned i = 0; i <= fragment_capacity; ++i) {
        dut->write(fragment_ids + i * 4, 4, 0xdeadbeefU);
    }
    dut->submit(clear);
    dut->submit(draw);
    complete(*dut);
    for (unsigned i = 0; i < 133; ++i) {
        require(dut->read(vertex_ids + i * 4, 4) == (i < 132 ? i : 0xdeadbeefU),
            "vertex ID reset between 128-call batches");
    }
    const auto counters = dut->counters();
    require(counters.m_vertices == 132 && counters.m_fragments == 44 * 32 * 33 / 2,
        "multi-batch triangle coverage incorrect");
    for (std::uint32_t i = 0; i < counters.m_fragments; ++i) {
        require(dut->read(fragment_ids + i * 4, 4) == i,
            "fragment ID reset across batches or triangles");
    }
    for (std::uint32_t i = static_cast<std::uint32_t>(counters.m_fragments); i <= fragment_capacity;
         ++i) {
        require(dut->read(fragment_ids + i * 4, 4) == 0xdeadbeefU, "tail fragment lane executed");
    }
    // 显式零索引绘制不得退回非索引路径或调用着色器。
    dut->reset();
    auto empty = test_draw(framebuffer);
    empty.m_index_count = 0;
    dut->submit(clear);
    dut->submit(empty);
    complete(*dut);
    require(dut->counters().m_draws == 1 && dut->counters().m_vertices == 0 &&
                dut->counters().m_fragments == 0,
        "empty indexed draw unexpectedly executed");
    upload_vertices(*dut, VERTEX_BASE, quad(0, {1, 1, 1, 1}));
    // 每次故障均检查锁存状态及后续清除命令保持不执行。
    const auto reject = [&](helo::DrawCommand bad) {
        dut->reset();
        dut->submit(clear);
        complete(*dut);
        auto later = clear;
        later.m_color = {0, 1, 0, 1};
        dut->submit(bad);
        dut->submit(later);
        const auto failure = dut->run();
        require(!failure.m_fault.m_message.empty() && !failure.completed() && dut->pending() == 2,
            "invalid graphics shader was accepted");
        require(dut->run().m_fault.m_message == failure.m_fault.m_message,
            "graphics shader error did not latch");
        pixel_is(dut->read_color(framebuffer),
            32,
            16,
            16,
            {0, 0, 0, 255},
            "clear after failed shader executed");
    };
    auto bad = test_draw(framebuffer);
    bad.m_vertex_shader = {true, VS_BASE, 0, 11};
    reject(bad);
    bad.m_vertex_shader.m_enabled = false;
    bad.m_fragment_shader = {true, FS_BASE, 0, 15};
    reject(bad);
    bad.m_fragment_shader.m_register_count = 32;
    dut->upload_program(FS_BASE, {imm(0x0c, 0, 2, 0, 7)});
    reject(bad);
    bad.m_fragment_shader.m_enabled = false;
    bad.m_vertex_shader = {true, VS_BASE, 0, 32, 1};
    dut->upload_program(VS_BASE, {NOP, EXIT});
    reject(bad);
    std::cout << "GPU: indexed texture quad, actual VS/FS edits, discard, depth order, "
              << counters.m_vertices << " vertices / " << counters.m_fragments
              << " fragments across batches verified\n";
}

namespace {
using Vector3 = std::array<double, 3>;
using Matrix4 = std::array<float, 16>;

// 保存独立场景构建产生的网格协议数据。
struct Mesh {
    std::vector<Vertex> m_vertices; // 保存待上传的世界空间顶点。
    std::vector<std::uint32_t> m_indices; // 保存独立三角形索引。
};

// 计算用于相机和场景生成的三维向量差。
Vector3 subtract(Vector3 a, Vector3 b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

// 计算相机基向量的叉积。
Vector3 cross(Vector3 a, Vector3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// 计算相机平移和光照所需的点积。
double dot(Vector3 a, Vector3 b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// 将有限非零向量归一为相机单位方向。
Vector3 normalized(Vector3 value) {
    const auto length = std::sqrt(dot(value, value));
    for (auto& component : value) {
        component /= length;
    }
    return value;
}

// 创建主机侧相机矩阵常量，实际顶点乘法由HELO机器码执行。
Matrix4 camera_matrix() {
    const Vector3 eye{6.0, 4.1, 8.0}, target{0, -0.1, 0};
    const auto forward = normalized(subtract(target, eye));
    const auto right = normalized(cross(forward, {0, 1, 0}));
    const auto up = cross(right, forward);
    const double view[16] = {right[0],
        right[1],
        right[2],
        -dot(right, eye),
        up[0],
        up[1],
        up[2],
        -dot(up, eye),
        -forward[0],
        -forward[1],
        -forward[2],
        dot(forward, eye),
        0,
        0,
        0,
        1};
    const double scale = 1.0 / std::tan(0.67 / 2), near = 0.1, far = 40;
    const double projection[16] = {scale / (4.0 / 3.0),
        0,
        0,
        0,
        0,
        scale,
        0,
        0,
        0,
        0,
        -(far + near) / (far - near),
        -2 * far * near / (far - near),
        0,
        0,
        -1,
        0};
    Matrix4 matrix{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            for (unsigned k = 0; k < 4; ++k) {
                matrix[row * 4 + col] +=
                    static_cast<float>(projection[row * 4 + k] * view[k * 4 + col]);
            }
        }
    }
    return matrix;
}

// 编码完整四乘四矩阵顶点程序，保留属性输入并写回四维裁剪坐标。
std::vector<std::uint64_t> matrix_program() {
    std::vector<std::uint64_t> program{imm(0x0c, 16, 8, 0, 0x15)};
    for (unsigned i = 0; i < 16; ++i) {
        program.push_back(imm(0x04, 32 + i, 4, 16, static_cast<int>(i * 4)));
    }
    for (unsigned row = 0; row < 4; ++row) {
        program.push_back(fp(2, 20 + row, 1, 32 + row * 4));
        for (unsigned col = 1; col < 4; ++col) {
            program.push_back(fused(0, 20 + row, 1 + col, 32 + row * 4 + col, 20 + row));
        }
    }
    for (unsigned row = 0; row < 4; ++row) {
        program.push_back(reg(0x01, 0x0a, 1 + row, 20 + row));
    }
    program.push_back(EXIT);
    return program;
}

// 将世界坐标四边形追加到独立索引网格中。
void face(Mesh& mesh, const std::array<Vector3, 4>& points, Color color, float uv_scale = 1) {
    const auto first = static_cast<std::uint32_t>(mesh.m_vertices.size());
    const std::array<std::array<float, 2>, 4> uv{
        {{0, 0}, {uv_scale, 0}, {uv_scale, uv_scale}, {0, uv_scale}}};
    for (unsigned i = 0; i < 4; ++i) {
        mesh.m_vertices.push_back({{static_cast<float>(points[i][0]),
                                       static_cast<float>(points[i][1]),
                                       static_cast<float>(points[i][2]),
                                       1},
            color,
            uv[i]});
    }
    for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        mesh.m_indices.push_back(first + index);
    }
}

// 生成带独立面法线光照色及纹理坐标的旋转立方体。
Mesh cube(Vector3 center, double radius, double rotation, Color base) {
    const std::array<Vector3, 8> corners{{{-1, -1, -1},
        {1, -1, -1},
        {1, 1, -1},
        {-1, 1, -1},
        {-1, -1, 1},
        {1, -1, 1},
        {1, 1, 1},
        {-1, 1, 1}}};
    const unsigned faces[6][4] = {
        {4, 5, 6, 7}, {1, 0, 3, 2}, {0, 4, 7, 3}, {5, 1, 2, 6}, {3, 7, 6, 2}, {0, 1, 5, 4}};
    const Vector3 normals[6] = {
        {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const auto light = normalized({-0.6, 1.0, 0.7});
    Mesh mesh;
    for (unsigned f = 0; f < 6; ++f) {
        std::array<Vector3, 4> points;
        for (unsigned i = 0; i < 4; ++i) {
            const auto p = corners[faces[f][i]];
            points[i] = {
                center[0] + radius * (p[0] * std::cos(rotation) + p[2] * std::sin(rotation)),
                center[1] + radius * p[1],
                center[2] + radius * (-p[0] * std::sin(rotation) + p[2] * std::cos(rotation))};
        }
        const auto n = normals[f];
        const Vector3 rotated{n[0] * std::cos(rotation) + n[2] * std::sin(rotation),
            n[1],
            -n[0] * std::sin(rotation) + n[2] * std::cos(rotation)};
        Color color = base;
        const auto illumination =
            static_cast<float>(0.38 + 0.62 * std::max(0.0, dot(rotated, light)));
        for (unsigned i = 0; i < 3; ++i) {
            color[i] *= illumination;
        }
        face(mesh, points, color);
    }
    return mesh;
}

// 创建重复采样时具有细网格与粗边框的独立RGBA纹理。
void scene_texture(GpuDut& dut, std::uint32_t address, bool floor) {
    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            std::uint32_t rgba;
            if (floor) {
                const bool line = x < 1 || y < 1;
                const unsigned shade = line ? 80 : (((x / 32 + y / 32) % 2) ? 19 : 24);
                rgba = shade | ((shade + 10) << 8) | ((shade + 16) << 16) | 0xff000000U;
            } else {
                const bool border = x < 2 || y < 2 || x > 61 || y > 61;
                const bool fine = x % 16 == 0 || y % 16 == 0;
                const unsigned shade =
                    border ? 255 : (fine ? 215 : (((x / 8 + y / 8) % 2) ? 172 : 193));
                rgba = shade | (shade << 8) | (shade << 16) | 0xff000000U;
            }
            dut.write(address + (y * 64 + x) * 4, 4, rgba);
        }
    }
}

// 将独立网格与纹理配置提交到真实命令队列，避免任何主机像素绘制。
void submit_mesh(GpuDut& dut,
    const Mesh& mesh,
    helo::DrawCommand state,
    std::uint32_t& cursor,
    std::vector<Command>& commands) {
    state.m_vertex_address = cursor;
    state.m_vertex_count = static_cast<std::uint32_t>(mesh.m_vertices.size());
    upload_vertices(dut, cursor, mesh.m_vertices);
    cursor += static_cast<std::uint32_t>(mesh.m_vertices.size() * 40);
    state.m_index_address = cursor;
    state.m_index_count = static_cast<std::uint32_t>(mesh.m_indices.size());
    for (std::size_t i = 0; i < mesh.m_indices.size(); ++i) {
        dut.write(cursor + static_cast<std::uint32_t>(i * 4), 4, mesh.m_indices[i]);
    }
    cursor += static_cast<std::uint32_t>(mesh.m_indices.size() * 4);
    commands.push_back(state);
}
}

// 逐像素验证可解析的纹理、半透明覆盖与深度组合，失败时生成期望及差异图。
static void check_composite_scene(const std::string& output_path) {
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer();
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    auto base = test_draw(framebuffer);
    base.m_vertex_shader = {true, VS_BASE};
    base.m_fragment_shader = {true, FS_BASE};
    base.m_texture_enabled = true;
    base.m_texture = {TEXTURE_BASE, 2, 2};
    quad_indices(*dut);
    dut->upload_program(VS_BASE, {EXIT});
    texture_program(*dut);
    const std::uint32_t texels[] = {0xff0000ffU, 0xff00ff00U, 0xffff0000U, 0xffffffffU};
    for (unsigned i = 0; i < 4; ++i) {
        dut->write(TEXTURE_BASE + i * 4, 4, texels[i]);
    }
    upload_vertices(*dut, VERTEX_BASE, quad(0, {1, 1, 1, 1}));
    auto overlay = base;
    overlay.m_vertex_address += 0x1000;
    overlay.m_texture_enabled = false;
    overlay.m_rop.m_blend = true;
    overlay.m_rop.m_depth_stencil.m_depth_write = false;
    auto left = quad(-.5f, {0, 0, 1, .5f});
    for (auto& vertex : left) {
        vertex.m_position[0] = (vertex.m_position[0] - 1) * .5f;
    }
    upload_vertices(*dut, overlay.m_vertex_address, left);
    auto hidden = base;
    hidden.m_vertex_address += 0x2000;
    hidden.m_texture_enabled = false;
    upload_vertices(*dut, hidden.m_vertex_address, quad(.5f, {1, 1, 0, 1}));
    dut->submit_commands({clear, base, overlay, hidden});
    complete(*dut);
    const auto actual = dut->read_color(framebuffer);
    std::vector<std::uint8_t> expected(actual.size()), difference(actual.size());
    std::size_t mismatches = 0;
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 32; ++x) {
            // 左上红色与半透明蓝色各占一半；左下原为蓝色，右半保留绿/白。
            const std::array<unsigned, 4> rgba =
                x < 16 ? (y < 16 ? std::array<unsigned, 4>{128, 0, 128, 255}
                                 : std::array<unsigned, 4>{0, 0, 255, 255})
                       : (y < 16 ? std::array<unsigned, 4>{0, 255, 0, 255}
                                 : std::array<unsigned, 4>{255, 255, 255, 255});
            for (unsigned channel = 0; channel < 4; ++channel) {
                const auto offset = (y * 32 + x) * 4 + channel;
                expected[offset] = static_cast<std::uint8_t>(rgba[channel]);
                const bool mismatch = actual[offset] != expected[offset];
                mismatches += mismatch;
                difference[offset] = channel == 3 ? 255 : mismatch ? 255 : 0;
            }
            require(dut->read(framebuffer.m_depth_address + (y * 32 + x) * 4, 4) == bits(.5f),
                "composite scene changed base depth");
        }
    }
    write_png(output_path + ".numeric.png", 32, 32, actual);
    if (mismatches) {
        write_png(output_path + ".expected.png", 32, 32, expected);
        write_png(output_path + ".diff.png", 32, 32, difference);
        throw std::runtime_error(
            "analytic composite image mismatch: " + std::to_string(mismatches) +
            " channels; see .expected.png and .diff.png");
    }
}

// 渲染完整真实机器码场景，并直接将设备颜色附件编码为PNG。
void gpu_render_test(const std::string& output_path) {
    check_composite_scene(output_path);
    auto dut = make_gpu_dut();
    const auto framebuffer = test_framebuffer(800, 600);
    helo::ClearCommand clear;
    clear.m_framebuffer = framebuffer;
    clear.m_color = {0.024F, 0.043F, 0.075F, 1};
    std::vector<Command> commands{clear};
    const auto matrix = camera_matrix();
    constexpr std::uint32_t MATRIX_BASE = 0x3000;
    for (unsigned i = 0; i < 16; ++i) {
        dut->write(MATRIX_BASE + i * 4, 4, bits(matrix[i]));
    }
    const auto vs = matrix_program();
    dut->upload_program(VS_BASE, vs);
    texture_program(*dut);
    scene_texture(*dut, TEXTURE_BASE, false);
    scene_texture(*dut, TEXTURE_BASE + 0x4000, true);
    auto state = test_draw(framebuffer);
    state.m_vertex_shader = {true, VS_BASE, MATRIX_BASE, 64};
    state.m_fragment_shader = {true, FS_BASE};
    state.m_texture_enabled = true;
    state.m_texture = {TEXTURE_BASE,
        64,
        64,
        helo::TextureFilter::BILINEAR,
        helo::TextureWrap::REPEAT,
        helo::TextureWrap::REPEAT};
    std::uint32_t cursor = 0x30000;
    Mesh floor;
    face(
        floor, {{{-8, -1.3, -8}, {8, -1.3, -8}, {8, -1.3, 8}, {-8, -1.3, 8}}}, {0.8F, 1, 1, 1}, 24);
    auto floor_state = state;
    floor_state.m_texture.m_address += 0x4000;
    submit_mesh(*dut, floor, floor_state, cursor, commands);

    // 独立几何网格跨越128顶点批次，保证完整场景也覆盖多批VS。
    Mesh grid;
    for (int i = -14; i <= 14; ++i) {
        const double p = i * 0.5, thickness = i == 0 ? 0.014 : 0.004;
        const Color color = i == 0 ? Color{0.08F, 0.45F, 0.55F, 1} : Color{0.05F, 0.14F, 0.2F, 1};
        face(grid,
            {{{p - thickness, -1.295, -7},
                {p + thickness, -1.295, -7},
                {p + thickness, -1.295, 7},
                {p - thickness, -1.295, 7}}},
            color);
        face(grid,
            {{{-7, -1.295, p - thickness},
                {7, -1.295, p - thickness},
                {7, -1.295, p + thickness},
                {-7, -1.295, p + thickness}}},
            color);
    }
    auto untextured = state;
    untextured.m_texture_enabled = false;
    submit_mesh(*dut, grid, untextured, cursor, commands);
    submit_mesh(
        *dut, cube({0, 0, 0}, 1.28, 0.28, {1.0F, 0.44F, 0.13F, 1}), state, cursor, commands);
    submit_mesh(*dut,
        cube({2.8, -0.62, -1.8}, 0.66, -0.25, {0.14F, 0.9F, 0.96F, 1}),
        state,
        cursor,
        commands);
    submit_mesh(*dut,
        cube({-2.4, -0.66, -1.3}, 0.60, -0.4, {0.60F, 0.36F, 1.0F, 1}),
        state,
        cursor,
        commands);
    submit_mesh(*dut,
        cube({-1.9, -0.86, 2.0}, 0.42, 0.5, {0.29F, 0.96F, 0.70F, 1}),
        state,
        cursor,
        commands);

    // 半透明轨道与面板经真实FS及ROP源透明度混合形成前后遮挡。
    Mesh orbit;
    constexpr double PI = 3.14159265358979323846;
    for (unsigned i = 0; i < 96; ++i) {
        const double a = 2 * PI * i / 96, b = 2 * PI * (i + 1) / 96;
        const double inner = 2.05, outer = 2.09;
        face(orbit,
            {{{inner * std::cos(a), 0.4 + 0.5 * std::sin(a), inner * std::sin(a)},
                {outer * std::cos(a), 0.4 + 0.5 * std::sin(a), outer * std::sin(a)},
                {outer * std::cos(b), 0.4 + 0.5 * std::sin(b), outer * std::sin(b)},
                {inner * std::cos(b), 0.4 + 0.5 * std::sin(b), inner * std::sin(b)}}},
            {0.15F, 0.85F, 1, 0.72F});
    }
    auto transparent = untextured;
    transparent.m_rop.m_blend = true;
    transparent.m_rop.m_depth_stencil.m_depth_write = false;
    submit_mesh(*dut, orbit, transparent, cursor, commands);
    Mesh glass;
    face(glass,
        {{{1.1, -1.2, 2.0}, {2.4, -1.2, 1.7}, {2.4, 1.1, 1.7}, {1.1, 0.5, 2.0}}},
        {0.1F, 0.66F, 1.0F, 0.23F});
    submit_mesh(*dut, glass, transparent, cursor, commands);
    dut->submit_commands(commands);
    complete(*dut);
    const auto pixels = dut->read_color(framebuffer);
    const auto counters = dut->counters();
    require(counters.m_draws >= 8 && counters.m_vertices > 512 && counters.m_fragments > 100000 &&
                counters.m_shader_steps > 1000,
        "render did not execute full multi-stage workload");
    std::set<std::uint32_t> colors;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        colors.insert(std::uint32_t(pixels[i]) | (std::uint32_t(pixels[i + 1]) << 8) |
                      (std::uint32_t(pixels[i + 2]) << 16));
    }
    require(colors.size() > 500, "rendered image lacks expected textured shading diversity");
    require(counters.m_passed < counters.m_fragments, "render scene did not exercise occlusion");
    // 顶部角落不被此场景几何覆盖，颜色由CLEAR常量独立量化得到。
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            pixel_is(pixels, 800, x, y, {6, 11, 19, 255}, "scene clear background changed");
            pixel_is(pixels, 800, 799 - x, y, {6, 11, 19, 255}, "scene clear background changed");
        }
    }
    write_png(output_path, 800, 600, pixels);
    std::cout << "PNG: " << output_path << " 800x600, unique RGB=" << colors.size() << "\n"
              << "Actual HELO VS instructions=" << vs.size()
              << ", FS instructions=5; draws=" << counters.m_draws
              << ", vertices=" << counters.m_vertices << ", triangles=" << counters.m_triangles
              << ", fragments=" << counters.m_fragments << ", passed=" << counters.m_passed
              << ", shader instruction groups=" << counters.m_shader_steps << '\n';
}
}
