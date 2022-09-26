#include "spng.h"
#include "vkw/vkw.h"
#include <queue>
#include <spdlog/spdlog.h>

namespace vkw {

std::vector<VkDynamicState> GraphicsPipeline::Builder::s_dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
VkPipelineDynamicStateCreateInfo GraphicsPipeline::Builder::s_dynamic_state {
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    nullptr,
    0,
    static_cast<uint32_t>(s_dynamic_states.size()),
    s_dynamic_states.data(),
};
VkPipelineViewportStateCreateInfo GraphicsPipeline::Builder::s_viewport_state {
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr,
    0,
    1,
    &s_viewport_state_viewport,
    1,
    &s_viewport_state_scissor,
};

Fence::Fence(const Device& device, bool signaled)
    : m_device(device)
{
    VkResult res;
    VkFenceCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createinfo.flags = 0;
    if (signaled)
        createinfo.flags |= VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < 2; i++) {
        if ((res = vkCreateFence(m_device, &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateFence: {}", res);
            abort();
        }
    }
}

Fence::~Fence()
{
    vkDestroyFence(m_device, m_handle[0], nullptr);
    vkDestroyFence(m_device, m_handle[1], nullptr);
}

bool Fence::wait(uint64_t timeout) const
{
    const VkFence& current = m_handle[m_device.current_frame() % 2];
    VkResult res = vkWaitForFences(m_device, 1, &current, VK_TRUE, timeout);
    if (res == VK_SUCCESS)
        return true;
    else if (res == VK_TIMEOUT)
        return false;
    else {
        spdlog::critical("vkWaitForFences: {}", res);
        abort();
    }
}

void Fence::reset() const
{
    VkResult res;
    const VkFence& current = m_handle[m_device.current_frame() % 2];
    if ((res = vkResetFences(m_device, 1, &current)) != VK_SUCCESS) {
        spdlog::critical("vkResetFences: {}", res);
        abort();
    }
}

Semaphore::Semaphore(const Device& device)
    : m_device(device)
{
    VkResult res;
    VkSemaphoreCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (int i = 0; i < 2; i++) {
        if ((res = vkCreateSemaphore(m_device, &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateSemaphore: {}", res);
            abort();
        }
    }
}

Semaphore::~Semaphore()
{
    vkDestroySemaphore(m_device, m_handle[0], nullptr);
    vkDestroySemaphore(m_device, m_handle[1], nullptr);
}

DescriptorSetLayoutInfo::DescriptorSetLayoutInfo(const SpvReflectShaderModule& reflect, const SpvReflectDescriptorSet& r_set)
    : m_set_number(r_set.set)
    , m_bindings(r_set.binding_count)
{
    for (uint32_t i = 0; i < r_set.binding_count; i++) {
        const auto& r_binding = *(r_set.bindings[i]);
        m_bindings[i].binding = r_binding.binding;
        m_bindings[i].stageFlags = static_cast<VkShaderStageFlagBits>(reflect.shader_stage);
        m_bindings[i].descriptorType = static_cast<VkDescriptorType>(r_binding.descriptor_type);
        m_bindings[i].descriptorCount = 1;
        for (uint32_t j = 0; j < r_binding.array.dims_count; j++)
            m_bindings[i].descriptorCount *= r_binding.array.dims[j];
    }
}

const std::vector<VkDescriptorPoolSize> DescriptorPool::s_pool_size = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, DescriptorPool::POOL_SIZE },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DescriptorPool::POOL_SIZE * 8 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DescriptorPool::POOL_SIZE * 8 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DescriptorPool::POOL_SIZE * 2 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, DescriptorPool::POOL_SIZE * 2 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, DescriptorPool::POOL_SIZE * 2 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DescriptorPool::POOL_SIZE * 4 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DescriptorPool::POOL_SIZE * 4 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DescriptorPool::POOL_SIZE * 2 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, DescriptorPool::POOL_SIZE * 2 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, DescriptorPool::POOL_SIZE },
    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK, DescriptorPool::POOL_SIZE },
    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, DescriptorPool::POOL_SIZE },
};

DescriptorPool::DescriptorPool(const Device& device)
    : m_device(device)
{
    append_next_pool();
}

DescriptorPool::~DescriptorPool()
{
    for (VkDescriptorPool& pool : m_pools)
        vkDestroyDescriptorPool(m_device, pool, nullptr);
}

void DescriptorPool::append_next_pool()
{
    VkResult res;
    m_current = m_pools.emplace(m_pools.end());

    VkDescriptorPoolCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createinfo.maxSets = POOL_SIZE;
    createinfo.poolSizeCount = s_pool_size.size();
    createinfo.pPoolSizes = s_pool_size.data();
    if ((res = vkCreateDescriptorPool(m_device, &createinfo, nullptr, &*m_current)) != VK_SUCCESS) {
        spdlog::critical("vkCreateDescriptorPool: {}", res);
        abort();
    }
}

DescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout)
{
    VkResult res;
    std::array<VkDescriptorSetLayout, 2> layouts = { layout, layout };
    std::array<VkDescriptorSet, 2> out;
    VkDescriptorSetAllocateInfo allocinfo {};
    allocinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocinfo.pSetLayouts = layouts.data();
    allocinfo.descriptorPool = *m_current;
    allocinfo.descriptorSetCount = 2;
    if ((res = vkAllocateDescriptorSets(m_device, &allocinfo, out.data())) != VK_SUCCESS) {
        spdlog::critical("vkAllocateDescriptorSets: {}", res);
        abort();
    }
    return DescriptorSet(m_device, out);
}

void DescriptorPool::reset()
{
    for (VkDescriptorPool& pool : m_pools) {
        vkResetDescriptorPool(m_device, pool, 0);
    }
    m_current = m_pools.begin();
}

void DescriptorSet::bind_buffer(uint32_t binding, VkDescriptorType type, const Buffer<2>& buffer, VkDeviceSize offset, VkDeviceSize range)
{
    auto& buf = m_buffers.emplace_back();
    buf.buffer = buffer[m_device.current_frame() % 2];
    buf.offset = offset;
    buf.range = range;

    auto& write = m_writes.emplace_back();
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_handle[m_device.current_frame() % 2];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &buf;
}

void DescriptorSet::bind_image(uint32_t binding, VkDescriptorType type, const ImageView<1>& image, VkImageLayout layout, const Sampler& sampler)
{
    auto& img = m_images.emplace_back();
    img.imageView = image;
    img.imageLayout = layout;
    img.sampler = sampler;

    auto& write = m_writes.emplace_back();
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_handle[m_device.current_frame() % 2];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &img;
}

void DescriptorSet::update()
{
    vkUpdateDescriptorSets(m_device, m_writes.size(), m_writes.data(), 0, nullptr);
    m_writes.clear();
    m_buffers.clear();
    m_images.clear();
}

ShaderModule::ShaderModule(const Device& device)
    : m_device(device)
{
    m_createinfo.module = VK_NULL_HANDLE;
}

void ShaderModule::load_from(const void* spv, size_t len)
{
    VkResult res;
    SpvReflectResult rfs;
    VkShaderModuleCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createinfo.codeSize = len;
    createinfo.pCode = reinterpret_cast<const uint32_t*>(spv);

    if ((res = vkCreateShaderModule(m_device, &createinfo, nullptr, &m_createinfo.module)) != VK_SUCCESS) {
        spdlog::critical("vkCreateShaderModule: {}", res);
        abort();
    }

    SpvReflectShaderModule reflect;
    uint32_t n = 0;
    if ((rfs = spvReflectCreateShaderModule(len, spv, &reflect)) != SPV_REFLECT_RESULT_SUCCESS) {
        spdlog::critical("spvReflectCreateShaderModule: {}", rfs);
        abort();
    }

    m_createinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_createinfo.pName = "main";
    m_createinfo.stage = static_cast<VkShaderStageFlagBits>(reflect.shader_stage);
    if ((rfs = spvReflectEnumerateDescriptorSets(&reflect, &n, nullptr)) != SPV_REFLECT_RESULT_SUCCESS) {
        spdlog::critical("spvReflectEnumerateDescriptorSets: {}", rfs);
        abort();
    }
    std::vector<SpvReflectDescriptorSet*> descriptor_sets(n);
    if ((rfs = spvReflectEnumerateDescriptorSets(&reflect, &n, descriptor_sets.data())) != SPV_REFLECT_RESULT_SUCCESS) {
        spdlog::critical("spvReflectEnumerateDescriptorSets: {}", rfs);
        abort();
    }
    m_descriptor_set_layout_info.reserve(n);
    for (auto& p_set : descriptor_sets)
        m_descriptor_set_layout_info.emplace_back(reflect, *p_set);

    if ((rfs = spvReflectEnumeratePushConstantBlocks(&reflect, &n, nullptr)) != SPV_REFLECT_RESULT_SUCCESS) {
        spdlog::critical("spvReflectEnumeratePushConstantBlocks: {}", rfs);
        abort();
    }
    std::vector<SpvReflectBlockVariable*> push_constants(n);
    if ((rfs = spvReflectEnumeratePushConstantBlocks(&reflect, &n, push_constants.data())) != SPV_REFLECT_RESULT_SUCCESS) {
        spdlog::critical("spvReflectEnumeratePushConstantBlocks: {}", rfs);
        abort();
    }
    m_push_constants.resize(n);
    for (size_t i = 0; i < n; i++) {
        m_push_constants[i].stageFlags = static_cast<VkShaderStageFlagBits>(reflect.shader_stage);
        m_push_constants[i].size = push_constants[i]->size;
        m_push_constants[i].offset = push_constants[i]->offset;
    }
}

void ShaderModule::load_from(fs::istream&& is)
{
    std::vector<char> slurped(is.length());
    is.read(slurped.data(), slurped.size());
    load_from(slurped.data(), slurped.size());
}

ShaderModule::~ShaderModule()
{
    vkDestroyShaderModule(m_device, m_createinfo.module, nullptr);
}

RenderPass::RenderPass(const Device& device)
    : m_device(device)
    , m_handle(VK_NULL_HANDLE)
{
}

void RenderPass::build(RenderPass::Builder& builder)
{
    std::vector<VkAttachmentDescription> attachments(builder.m_attachments.size());
    std::vector<VkSubpassDescription> subpasses(builder.m_subpasses.size());
    std::vector<VkSubpassDependency> dependencies(builder.m_dependencies.size());
    std::vector<VkClearValue> clear_values(builder.m_attachments.size());
    std::vector<VkAttachmentReference> refs;
    for (size_t i = 0; i < attachments.size(); i++) {
        attachments[i] = builder.m_attachments[i].m_description;
        clear_values[i] = builder.m_attachments[i].m_clear_value;
    }
    for (size_t i = 0; i < dependencies.size(); i++)
        dependencies[i] = builder.m_dependencies[i].m_description;
    for (size_t i = 0; i < subpasses.size(); i++)
        builder.m_subpasses[i].bake(subpasses[i]);

    VkRenderPassCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createinfo.attachmentCount = attachments.size();
    createinfo.pAttachments = attachments.data();
    createinfo.subpassCount = subpasses.size();
    createinfo.pSubpasses = subpasses.data();
    createinfo.dependencyCount = dependencies.size();
    createinfo.pDependencies = dependencies.data();

    VkResult res = vkCreateRenderPass(m_device, &createinfo, nullptr, &m_handle);
    if (res == VK_SUCCESS) {
        m_clear_values = clear_values;
    } else {
        spdlog::critical("vkCreateRenderPass: {}", res);
        abort();
    }
}

RenderPass::~RenderPass()
{
    vkDestroyRenderPass(m_device, m_handle, nullptr);
}

RenderPass::Attachment::Attachment(size_t index, VkFormat format, int samples)
    : m_index(index)
    , m_is_swapchain_image(false)
    , m_clear_value()
    , m_description()
{
    m_description.format = format;
#ifdef NDEBUG
    m_description.samples = static_cast<VkSampleCountFlagBits>(samples);
#else
    if (0 < samples && samples <= 64) {
        if ((samples & (samples - 1)) == 0) { // is power of two
            m_description.samples = static_cast<VkSampleCountFlagBits>(samples);
        } else
            spdlog::error("RenderPass::Attachment(samples={}): invalid value", samples);
    } else
        spdlog::error("RenderPass::Attachment(samples={}): invalid value", samples);
#endif
    m_description.loadOp = m_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    m_description.storeOp = m_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    m_description.initialLayout = m_description.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

RenderPass::Attachment& RenderPass::Attachment::is_swapchain_image(bool id)
{
    m_is_swapchain_image = id;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::with_color_operations(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op)
{
    m_description.loadOp = load_op;
    m_description.storeOp = store_op;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::with_stencil_operations(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op)
{
    m_description.stencilLoadOp = load_op;
    m_description.stencilStoreOp = store_op;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::with_clear_color(float r, float g, float b, float a)
{
    m_clear_value.color.float32[0] = r;
    m_clear_value.color.float32[1] = g;
    m_clear_value.color.float32[2] = b;
    m_clear_value.color.float32[3] = a;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::with_clear_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    m_clear_value.color.uint32[0] = r;
    m_clear_value.color.uint32[1] = g;
    m_clear_value.color.uint32[2] = b;
    m_clear_value.color.uint32[3] = a;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::with_clear_depth(float depth, uint32_t stencil)
{
    m_clear_value.depthStencil.depth = depth;
    m_clear_value.depthStencil.stencil = stencil;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::initial_layout(VkImageLayout layout)
{
    m_description.initialLayout = layout;
    return *this;
}

RenderPass::Attachment& RenderPass::Attachment::final_layout(VkImageLayout layout)
{
    m_description.finalLayout = layout;
    return *this;
}

RenderPass::Subpass& RenderPass::Subpass::with_input_attachment(Attachment& attachment, VkImageLayout layout)
{
    VkAttachmentReference ref { static_cast<uint32_t>(attachment.index()), layout };
    m_input_attachments.push_back(ref);
    return *this;
}

RenderPass::Subpass& RenderPass::Subpass::with_color_attachment(Attachment& attachment, VkImageLayout layout)
{
    VkAttachmentReference ref { static_cast<uint32_t>(attachment.index()), layout };
    m_color_attachments.push_back(ref);
    return *this;
}

RenderPass::Subpass& RenderPass::Subpass::with_resolve_attachment(Attachment& attachment, VkImageLayout layout)
{
    VkAttachmentReference ref { static_cast<uint32_t>(attachment.index()), layout };
    m_resolve_attachments.push_back(ref);
    return *this;
}

RenderPass::Subpass& RenderPass::Subpass::with_depth_attachment(Attachment& attachment, VkImageLayout layout)
{
    m_has_depth_attachment = true;
    m_depth_attachment.attachment = attachment.index();
    m_depth_attachment.layout = layout;
    return *this;
}

RenderPass::Subpass& RenderPass::Subpass::preserve_attachment(Attachment& attachment)
{
    m_preserve_attachments.push_back(static_cast<uint32_t>(attachment.index()));
    return *this;
}

bool RenderPass::Subpass::bake(VkSubpassDescription& out)
{
    out.inputAttachmentCount = m_input_attachments.size();
    out.pInputAttachments = m_input_attachments.data();
    out.colorAttachmentCount = m_color_attachments.size();
    out.pColorAttachments = m_color_attachments.data();

    if (m_resolve_attachments.size() > 0) {
#ifndef NDEBUG
        if (m_resolve_attachments.size() != m_color_attachments.size()) {
            spdlog::error("RenderPassBuilder: subpass {}: number of color attachments ({}) must match number of resolve attachments({})", m_index, m_color_attachments.size(), m_resolve_attachments.size());
            return false;
        }
#endif
        out.pResolveAttachments = m_resolve_attachments.data();
    } else {
        out.pResolveAttachments = nullptr;
    }

    out.pDepthStencilAttachment = m_has_depth_attachment ? &m_depth_attachment : nullptr;
    out.preserveAttachmentCount = m_preserve_attachments.size();
    out.pPreserveAttachments = m_preserve_attachments.data();
    return true;
}

RenderPass::SubpassDependency::SubpassDependency(uint32_t src_index, uint32_t dst_index)
    : m_description()
{
    m_description.srcSubpass = src_index;
    m_description.dstSubpass = dst_index;
}

RenderPass::SubpassDependency& RenderPass::SubpassDependency::stage_mask(VkPipelineStageFlags src_mask, VkPipelineStageFlags dst_mask)
{
    m_description.srcStageMask = src_mask;
    m_description.dstStageMask = dst_mask;
    return *this;
}

RenderPass::SubpassDependency& RenderPass::SubpassDependency::access_mask(VkAccessFlags src_mask, VkAccessFlags dst_mask)
{
    m_description.srcAccessMask = src_mask;
    m_description.dstAccessMask = dst_mask;
    return *this;
}

RenderPass::Attachment& RenderPass::Builder::add_attachment(VkFormat format, int samples)
{
    Attachment att(m_attachments.size(), format, samples);
    m_attachments.push_back(att);
    return m_attachments.back();
}

RenderPass::Subpass& RenderPass::Builder::add_subpass(VkPipelineBindPoint pipeline_bind_point)
{
    Subpass sp(m_subpasses.size(), pipeline_bind_point);
    m_subpasses.push_back(sp);
    return m_subpasses.back();
}

RenderPass::SubpassDependency& RenderPass::Builder::add_subpass_dependency(RenderPass::Subpass& src, RenderPass::Subpass& dst)
{
    SubpassDependency sd(src.index(), dst.index());
    m_dependencies.push_back(sd);
    return m_dependencies.back();
}

RenderPass::SubpassDependency& RenderPass::Builder::add_subpass_dependency(uint32_t src_index, RenderPass::Subpass& dst)
{
#ifndef NDEBUG
    if (src_index != VK_SUBPASS_EXTERNAL)
        spdlog::warn("RenderPass::Builder::add_subpass_dependency: when neither subpass is VK_SUBPASS_EXTERNAL, please pass in subpass references");
#endif

    SubpassDependency sd(src_index, dst.index());
    m_dependencies.push_back(sd);
    return m_dependencies.back();
}

RenderPass::SubpassDependency& RenderPass::Builder::add_subpass_dependency(RenderPass::Subpass& src, uint32_t dst_index)
{
#ifndef NDEBUG
    if (dst_index != VK_SUBPASS_EXTERNAL)
        spdlog::warn("RenderPass::Builder::add_subpass_dependency: when neither subpass is VK_SUBPASS_EXTERNAL, please pass in subpass references");
#endif

    SubpassDependency sd(src.index(), dst_index);
    m_dependencies.push_back(sd);
    return m_dependencies.back();
}

Pipeline::~Pipeline()
{
    if (m_handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_handle, nullptr);
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        for (auto& layout : m_descriptor_set_layout)
            vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
    }
}

void Pipeline::set_layouts(const std::array<std::vector<VkDescriptorSetLayoutBinding>, DESCRIPTOR_SET_COUNT>& descriptor_layout_bindings,
    const std::vector<VkPushConstantRange>& push_constants)
{
    VkResult res;
    for (int i = 0; i < DESCRIPTOR_SET_COUNT; i++) {
        VkDescriptorSetLayoutCreateInfo ds_layout_createinfo {};
        ds_layout_createinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ds_layout_createinfo.pNext = nullptr;
        ds_layout_createinfo.flags = 0;
        ds_layout_createinfo.bindingCount = descriptor_layout_bindings[i].size();
        ds_layout_createinfo.pBindings = descriptor_layout_bindings[i].data();
        if ((res = vkCreateDescriptorSetLayout(m_device, &ds_layout_createinfo, nullptr, &m_descriptor_set_layout[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateDescriptorSetLayout: {}", res);
            abort();
        }
    }

    VkPipelineLayoutCreateInfo layout_createinfo {};
    layout_createinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_createinfo.setLayoutCount = DESCRIPTOR_SET_COUNT;
    layout_createinfo.pSetLayouts = m_descriptor_set_layout.data();
    layout_createinfo.pushConstantRangeCount = push_constants.size();
    layout_createinfo.pPushConstantRanges = push_constants.data();
    if ((res = vkCreatePipelineLayout(m_device, &layout_createinfo, nullptr, &m_layout)) != VK_SUCCESS) {
        spdlog::critical("vkCreatePipelineLayout: {}", res);
        abort();
    }
}

Pipeline::Builder::~Builder()
{
}

void Pipeline::Builder::add_shader(const ShaderModule& shader)
{
    m_shaders.emplace_back(shader);
    m_push_constants.insert(m_push_constants.end(), shader.push_constants().begin(), shader.push_constants().end());
    for (auto& info : shader.descriptor_set_layout_info()) {
        auto& bindings = m_descriptor_layout_bindings[info.set()];
        bindings.insert(bindings.end(), info.bindings().begin(), info.bindings().end());
    }
}

void Pipeline::Builder::clear_shaders()
{
    m_shaders.clear();
    m_push_constants.clear();
    for (auto& bindings : m_descriptor_layout_bindings)
        bindings.clear();
}

GraphicsPipeline::Builder::Builder(const Device& device)
    : m_device(device)
    , m_subpass(VK_NULL_HANDLE, 0)
{
    m_vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    m_input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_tessellation_state.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    m_rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    m_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    m_rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    m_rasterization_state.lineWidth = 1.f;

    m_multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    m_color_blend_state.logicOpEnable = false;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::append_derivative()
{
    return m_derivatives.emplace_back(*this);
}

bool GraphicsPipeline::Builder::has_vertex_shader() const
{
    return std::any_of(m_shaders.begin(), m_shaders.end(), [](const VkPipelineShaderStageCreateInfo& shader) {
        return shader.stage == VK_SHADER_STAGE_VERTEX_BIT;
    });
}

void GraphicsPipeline::Builder::add_vertex_input_attribute(uint32_t binding, uint32_t location, VkFormat format, size_t offset)
{
    for (auto it = m_vertex_input_attributes.begin(); it != m_vertex_input_attributes.end(); ++it) {
        if (it->location == location) {
            it->binding = binding;
            it->format = format;
            it->offset = offset;
            return;
        }
    }

    VkVertexInputAttributeDescription attr;
    attr.location = location;
    attr.binding = binding;
    attr.format = format;
    attr.offset = static_cast<uint32_t>(offset);
    m_vertex_input_attributes.push_back(attr);
}

void GraphicsPipeline::Builder::add_vertex_input_binding(uint32_t binding, size_t stride, bool by_instance)
{
#ifndef NDEBUG
    if (stride & 0x3)
        spdlog::warn("GraphicsPipeline::Builder::add_vertex_input_binding: stride ({}) should be a multiple of 4", stride);
#endif
    if (binding >= m_vertex_input_bindings.size())
        m_vertex_input_bindings.resize(binding + 1);
    m_vertex_input_bindings[binding].binding = binding;
    m_vertex_input_bindings[binding].stride = stride;
    m_vertex_input_bindings[binding].inputRate = by_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
}

void GraphicsPipeline::Builder::set_primitive_topology(VkPrimitiveTopology topology, bool enable_restart)
{
    m_input_assembly_state.topology = topology;
    m_input_assembly_state.primitiveRestartEnable = enable_restart;
}

void GraphicsPipeline::Builder::set_depth_bias(bool enable, float constant_factor, float clamp, float slope_factor)
{
    m_rasterization_state.depthBiasEnable = enable;
    m_rasterization_state.depthBiasConstantFactor = constant_factor;
    m_rasterization_state.depthBiasClamp = clamp;
    m_rasterization_state.depthBiasSlopeFactor = slope_factor;
}

void GraphicsPipeline::Builder::set_multisample_samples(int samples)
{
#ifdef NDEBUG
    m_multisample_state.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
#else
    if (0 < samples && samples <= 64) {
        if ((samples & (samples - 1)) == 0) { // is power of two
            m_multisample_state.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
        } else
            spdlog::error("GraphicsPipeline::Builder::set_multisample_samples({}): invalid value", samples);
    } else
        spdlog::error("GraphicsPipeline::Builder::set_multisample_samples({}): invalid value", samples);
#endif
}

void GraphicsPipeline::Builder::set_sample_shading(bool enable, float min_fraction)
{
    if (enable) {
        m_multisample_state.sampleShadingEnable = true;
        m_multisample_state.minSampleShading = min_fraction;
    } else {
        m_multisample_state.sampleShadingEnable = false;
    }
}

void GraphicsPipeline::Builder::set_depth_test(bool enable, VkCompareOp compare_op)
{
    m_depth_stencil_state.depthTestEnable = enable;
    m_depth_stencil_state.depthCompareOp = compare_op;
}

void GraphicsPipeline::Builder::set_depth_write(bool enable)
{
#ifndef NDEBUG
    if (enable && m_depth_stencil_state.depthTestEnable == false) {
        spdlog::warn("GraphicsPipeline::Builder::set_depth_write: depth test is currently disabled; this will have no effect");
    }
#endif
    m_depth_stencil_state.depthWriteEnable = enable;
}

void GraphicsPipeline::Builder::set_depth_bounds_test(bool enable, float min, float max)
{
    m_depth_stencil_state.depthBoundsTestEnable = enable;
    m_depth_stencil_state.minDepthBounds = min;
    m_depth_stencil_state.maxDepthBounds = max;
}

void GraphicsPipeline::Builder::set_stencil_test(bool enable)
{
    m_depth_stencil_state.stencilTestEnable = enable;
}

void GraphicsPipeline::Builder::set_stencil_test_parameters(bool front_face, VkCompareOp compare_op, VkStencilOp pass_op, VkStencilOp fail_op, VkStencilOp depth_fail_op,
    uint32_t compare_mask, uint32_t write_mask, uint32_t ref_value)
{
    auto& params = front_face ? m_depth_stencil_state.front : m_depth_stencil_state.back;
    params.failOp = fail_op;
    params.passOp = pass_op;
    params.compareOp = compare_op;
    params.depthFailOp = depth_fail_op;
    params.compareMask = compare_mask;
    params.writeMask = write_mask;
    params.reference = ref_value;
}

void GraphicsPipeline::Builder::set_attachment_color_blend_info(size_t index, bool enabled, VkBlendOp blend_op, VkBlendFactor src_factor, VkBlendFactor dst_factor, VkColorComponentFlags color_write_mask)
{
    if (index >= m_color_blend_attachments.size())
        m_color_blend_attachments.resize(index + 1);

    auto& att = m_color_blend_attachments[index];
    att.blendEnable = enabled;
    att.colorBlendOp = blend_op;
    att.srcColorBlendFactor = src_factor;
    att.dstColorBlendFactor = dst_factor;
    if (att.colorWriteMask & VK_COLOR_COMPONENT_A_BIT)
        att.colorWriteMask = color_write_mask | VK_COLOR_COMPONENT_A_BIT;
    else
        att.colorWriteMask = color_write_mask;
}

void GraphicsPipeline::Builder::set_attachment_alpha_blend_info(size_t index, VkBlendOp blend_op, VkBlendFactor src_factor, VkBlendFactor dst_factor, bool write_alpha)
{
    if (index >= m_color_blend_attachments.size())
        m_color_blend_attachments.resize(index + 1);

    auto& att = m_color_blend_attachments[index];
    att.alphaBlendOp = blend_op;
    att.srcAlphaBlendFactor = src_factor;
    att.dstAlphaBlendFactor = dst_factor;
    if (write_alpha)
        att.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    else
        att.colorWriteMask &= (~VK_COLOR_COMPONENT_A_BIT);
}

void GraphicsPipeline::Builder::set_color_blend_constants(float r, float g, float b, float a)
{
    m_color_blend_state.blendConstants[0] = r;
    m_color_blend_state.blendConstants[1] = g;
    m_color_blend_state.blendConstants[2] = b;
    m_color_blend_state.blendConstants[3] = a;
}

void GraphicsPipeline::Builder::assign_to_subpass(const RenderPass& pass, size_t subpass)
{
    m_subpass = std::make_pair(VkRenderPass(pass), static_cast<uint32_t>(subpass));
}

std::vector<GraphicsPipeline> GraphicsPipeline::Builder::build()
{
    VkResult res;
    size_t current_index = 0;
    std::vector<GraphicsPipeline> out_pipelines;
    std::vector<VkGraphicsPipelineCreateInfo> create_infos;
    std::queue<std::pair<GraphicsPipeline::Builder*, int32_t>> builders;
    builders.push(std::make_pair(this, -1));

    while (builders.empty() == false) {
        GraphicsPipeline::Builder* b = builders.front().first;
        out_pipelines.push_back(std::move(GraphicsPipeline(m_device)));
        create_infos.emplace_back();

        b->m_vertex_input_state.vertexAttributeDescriptionCount = b->m_vertex_input_attributes.size();
        b->m_vertex_input_state.vertexBindingDescriptionCount = b->m_vertex_input_bindings.size();
        b->m_vertex_input_state.pVertexAttributeDescriptions = b->m_vertex_input_attributes.data();
        b->m_vertex_input_state.pVertexBindingDescriptions = b->m_vertex_input_bindings.data();
        b->m_color_blend_state.attachmentCount = b->m_color_blend_attachments.size();
        b->m_color_blend_state.pAttachments = b->m_color_blend_attachments.data();

        GraphicsPipeline& out = out_pipelines.back();
        out.set_layouts(m_descriptor_layout_bindings, m_push_constants);

        VkGraphicsPipelineCreateInfo& createinfo = create_infos.back();
        createinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createinfo.stageCount = b->m_shaders.size();
        createinfo.pStages = b->m_shaders.data();
        createinfo.pVertexInputState = &b->m_vertex_input_state;
        createinfo.pInputAssemblyState = &b->m_input_assembly_state;
        createinfo.pTessellationState = &b->m_tessellation_state;
        createinfo.pViewportState = &b->s_viewport_state;
        createinfo.pRasterizationState = &b->m_rasterization_state;
        createinfo.pMultisampleState = &b->m_multisample_state;
        createinfo.pDepthStencilState = &b->m_depth_stencil_state;
        createinfo.pColorBlendState = &b->m_color_blend_state;
        createinfo.pDynamicState = &b->s_dynamic_state;
        createinfo.layout = out.m_layout;
        createinfo.renderPass = b->m_subpass.first;
        createinfo.subpass = b->m_subpass.second;
        createinfo.basePipelineHandle = VK_NULL_HANDLE;
        createinfo.basePipelineIndex = builders.front().second;

        builders.pop();
        for (auto& child : b->m_derivatives) {
            builders.push(std::make_pair(&child, current_index));
        }
        current_index++;
    }

    std::vector<VkPipeline> out_handles(create_infos.size());
    if ((res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, create_infos.size(), create_infos.data(), nullptr, out_handles.data())) != VK_SUCCESS) {
        spdlog::critical("vkCreateGraphicsPipelines: {}", res);
        abort();
    }
    for (int i = 0; i < out_handles.size(); i++)
        out_pipelines[i].m_handle = out_handles[i];

    return out_pipelines;
}

Sampler::Builder::Builder()
    : m_createinfo({})
{
    m_createinfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    m_createinfo.anisotropyEnable = VK_FALSE;
    m_createinfo.compareEnable = VK_FALSE;
    m_createinfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    m_createinfo.unnormalizedCoordinates = VK_FALSE;
}
Sampler::Builder& Sampler::Builder::with_texture_filtering(VkFilter min_filter, VkFilter mag_filter)
{
    m_createinfo.minFilter = min_filter;
    m_createinfo.magFilter = mag_filter;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_mipmap_filtering(VkSamplerMipmapMode mode)
{
    m_createinfo.mipmapMode = mode;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_address_mode(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w)
{
    m_createinfo.addressModeU = u;
    m_createinfo.addressModeV = v;
    m_createinfo.addressModeW = w;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_anisotropy(bool enable, float ratio)
{
    m_createinfo.anisotropyEnable = enable;
    m_createinfo.maxAnisotropy = ratio;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_compare(bool enable, VkCompareOp operation)
{
    m_createinfo.compareEnable = enable;
    m_createinfo.compareOp = operation;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_lod_bounds(float min_lod, float max_lod, float lod_bias)
{
    m_createinfo.minLod = min_lod;
    m_createinfo.maxLod = max_lod;
    m_createinfo.mipLodBias = lod_bias;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_border_color(VkBorderColor border_color)
{
    m_createinfo.borderColor = border_color;
    return *this;
}
Sampler::Builder& Sampler::Builder::with_coordinate_normalization(bool enable)
{
    m_createinfo.unnormalizedCoordinates = !enable;
    return *this;
}

void Sampler::build(const Sampler::Builder& builder)
{
    VkResult res;
    if ((res = vkCreateSampler(m_device, &builder.m_createinfo, nullptr, &m_handle)) != VK_SUCCESS) {
        spdlog::critical("vkCreateSampler: {}", res);
        abort();
    }
}

Sampler::~Sampler()
{
    vkDestroySampler(m_device, m_handle, nullptr);
}

void Framebuffer::initialize(VkFramebufferCreateInfo& ci_template, std::array<std::vector<VkImageView>, 2>& attachments)
{
    VkResult res;
    VkFramebufferCreateInfo createinfo = ci_template;

    m_extent = { ci_template.width, ci_template.height };
    m_handles.resize(2);
    for (int i = 0; i < 2; i++) {
        createinfo.pAttachments = attachments[i].data();
        if ((res = vkCreateFramebuffer(m_device, &createinfo, nullptr, &m_handles[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateFramebuffer: {}", res);
            abort();
        }
    }
}

void Framebuffer::initialize(VkFramebufferCreateInfo& ci_template, std::array<std::vector<VkImageView>, 2>& attachments, int32_t swapchain_attachment_index)
{
    VkResult res;
    const size_t image_count = m_device.swapchain().image_count();
    VkFramebufferCreateInfo createinfo = ci_template;

    m_extent = { ci_template.width, ci_template.height };
    if (swapchain_attachment_index == 0 && attachments[0].size() == 1 && attachments[1].size() == 1) {
        m_handles.resize(m_device.swapchain().image_count());
        for (int i = 0; i < image_count; i++) {
            createinfo.pAttachments = &m_device.swapchain().image_view(i);
            if ((res = vkCreateFramebuffer(m_device, &createinfo, nullptr, &m_handles[i])) != VK_SUCCESS) {
                spdlog::critical("vkCreateFramebuffer(swapchain_image={}): {}", i, res);
                abort();
            }
        }
    } else {
        m_handles.resize(2 * m_device.swapchain().image_count());
        for (int f = 0; f < 2; f++) {
            createinfo.pAttachments = attachments[f].data();
            for (int i = 0; i < image_count; i++) {
                attachments[f][swapchain_attachment_index] = m_device.swapchain().image_view(i);
                if ((res = vkCreateFramebuffer(m_device, &createinfo, nullptr, &m_handles[i + image_count * f])) != VK_SUCCESS) {
                    spdlog::critical("vkCreateFramebuffer(frame={}, swapchain_image={}): {}", f, i, res);
                    abort();
                }
            }
        }
    }
}

Framebuffer::~Framebuffer()
{
    for (auto& fb : m_handles) {
        vkDestroyFramebuffer(m_device, fb, nullptr);
    }
}

Framebuffer::Builder::Builder(const RenderPass& render_pass)
    : m_attachment_counter(0)
    , m_swapchain_attachment_index(-1)
    , m_createinfo()
    , m_attachments { { std::vector<VkImageView>(render_pass.attachment_count()), std::vector<VkImageView>(render_pass.attachment_count()) } }
{
    m_createinfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    m_createinfo.renderPass = render_pass;
    m_createinfo.attachmentCount = render_pass.attachment_count();
    m_createinfo.width = m_createinfo.height = 0;
    m_createinfo.layers = 1;
}

Framebuffer::Builder& Framebuffer::Builder::with_dimensions(uint32_t width, uint32_t height)
{
    m_createinfo.width = width;
    m_createinfo.height = height;
    return *this;
}

Framebuffer::Builder& Framebuffer::Builder::with_layers(uint32_t layers)
{
    m_createinfo.layers = layers;
    return *this;
}

Framebuffer::Builder& Framebuffer::Builder::with_swapchain_attachment()
{
    return with_swapchain_attachment(m_attachment_counter);
}
Framebuffer::Builder& Framebuffer::Builder::with_swapchain_attachment(uint32_t index)
{
    m_swapchain_attachment_index = index;
    m_attachments[0][index] = VK_NULL_HANDLE;
    m_attachments[1][index] = VK_NULL_HANDLE;
    m_attachment_counter++;
    return *this;
}

template <>
Framebuffer::Builder& Framebuffer::Builder::with_bound_attachment(const vkw::ImageView<1>& image)
{
    return with_bound_attachment(m_attachment_counter++, image, image);
}
template <>
Framebuffer::Builder& Framebuffer::Builder::with_bound_attachment(const vkw::ImageView<2>& image)
{
    return with_bound_attachment(m_attachment_counter++, image[0], image[1]);
}
Framebuffer::Builder& Framebuffer::Builder::with_bound_attachment(uint32_t index, VkImageView a, VkImageView b)
{
    m_attachments[0][index] = a;
    m_attachments[1][index] = b;
    return *this;
}

void Framebuffer::Builder::build(Framebuffer& out)
{
    if (m_swapchain_attachment_index == -1) {
        if (m_createinfo.width == 0 || m_createinfo.height == 0) {
            spdlog::critical("Framebuffer::Builder::build: width/height must be set, or one of the attachments must be a swapchain image");
            abort();
        }
        out.initialize(m_createinfo, m_attachments);
    } else {
        m_createinfo.width = out.m_device.swapchain().width();
        m_createinfo.height = out.m_device.swapchain().height();
        out.initialize(m_createinfo, m_attachments, m_swapchain_attachment_index);
    }
}

}
