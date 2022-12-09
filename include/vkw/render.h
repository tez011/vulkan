#pragma once
#include "Allocator.h"
#include "Device.h"
#include "resource.h"
#include <array>
#include <list>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace fs {
class file;
}

namespace vkw {
template <unsigned int N>
class Buffer;
class CommandBuffer;

static constexpr size_t DESCRIPTOR_SET_COUNT = 4;

class Fence {
private:
    const Device& m_device;
    std::array<VkFence, 2> m_handle;

public:
    Fence(const Device&, bool signaled = false);
    ~Fence();

    inline operator VkFence() const { return m_handle[m_device.current_frame() % 2]; }
    bool wait(uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;
    void reset() const;
};

class Semaphore {
private:
    const Device& m_device;
    std::array<VkSemaphore, 2> m_handle;

public:
    Semaphore(const Device&);
    ~Semaphore();

    inline operator VkSemaphore() const { return m_handle[m_device.current_frame() % 2]; }
};

class Sampler {
private:
    VkDevice m_device;
    VkSampler m_handle;

public:
    class Builder {
        friend class Sampler;

    private:
        VkSamplerCreateInfo m_createinfo;

    public:
        Builder();
        Builder& with_texture_filtering(VkFilter min_filter, VkFilter mag_filter);
        Builder& with_mipmap_filtering(VkSamplerMipmapMode mode);
        Builder& with_address_mode(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);
        Builder& with_anisotropy(float ratio);
        Builder& with_compare(bool enable, VkCompareOp operation);
        Builder& with_lod_bounds(float min_lod, float max_lod, float lod_bias = 0);
        Builder& with_border_color(VkBorderColor border_color);
        Builder& with_coordinate_normalization(bool enable);
    };

    Sampler(const Device& device)
        : m_device(device)
        , m_handle(VK_NULL_HANDLE)
    {
    }
    void build(const Builder&);
    ~Sampler();

    inline operator VkSampler() const { return m_handle; }
};

class DescriptorSet {
private:
    const Device& m_device;
    VkSampler m_active_sampler;
    std::array<VkDescriptorSet, 2> m_handle;

    std::vector<VkWriteDescriptorSet> m_writes;
    std::vector<VkDescriptorBufferInfo> m_buffers;
    std::vector<VkDescriptorImageInfo> m_images;

    friend class DescriptorPool;

    DescriptorSet(const Device& device, VkDescriptorSet h0, VkDescriptorSet h1)
        : m_device(device)
        , m_active_sampler(VK_NULL_HANDLE)
        , m_handle({ h0, h1 })
    {
    }

public:
    DescriptorSet(DescriptorSet&&) = default;
    inline operator VkDescriptorSet() const { return m_handle[m_device.current_frame() % 2]; }

    void bind_buffer(uint32_t binding, VkDescriptorType type, const Buffer<1>& buffer, VkDeviceSize offset, VkDeviceSize range = VK_WHOLE_SIZE);
    void bind_buffer(uint32_t binding, VkDescriptorType type, const Buffer<2>& buffer, VkDeviceSize offset, VkDeviceSize range = VK_WHOLE_SIZE);
    void bind_image(uint32_t binding, VkDescriptorType type, const ImageView<1>& image, VkImageLayout layout, VkSampler sampler = VK_NULL_HANDLE);
    inline void bind_image_sampler(VkSampler sampler) { m_active_sampler = sampler; }
    void update();
};

class Pipeline;

class DescriptorPool {
private:
    constexpr static uint32_t POOL_SIZE = 64;
    static const std::vector<VkDescriptorPoolSize> s_pool_size;

    const Device& m_device;
    std::list<VkDescriptorPool> m_pools;
    std::list<VkDescriptorPool>::iterator m_current;

    void append_next_pool();

public:
    DescriptorPool(const Device&);
    ~DescriptorPool();

    DescriptorSet allocate(VkDescriptorSetLayout layout);
    void reset();
};

// TODO: Fold into Device class?
using Shader = size_t;
class ShaderFactory {
private:
    struct module {
        VkPipelineShaderStageCreateInfo m_createinfo {};

        module(VkDevice device, const void* spv, size_t len, VkShaderStageFlagBits stage, VkSpecializationInfo* specialization);
        module(const module&) = default;

        inline const VkPipelineShaderStageCreateInfo& stage() const { return m_createinfo; }
    };
    struct specialization_data {
        VkSpecializationInfo info;
        std::unique_ptr<char[]> specdata;
        std::vector<VkSpecializationMapEntry> entries;

        specialization_data(const void* specialization, size_t size, std::vector<VkSpecializationMapEntry>&& index);
    };

    VkDevice m_device;
    std::unordered_map<std::string, Shader> m_cache;
    std::vector<specialization_data> m_specialization_data;
    std::vector<module> m_shaders;

public:
    using Handle = size_t;

    ShaderFactory(const Device& device)
        : m_device(device)
    {
    }
    ShaderFactory(const ShaderFactory&) = delete;
    ~ShaderFactory();

    Shader open(const fs::file& path, VkShaderStageFlagBits stage);
    Shader open(const fs::file& path, VkShaderStageFlagBits stage, const void* specialization, size_t size, std::vector<VkSpecializationMapEntry>&& index);
    inline const module& get(Shader s) const { return m_shaders.at(s); }
};

class RenderPass;

class Framebuffer {
private:
    Device& m_device;
    VkExtent2D m_extent;
    std::vector<VkFramebuffer> m_handles;

    void initialize(VkFramebufferCreateInfo&, std::array<std::vector<VkImageView>, 2>& attachments);
    void initialize(VkFramebufferCreateInfo&, std::array<std::vector<VkImageView>, 2>& attachments, int32_t swapchain_attachment_index);

    friend class GarbageCollector;

public:
    Framebuffer(Device& device)
        : m_device(device)
        , m_extent({ device.swapchain().width(), device.swapchain().height() })
        , m_handles(0)
    {
    }
    Framebuffer(const Framebuffer&) = delete;
    ~Framebuffer();

    inline uint32_t width() const { return m_extent.width; }
    inline uint32_t height() const { return m_extent.height; }
    inline operator VkFramebuffer() const
    {
        if (m_handles.size() == 2)
            return m_handles[m_device.current_frame() % 2];
        else if (m_handles.size() == m_device.swapchain().image_count())
            return m_handles[m_device.current_frame_image()];
        else
            return m_handles[(m_device.current_frame() % 2) * m_device.swapchain().image_count() + m_device.current_frame_image()];
    }

    class Builder {
    private:
        uint32_t m_attachment_counter;
        int32_t m_swapchain_attachment_index;
        VkFramebufferCreateInfo m_createinfo;
        std::array<std::vector<VkImageView>, 2> m_attachments;

    public:
        Builder(const RenderPass&);
        Builder& with_dimensions(uint32_t width, uint32_t height);
        Builder& with_layers(uint32_t layers);
        Builder& with_swapchain_attachment();
        Builder& with_swapchain_attachment(uint32_t index);
        Builder& with_bound_attachment(uint32_t index, VkImageView, VkImageView);
        template <unsigned int N>
        Builder& with_bound_attachment(const vkw::ImageView<N>& image);

        void build(Framebuffer& out);
    };
};

class RenderPass {
public:
    class Attachment;
    class Builder;

private:
    const Device& m_device;
    VkRenderPass m_handle;
    std::vector<VkClearValue> m_clear_values;

public:
    RenderPass(const Device&);
    RenderPass(const RenderPass&) = delete;
    void build(Builder&);
    ~RenderPass();

    inline size_t attachment_count() const { return m_clear_values.size(); }
    inline const std::vector<VkClearValue>& clear_values() const { return m_clear_values; }
    inline operator VkRenderPass() const { return m_handle; }
    Framebuffer::Builder create_framebuffers() const { return Framebuffer::Builder(*this); }

    class Attachment {
        friend class RenderPass;

    private:
        size_t m_index;
        bool m_is_swapchain_image;
        VkClearValue m_clear_value;
        VkAttachmentDescription m_description;

        Attachment(size_t index, VkFormat format, int samples);

    public:
        size_t index() const { return m_index; }
        Attachment& is_swapchain_image(bool id);
        Attachment& with_color_operations(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op);
        Attachment& with_stencil_operations(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op);
        Attachment& with_clear_color(float r, float g, float b, float a);
        Attachment& with_clear_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
        Attachment& with_clear_depth(float depth, uint32_t stencil);
        Attachment& initial_layout(VkImageLayout layout);
        Attachment& final_layout(VkImageLayout layout);
    };

    class Subpass {
        friend class RenderPass;

    private:
        size_t m_index;
        std::vector<VkAttachmentReference> m_input_attachments, m_color_attachments, m_resolve_attachments;
        bool m_has_depth_attachment;
        VkAttachmentReference m_depth_attachment;
        std::vector<uint32_t> m_preserve_attachments;

        Subpass(size_t index, VkPipelineBindPoint bp)
            : m_index(index)
            , m_has_depth_attachment(false)
            , m_depth_attachment()
        {
        }
        bool bake(VkSubpassDescription& out);

    public:
        size_t index() const { return m_index; }
        Subpass& with_input_attachment(Attachment&, VkImageLayout layout);
        Subpass& with_color_attachment(Attachment&, VkImageLayout layout);
        Subpass& with_resolve_attachment(Attachment&, VkImageLayout layout);
        Subpass& with_depth_attachment(Attachment&, VkImageLayout layout);
        Subpass& preserve_attachment(Attachment&);
    };

    class SubpassDependency {
        friend class RenderPass;

    private:
        VkSubpassDependency m_description;
        SubpassDependency(uint32_t src_index, uint32_t dst_index);

    public:
        SubpassDependency& stage_mask(VkPipelineStageFlags src_mask, VkPipelineStageFlags dst_mask);
        SubpassDependency& access_mask(VkAccessFlags src_mask, VkAccessFlags dst_mask);
    };

    class Builder {
        friend class RenderPass;

    private:
        std::deque<Attachment> m_attachments;
        std::deque<Subpass> m_subpasses;
        std::vector<SubpassDependency> m_dependencies;

    public:
        Builder() { }
        Builder(const Builder&) = default;

        Attachment& add_attachment(VkFormat format, int samples);
        Subpass& add_subpass(VkPipelineBindPoint pipeline_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS);
        SubpassDependency& add_subpass_dependency(Subpass& src, Subpass& dst);
        SubpassDependency& add_subpass_dependency(uint32_t src, Subpass& dst);
        SubpassDependency& add_subpass_dependency(Subpass& src, uint32_t dst);
    };
};

class PipelineLayout {
private:
    constexpr static size_t DESCRIPTOR_SET_COUNT = 4;
    VkDevice m_device;
    VkPipelineLayout m_layout;
    std::array<VkDescriptorSetLayout, DESCRIPTOR_SET_COUNT> m_descriptor_set_layouts;

    class Builder {
    private:
        std::array<std::vector<VkDescriptorSetLayoutBinding>, DESCRIPTOR_SET_COUNT> m_bindings;
        std::vector<VkPushConstantRange> m_push_constants;
        std::vector<std::vector<VkSampler>> m_immutable_samplers;

    public:
        Builder& with_descriptor_binding(uint32_t set, uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stage = VK_SHADER_STAGE_ALL, std::initializer_list<VkSampler> immutable_samplers = {});
        Builder& with_push_constant_range(uint32_t offset, uint32_t size, VkShaderStageFlags stage);
        PipelineLayout build(const Device&);
    };

    PipelineLayout(const Device& device, VkPipelineLayout layout, std::array<VkDescriptorSetLayout, DESCRIPTOR_SET_COUNT>&& descriptor_set_layouts);

public:
    ~PipelineLayout();
    inline VkDescriptorSetLayout descriptor_set_layout(size_t i) const { return m_descriptor_set_layouts[i]; }
    inline operator VkPipelineLayout() const { return m_layout; }

    static Builder build() { return Builder(); }
};

class Pipeline {
private:
    VkDevice m_device;
    VkPipeline m_handle;
    VkPipelineLayout m_layout;
    VkPipelineBindPoint m_bind_point;

public:
    Pipeline(VkDevice device, VkPipelineBindPoint bind_point, VkPipeline handle, VkPipelineLayout layout)
        : m_device(device)
        , m_handle(handle)
        , m_layout(layout)
        , m_bind_point(bind_point)
    {
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = default;
    ~Pipeline();
    inline VkPipelineBindPoint bind_point() const { return m_bind_point; }
    inline VkPipelineLayout layout() const { return m_layout; }
    inline operator VkPipeline() const { return m_handle; }
};

class PipelineFactory {
public:
    class ComputePipelineSpecification {
    private:
        std::vector<Shader> m_shaders;
        VkPipelineLayout m_layout;

        friend class PipelineFactory;

    public:
        ComputePipelineSpecification(std::vector<Shader>&& shaders, VkPipelineLayout layout)
            : m_shaders(shaders)
            , m_layout(layout)
        {
        }

        bool operator==(const ComputePipelineSpecification& other) const;
    };

    class GraphicsPipelineSpecification {
    private:
        std::vector<Shader> m_shaders;
        VkPipelineLayout m_layout;
        struct {
            VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
            VkPipelineTessellationStateCreateInfo tessellation_state;
            VkPipelineRasterizationStateCreateInfo rasterization_state;
            VkPipelineMultisampleStateCreateInfo multisample_state;
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
            VkPipelineColorBlendStateCreateInfo color_blend_state;
            VkRenderPass render_pass;
            uint32_t subpass_index;
        } m_pod;

        std::vector<VkVertexInputAttributeDescription> m_vertex_input_attributes;
        std::vector<VkVertexInputBindingDescription> m_vertex_input_bindings;
        std::vector<VkPipelineColorBlendAttachmentState> m_color_blend_attachments;

        GraphicsPipelineSpecification& set_stencil_test_parameters(bool front_face, VkCompareOp compare_op,
            VkStencilOp pass_op, VkStencilOp fail_op, VkStencilOp depth_fail_op,
            uint32_t compare_mask, uint32_t write_mask, uint32_t ref_value);

        friend class PipelineFactory;

    public:
        GraphicsPipelineSpecification(std::vector<Shader>&& shaders, VkPipelineLayout layout);
        bool operator==(const GraphicsPipelineSpecification& other) const;

        GraphicsPipelineSpecification& set_vertex_input_attribute(uint32_t location, uint32_t binding, VkFormat format, size_t offset);
        GraphicsPipelineSpecification& set_vertex_input_binding(uint32_t binding, size_t stride, bool by_instance = false);
        GraphicsPipelineSpecification& set_primitive_topology(VkPrimitiveTopology topology, bool enable_restart = false);
        GraphicsPipelineSpecification& set_tessellation_patch_control_points(uint32_t n_points);
        GraphicsPipelineSpecification& set_depth_clamp(bool enable);
        GraphicsPipelineSpecification& set_rasterizer_discard(bool enable);
        GraphicsPipelineSpecification& set_polygon_mode(VkPolygonMode polygon_mode);
        GraphicsPipelineSpecification& set_cull_mode(VkCullModeFlagBits cull_mode);
        GraphicsPipelineSpecification& set_front_face(VkFrontFace front_face);
        GraphicsPipelineSpecification& set_depth_bias(bool enable, float constant_factor, float clamp, float slope_factor);
        GraphicsPipelineSpecification& set_multisample_samples(int samples);
        GraphicsPipelineSpecification& set_sample_shading(bool enable, float min_fraction = 1.f);
        GraphicsPipelineSpecification& set_depth_test(bool enable, VkCompareOp compare_op = VK_COMPARE_OP_NEVER);
        GraphicsPipelineSpecification& set_depth_write(bool enable);
        GraphicsPipelineSpecification& set_depth_bounds_test(bool enable, float min = 0.f, float max = 0.f);
        GraphicsPipelineSpecification& set_stencil_test(bool enable);
        inline GraphicsPipelineSpecification& set_stencil_test_front_face_parameters(VkCompareOp compare_op, VkStencilOp pass_op, VkStencilOp fail_op, VkStencilOp depth_fail_op,
            uint32_t compare_mask, uint32_t write_mask, uint32_t ref_value)
        {
            return set_stencil_test_parameters(true, compare_op, pass_op, fail_op, depth_fail_op, compare_mask, write_mask, ref_value);
        }
        inline GraphicsPipelineSpecification& set_stencil_test_back_face_parameters(VkCompareOp compare_op, VkStencilOp pass_op, VkStencilOp fail_op, VkStencilOp depth_fail_op,
            uint32_t compare_mask, uint32_t write_mask, uint32_t ref_value)
        {
            return set_stencil_test_parameters(false, compare_op, pass_op, fail_op, depth_fail_op, compare_mask, write_mask, ref_value);
        }
        GraphicsPipelineSpecification& set_attachment_color_blend_info(size_t index, bool enabled,
            VkBlendOp blend_op = VK_BLEND_OP_ADD,
            VkBlendFactor src_factor = VK_BLEND_FACTOR_ZERO,
            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO,
            VkColorComponentFlags color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
        GraphicsPipelineSpecification& set_attachment_alpha_blend_info(size_t index, VkBlendOp blend_op = VK_BLEND_OP_ADD,
            VkBlendFactor src_factor = VK_BLEND_FACTOR_ZERO,
            VkBlendFactor dst_factor = VK_BLEND_FACTOR_ZERO,
            bool write_alpha = true);
        GraphicsPipelineSpecification& set_color_blend_constants(float r, float g, float b, float a);
        GraphicsPipelineSpecification& set_render_pass(VkRenderPass render_pass, uint32_t subpass_index);
    };

private:
    static std::string s_cache_path;
    static std::vector<VkDynamicState> s_graphics_dynamic_states;
    static VkPipelineDynamicStateCreateInfo s_graphics_dynamic_state;
    static VkPipelineViewportStateCreateInfo s_viewport_state;
    static constexpr VkViewport s_viewport_state_viewport {};
    static constexpr VkRect2D s_viewport_state_scissor {};

    const Device& m_device;
    const ShaderFactory& m_shaders;
    VkPipelineCache m_persistent_cache;

    size_t m_bucket_count;
    std::vector<Pipeline> m_compute, m_graphics;
    std::vector<ComputePipelineSpecification> m_compute_specs;
    std::vector<GraphicsPipelineSpecification> m_graphics_specs;

    size_t spec_bucket(const std::vector<Shader>& shaders);

public:
    PipelineFactory(const Device&, const ShaderFactory&, size_t bucket_count = 16);
    PipelineFactory(const PipelineFactory&) = delete;
    ~PipelineFactory();

    void write_cache() const;
    Pipeline& get(const ComputePipelineSpecification&);
    Pipeline& get(const GraphicsPipelineSpecification&);
};

class CommandPool {
private:
    const Device& m_device;
    std::array<VkCommandPool, 2> m_handle;
    std::array<std::vector<CommandBuffer>, 4> m_buffers;

    friend class CommandBuffer; // for m_device field

public:
    CommandPool(const Device&, QueueFamilyType, size_t primary, size_t secondary, bool transient = false);
    ~CommandPool();

    void trim();
    void reset(bool release_resources = true);
    CommandBuffer& get(VkCommandBufferLevel level, size_t index);
};

class CommandBuffer {
private:
    VkCommandBuffer m_handle;
    VkCommandBufferLevel m_level;

    VkPipelineBindPoint m_bound_pipeline_bind_point;
    VkPipelineLayout m_bound_pipeline_layout;

    friend class CommandPool;

    CommandBuffer(VkCommandBuffer buffer, VkCommandBufferLevel level)
        : m_handle(buffer)
        , m_level(level)
        , m_bound_pipeline_bind_point(VK_PIPELINE_BIND_POINT_MAX_ENUM)
        , m_bound_pipeline_layout(VK_NULL_HANDLE)
    {
    }

    void begin(const RenderPass* render_pass, size_t subpass, VkFramebuffer framebuffer, bool one_time_submit);

public:
    inline operator VkCommandBuffer() const { return m_handle; }
    void begin(bool one_time_submit = false)
    {
        begin(nullptr, 0, VK_NULL_HANDLE, one_time_submit);
    }
    inline void begin(const RenderPass& render_pass, size_t subpass, bool one_time_submit = false)
    {
        begin(&render_pass, subpass, VK_NULL_HANDLE, one_time_submit);
    }
    inline void begin(const RenderPass& render_pass, size_t subpass, const Framebuffer& framebuffer, bool one_time_submit = false)
    {
        begin(&render_pass, subpass, framebuffer, one_time_submit);
    }

    void begin_render_pass(const RenderPass& render_pass, const Framebuffer& framebuffer, int32_t x, int32_t y, uint32_t w, uint32_t h, VkSubpassContents contents);
    void begin_render_pass(const RenderPass& render_pass, const Framebuffer& framebuffer, VkSubpassContents contents)
    {
        begin_render_pass(render_pass, framebuffer, 0, 0, framebuffer.width(), framebuffer.height(), contents);
    }
    void bind_descriptor_set(uint32_t set_number, VkDescriptorSet handle);
    void bind_index_buffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type);
    void bind_pipeline(const Pipeline& pipeline);
    void push_constants(VkShaderStageFlags stage, uint32_t offset, uint32_t size, const void* data);
    void bind_vertex_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset);
    void set_image_layout(VkImage image, VkImageLayout from, VkImageLayout to, VkImageSubresourceRange& subresource, VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    void set_viewport(float x, float y, float width, float height, float min_depth, float max_depth);
    void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    void end();
};

}
