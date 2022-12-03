#include "config.h"
#include "vkw/vkw.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#define GLM_FORCE_RADIANS
#include "fs.h"
#include "scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <spdlog/spdlog.h>

static inline void glfw_error_callback(int code, const char* description)
{
    spdlog::error("[glfw] {}: {}", code, description);
}

class DuckMesh : public scene::Mesh {
public:
    vkw::HostBuffer<1> host_buffer;
    vkw::Buffer<1> vertex_buffer, index_buffer;

    DuckMesh(vkw::Allocator& allocator)
        : host_buffer(allocator, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fs::istream("/rs/Duck0.bin"), 102040)
        , vertex_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 76768)
        , index_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 25272)
    {
    }

    void initialize_buffers(vkw::CommandBuffer& cmd)
    {
        vertex_buffer.copy_from(host_buffer, cmd, 0);
        index_buffer.copy_from(host_buffer, cmd, 76768);
    }

    void cleanup_initialize_buffers()
    {
        host_buffer.destroy();
    }

    void draw(vkw::CommandBuffer& cbuffer) const
    {
        cbuffer.bind_vertex_buffer(0, vertex_buffer, 0);
        cbuffer.bind_vertex_buffer(1, vertex_buffer, 28788);
        cbuffer.bind_vertex_buffer(2, vertex_buffer, 57576);
        cbuffer.bind_index_buffer(index_buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cbuffer, 12636, 1, 0, 0, 0);
    }
};

class DuckMaterial : public scene::Material {
public:
    vkw::HostImage texture_data;
    vkw::Image<1> texture_image;
    vkw::ImageView<1> texture_image_view;
    vkw::Sampler& sampler;

    DuckMaterial(vkw::Allocator& allocator, vkw::Sampler& sampler, vkw::DescriptorSet&& d)
        : Material(std::move(d))
        , texture_data(allocator, vkw::HostImage::InputFormat::PNG, fs::istream("/rs/DuckCM.png"), true)
        , texture_image(allocator, texture_data, vkw::MemoryUsage::DeviceLocal, VK_IMAGE_USAGE_SAMPLED_BIT)
        , texture_image_view(allocator.device(), texture_image, VK_IMAGE_VIEW_TYPE_2D, texture_image.format())
        , sampler(sampler)
    {
    }

    void initialize_buffers(vkw::CommandBuffer& cmd)
    {
        texture_image.copy_from(texture_data, cmd);
        texture_image.set_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
    }

    void cleanup_initialize_buffers()
    {
        texture_data.destroy();

        m_descriptor_set.bind_image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
        m_descriptor_set.update();
    }
};

class CoolVisitor : public scene::SceneVisitor {
    vkw::CommandBuffer* m_cmd = nullptr;

public:
    void set_command_info(vkw::CommandBuffer& cmd)
    {
        m_cmd = &cmd;
    }

    virtual void visitGeometry(scene::Geometry& geometry)
    {
        m_cmd->bind_descriptor_set(3, geometry.material().descriptor_set());
        m_cmd->push_constants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &current_matrix());
        geometry.mesh().draw(*m_cmd);
    }
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
    vkw::Shader vert = shader_factory.open(fs::file("/rs/shaders/duck.vert.spv"), VK_SHADER_STAGE_VERTEX_BIT),
                frag = shader_factory.open(fs::file("/rs/shaders/duck.frag.spv"), VK_SHADER_STAGE_FRAGMENT_BIT);

    vkw::Allocator allocator(device, true);
    vkw::Image<2> depth_buffer(allocator, vkw::MemoryUsage::DeviceLocal, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, { device.swapchain().width(), device.swapchain().height(), 1 }, VK_FORMAT_D24_UNORM_S8_UINT, 1, 1, 1);
    vkw::ImageView<2> depth_buffer_view(device);
    depth_buffer_view.create(depth_buffer, VK_IMAGE_VIEW_TYPE_2D, depth_buffer.format(), VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    vkw::Sampler texture_sampler(device);
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
    vkw::PipelineLayout pipeline_layout = vkw::PipelineLayout::build()
                                              .with_descriptor_binding(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
                                              .with_descriptor_binding(3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                                              .with_push_constant_range(0, sizeof(glm::mat4), VK_SHADER_STAGE_VERTEX_BIT)
                                              .build(device);

    vkw::PipelineFactory::GraphicsPipelineSpecification pb({ vert, frag }, pipeline_layout);
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

    vkw::CommandPool command_pool(device, vkw::QueueFamilyType::Graphics, 1, 0);
    vkw::DescriptorPool descriptor_pool(device);
    vkw::DescriptorSet descriptor_set_global = descriptor_pool.allocate(pipeline_layout.descriptor_set_layout(0)),
                       descriptor_set_perpass = descriptor_pool.allocate(pipeline_layout.descriptor_set_layout(1)),
                       descriptor_set_perobject = descriptor_pool.allocate(pipeline_layout.descriptor_set_layout(2)),
                       descriptor_set_duckmaterial = descriptor_pool.allocate(pipeline_layout.descriptor_set_layout(3));

    DuckMesh duck(allocator);
    DuckMaterial duck_material(allocator, texture_sampler, std::move(descriptor_set_duckmaterial));
    vkw::HostBuffer<2> uniform_buffer(allocator, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 2 * sizeof(glm::mat4)); // probably this is gonna get split up

    Scene main_scene;
    scene::Rotation n1(main_scene.root());
    scene::Geometry n2(&n1, duck, duck_material);
    CoolVisitor visitor;

    auto& cmd = command_pool.get(VK_COMMAND_BUFFER_LEVEL_PRIMARY, 0);
    cmd.begin(true);
    duck.initialize_buffers(cmd);
    duck_material.initialize_buffers(cmd);
    cmd.end();
    device.submit_commands()
        .add(cmd)
        .to_queue(vkw::QueueFamilyType::Graphics, 0);
    vkDeviceWaitIdle(device);
    duck.cleanup_initialize_buffers();
    duck_material.cleanup_initialize_buffers();

    while (glfwWindowShouldClose(window) == false) {
        glfwPollEvents();

        fence.wait();
        fence.reset();
        device.acquire_next_image(image_available);
        command_pool.reset(false);

        static auto start_time = std::chrono::high_resolution_clock::now();
        auto now_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(now_time - start_time).count();
        n1.set_rotation(glm::angleAxis(glm::radians(90.f) * time, glm::vec3(0.f, 1.f, 0.f)));

        glm::mat4 mvp[2];
        mvp[0] = glm::perspective(glm::radians(45.f), device.swapchain().width() / static_cast<float>(device.swapchain().height()), 1.f, 1000.f);
        mvp[1] = glm::lookAt(glm::vec3(0.f, 250.f, 400.f), glm::vec3(0.0f, 100.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        mvp[0][1][1] *= -1;
        uniform_buffer.write_mapped(&mvp, 2 * sizeof(glm::mat4));
        descriptor_set_perpass.bind_buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_buffer, 0, 2 * sizeof(glm::mat4));
        descriptor_set_perpass.update();

        auto& cbuffer = command_pool.get(VK_COMMAND_BUFFER_LEVEL_PRIMARY, 0);
        cbuffer.begin();
        cbuffer.begin_render_pass(render_pass, framebuffer, VK_SUBPASS_CONTENTS_INLINE);
        cbuffer.set_viewport(0, 0, device.swapchain().width(), device.swapchain().height(), 0, 1);
        cbuffer.set_scissor(0, 0, device.swapchain().width(), device.swapchain().height());
        cbuffer.bind_pipeline(pipeline);
        cbuffer.bind_descriptor_set(1, descriptor_set_perpass);
        visitor.set_command_info(cbuffer);
        visitor.visit(main_scene);
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
