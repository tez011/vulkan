#include "config.h"
#include "vkw/vkw.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#define GLM_FORCE_RADIANS
#include "fs.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

static inline void glfw_error_callback(int code, const char* description)
{
    spdlog::error("[glfw] {}: {}", code, description);
}

struct UniformBuffer {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 tex_coord;
};

GLFWwindow* create_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        spdlog::critical("failed to initialize GLFW");
        abort();
    }
    if (glfwVulkanSupported() == GLFW_FALSE) {
        spdlog::critical("failed to initialize GLFW: Vulkan not supported");
        abort();
    }
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    atexit(glfwTerminate);

    GLFWwindow* window = glfwCreateWindow(720, 480, "Untitled Window", nullptr, nullptr);
    if (window == nullptr) {
        spdlog::critical("glfwCreateWindow failed");
        abort();
    }
    return window;
}

int main(int argc, char** argv)
{
    fs::init(argv[0]);
    GLFWwindow* window = create_window();
    vkw::Device device(window);
    vkw::Semaphore image_available(device), render_finished(device);
    vkw::Fence fence(device, true);
    vkw::ShaderFactory shader_factory(device);
    shader_factory.open(fs::file("/rs/shaders/duck.vert.spv"));
    shader_factory.open(fs::file("/rs/shaders/duck.frag.spv"));

    vkw::Allocator allocator(device, true);
    vkw::Image<2> depth_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, { device.swapchain().width(), device.swapchain().height(), 1 }, VK_FORMAT_D24_UNORM_S8_UINT, 1, 1, 1);
    vkw::ImageView<2> depth_buffer_view(device);
    depth_buffer_view.create(depth_buffer, VK_IMAGE_VIEW_TYPE_2D, depth_buffer.format(), VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    vkw::HostBuffer<1> duck_data(allocator, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fs::istream("/rs/Duck0.bin"), 102040);
    vkw::HostBuffer<2> uniform_buffer(allocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UniformBuffer));
    vkw::Buffer<1> vertex_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 76768);
    vkw::Buffer<1> index_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 25272);

    vkw::HostImage texture_data(allocator, vkw::HostImage::InputFormat::PNG, fs::istream("/rs/DuckCM.png"), true);
    vkw::Image<1> texture_image(texture_data, vkw::MemoryUsage::DeviceLocal, VK_IMAGE_USAGE_SAMPLED_BIT);
    vkw::ImageView<1> texture_image_view(device);
    vkw::Sampler texture_sampler(device);
    texture_image_view.create(texture_image, VK_IMAGE_VIEW_TYPE_2D, texture_image.format());
    texture_sampler.build(vkw::Sampler::Builder()
                              .with_texture_filtering(VK_FILTER_NEAREST, VK_FILTER_LINEAR)
                              .with_mipmap_filtering(VK_SAMPLER_MIPMAP_MODE_LINEAR)
                              .with_anisotropy(device.max_anisotropy())
                              .with_address_mode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT));

    vkw::RenderPass render_pass(device);
    vkw::Framebuffer framebuffer(device);
    vkw::RenderPass::Builder render_pass_builder;
    auto& color_att = render_pass_builder.add_attachment(device.swapchain().format(), 1)
                          .is_swapchain_image(true)
                          .with_color_operations(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
                          .final_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    auto& depth_att = render_pass_builder.add_attachment(VK_FORMAT_D24_UNORM_S8_UINT, 1)
                          .with_color_operations(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE)
                          .with_clear_depth(1.0f, 0)
                          .final_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    auto& subpass = render_pass_builder.add_subpass(VK_PIPELINE_BIND_POINT_GRAPHICS)
                        .with_color_attachment(color_att, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        .with_depth_attachment(depth_att, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    render_pass_builder.add_subpass_dependency(VK_SUBPASS_EXTERNAL, subpass)
        .stage_mask(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT)
        .access_mask(0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    render_pass.build(render_pass_builder);
    render_pass.create_framebuffers()
        .with_swapchain_attachment(0)
        .with_bound_attachment(depth_buffer_view)
        .build(framebuffer);
    device.on_recreate_swapchain([&](const vkw::Device& device, vkw::GarbageCollector& retirer) {
        VkExtent3D swapchain_extent = { device.swapchain().width(), device.swapchain().height(), 1 };
        retirer.add(depth_buffer);
        retirer.add(depth_buffer_view);
        retirer.add(framebuffer);

        depth_buffer.resize(swapchain_extent);
        depth_buffer_view.create(depth_buffer, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        render_pass.create_framebuffers()
            .with_swapchain_attachment(0)
            .with_bound_attachment(depth_buffer_view)
            .build(framebuffer);
    });

    vkw::PipelineFactory pipeline_factory(device, shader_factory);
    vkw::PipelineFactory::GraphicsPipelineSpecification pb({ "/rs/shaders/duck.vert.spv", "/rs/shaders/duck.frag.spv" });
    pb.set_vertex_input_binding(0, 12);
    pb.set_vertex_input_binding(1, 12);
    pb.set_vertex_input_binding(2, 8);
    pb.set_vertex_input_attribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
    pb.set_vertex_input_attribute(1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0);
    pb.set_vertex_input_attribute(2, 2, VK_FORMAT_R32G32_SFLOAT, 0);
    pb.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pb.set_depth_clamp(false);
    pb.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pb.set_cull_mode(VK_CULL_MODE_BACK_BIT);
    pb.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pb.set_depth_test(true, VK_COMPARE_OP_LESS);
    pb.set_depth_write(true);
    pb.set_attachment_color_blend_info(0, true, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    pb.set_attachment_alpha_blend_info(0, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    pb.set_render_pass(render_pass, 0);
    vkw::Pipeline& pipeline = pipeline_factory.get(pb);

    vkw::DescriptorPool descriptor_pool(device);
    vkw::DescriptorSet descriptor_set = descriptor_pool.allocate(pipeline.descriptor_set_layout(0));

    vkw::CommandPool command_pool(device, vkw::QueueFamilyType::Graphics, 1, 0);
    auto& cmd = command_pool.get(VK_COMMAND_BUFFER_LEVEL_PRIMARY, 0);
    cmd.begin(true);
    vertex_buffer.copy_from(duck_data, cmd, 0);
    index_buffer.copy_from(duck_data, cmd, 76768);
    texture_image.copy_from(texture_data, cmd);
    texture_image.set_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
    cmd.end();
    device.submit_commands()
        .add(cmd)
        .to_queue(vkw::QueueFamilyType::Graphics, 0);
    vkDeviceWaitIdle(device);

    while (glfwWindowShouldClose(window) == false) {
        glfwPollEvents();

        fence.wait();
        fence.reset();
        device.acquire_next_image(image_available);
        command_pool.reset(false);

        UniformBuffer ubuffer {};
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto now_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(now_time - start_time).count();
        ubuffer.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubuffer.view = glm::lookAt(glm::vec3(0.f, 250.f, 400.f), glm::vec3(0.0f, 100.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubuffer.proj = glm::perspective(glm::radians(45.f), device.swapchain().width() / static_cast<float>(device.swapchain().height()), 1.f, 10000.f);
        ubuffer.proj[1][1] *= -1;
        uniform_buffer.write_mapped(&ubuffer, sizeof(UniformBuffer));
        descriptor_set.bind_buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_buffer, 0, VK_WHOLE_SIZE);
        descriptor_set.bind_image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture_sampler);
        descriptor_set.update();

        auto& cbuffer = command_pool.get(VK_COMMAND_BUFFER_LEVEL_PRIMARY, 0);
        cbuffer.begin();
        cbuffer.begin_render_pass(render_pass, framebuffer, VK_SUBPASS_CONTENTS_INLINE);
        cbuffer.bind_pipeline(pipeline);
        cbuffer.set_viewport(0, 0, (float)device.swapchain().width(), (float)device.swapchain().height(), 0, 1);
        cbuffer.set_scissor(0, 0, device.swapchain().width(), device.swapchain().height());
        cbuffer.bind_vertex_buffer(0, vertex_buffer, 0);
        cbuffer.bind_vertex_buffer(1, vertex_buffer, 28788);
        cbuffer.bind_vertex_buffer(2, vertex_buffer, 57576);
        cbuffer.bind_index_buffer(index_buffer, 0, VK_INDEX_TYPE_UINT16);
        cbuffer.bind_descriptor_set(0, descriptor_set);
        vkCmdDrawIndexed(cbuffer, 12636, 1, 0, 0, 0);
        vkCmdEndRenderPass(cbuffer);
        cbuffer.end();

        device.submit_commands()
            .add(cbuffer)
            .wait_on(image_available, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
            .signal(render_finished)
            .to_queue(vkw::QueueFamilyType::Graphics, 0, fence);
        device.present_image({ render_finished });
    }

    vkDeviceWaitIdle(device);
}
