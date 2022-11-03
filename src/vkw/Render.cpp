#include "spng.h"
#include "vkw/vkw.h"
#include <queue>
#include <spdlog/spdlog.h>

namespace vkw {

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

ShaderModule::ShaderModule(VkDevice device, const void* spv, size_t len)
{
    VkResult res;
    SpvReflectResult rfs;
    VkShaderModuleCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createinfo.codeSize = len;
    createinfo.pCode = reinterpret_cast<const uint32_t*>(spv);

    if ((res = vkCreateShaderModule(device, &createinfo, nullptr, &m_createinfo.module)) != VK_SUCCESS) {
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

ShaderModule::ShaderModule(const ShaderModule& base, VkSpecializationInfo* specialization)
    : m_createinfo(base.m_createinfo)
    , m_descriptor_set_layout_info(base.m_descriptor_set_layout_info)
    , m_push_constants(base.m_push_constants)
{
    m_createinfo.pSpecializationInfo = specialization;
}

void ShaderModule::destroy(VkDevice device)
{
    vkDestroyShaderModule(device, m_createinfo.module, nullptr);
}

ShaderFactory::shader_specialization_data::shader_specialization_data(const void* specialization, size_t size, std::vector<VkSpecializationMapEntry>&& index)
    : entries(index)
{
    void* local_data = malloc(size);
    memcpy(local_data, specialization, size);

    info.mapEntryCount = entries.size();
    info.pMapEntries = entries.data();
    info.dataSize = size;
    info.pData = local_data;
}

ShaderFactory::shader_specialization_data::~shader_specialization_data()
{
    free(const_cast<void*>(info.pData));
}

ShaderFactory::ShaderFactory(const Device& device)
    : m_device(device)
{
}

ShaderFactory::~ShaderFactory()
{
    clear(true);
}

void ShaderFactory::clear(bool all)
{
    m_specialized.clear();
    m_specialized_data.clear();
    if (all) {
        for (auto& it : m_cache) {
            it.second.destroy(m_device);
        }
        m_cache.clear();
    }
}

ShaderModule& ShaderFactory::open(const fs::file& path)
{
    fs::istream is(path);
    std::vector<char> slurped(is.length());
    is.read(slurped.data(), slurped.size());

    std::unordered_map<std::string, ShaderModule>::iterator location;
    std::tie(location, std::ignore) = m_cache.emplace(path.path(), ShaderModule(m_device, slurped.data(), slurped.size()));
    return location->second;
}

ShaderModule& ShaderFactory::specialize(const std::string& path, const void* specialization, size_t size, std::vector<VkSpecializationMapEntry>&& index)
{
    auto& spec_data = m_specialized_data.emplace_back(specialization, size, std::move(index));
    return m_specialized.emplace_back(ShaderModule(get(path), &spec_data.info));
}

const ShaderModule& ShaderFactory::get(const std::string& path) const
{
    return m_cache.at(path);
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

void Pipeline::set_layouts(uint32_t layout_count, const std::vector<VkDescriptorSetLayoutBinding>* descriptor_layout_bindings,
    const std::vector<VkPushConstantRange>& push_constants)
{
    VkResult res;

    m_descriptor_set_layout.resize(layout_count);
    for (int i = 0; i < m_descriptor_set_layout.size(); i++) {
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
    layout_createinfo.setLayoutCount = layout_count;
    layout_createinfo.pSetLayouts = m_descriptor_set_layout.data();
    layout_createinfo.pushConstantRangeCount = push_constants.size();
    layout_createinfo.pPushConstantRanges = push_constants.data();
    if ((res = vkCreatePipelineLayout(m_device, &layout_createinfo, nullptr, &m_layout)) != VK_SUCCESS) {
        spdlog::critical("vkCreatePipelineLayout: {}", res);
        abort();
    }
}

std::string PipelineFactory::s_cache_path = "/pref/pipelinecache";
std::vector<VkDynamicState> PipelineFactory::s_graphics_dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
VkPipelineDynamicStateCreateInfo PipelineFactory::s_graphics_dynamic_state = {
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    nullptr,
    0,
    static_cast<uint32_t>(s_graphics_dynamic_states.size()),
    s_graphics_dynamic_states.data(),
};
VkPipelineViewportStateCreateInfo PipelineFactory::s_viewport_state {
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr,
    0,
    1,
    &s_viewport_state_viewport,
    1,
    &s_viewport_state_scissor,
};
PipelineFactory::PipelineFactory(const Device& device, const ShaderFactory& shaders, size_t bucket_count)
    : m_device(device)
    , m_shaders(shaders)
    , m_persistent_cache(VK_NULL_HANDLE)
    , m_bucket_count(bucket_count)
    , m_compute(bucket_count)
    , m_graphics(bucket_count)
{
    fs::file cache_file(s_cache_path);
    std::vector<char> cache_data;
    if (cache_file.exists()) {
        fs::istream cache_stream(cache_file);
        cache_data.resize(cache_stream.length());
        cache_stream.read(cache_data.data(), cache_data.size());
    }

    VkResult res;
    VkPipelineCacheCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createinfo.initialDataSize = cache_data.size();
    createinfo.pInitialData = cache_data.data();
    if ((res = vkCreatePipelineCache(m_device, &createinfo, nullptr, &m_persistent_cache)) != VK_SUCCESS) {
        spdlog::critical("vkCreatePipelineCache: {}", res);
        abort();
    }
}

PipelineFactory::~PipelineFactory()
{
    m_compute.clear();
    m_graphics.clear();
    vkDestroyPipelineCache(m_device, m_persistent_cache, nullptr);
}

void PipelineFactory::write_cache() const
{
    VkResult res;
    std::vector<char> cache_data;
    size_t cache_size = 0;
    if ((res = vkGetPipelineCacheData(m_device, m_persistent_cache, &cache_size, nullptr)) != VK_SUCCESS) {
        spdlog::critical("vkGetPipelineCacheData(size): {}", res);
        abort();
    }
    cache_data.resize(cache_size);
    if ((res = vkGetPipelineCacheData(m_device, m_persistent_cache, &cache_size, cache_data.data())) != VK_SUCCESS) {
        spdlog::critical("vkGetPipelineCacheData: {}", res);
        abort();
    }

    fs::ostream cw(s_cache_path);
    cw.write(cache_data.data(), cache_size);
}

size_t PipelineFactory::spec_bucket(const std::vector<std::string>& shaders)
{
    size_t h = 0;
    for (const std::string& s : shaders) {
        h = (h << 1) ^ std::hash<std::string>()(s);
    }
    return h % m_bucket_count;
}

Pipeline& PipelineFactory::get(const ComputePipelineSpecification& in_spec)
{
    auto& candidates = m_compute[spec_bucket(in_spec.m_shaders)];
    for (auto& c : candidates) {
        if (c.second == in_spec)
            return c.first;
    }

    std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptor_layout_bindings(m_device.limits().maxBoundDescriptorSets);
    uint32_t max_descriptor_set = 0;
    auto& new_slot = candidates.emplace_back(Pipeline(m_device, VK_PIPELINE_BIND_POINT_COMPUTE), in_spec);
    auto& shader = m_shaders.get(in_spec.m_shaders.front());
    for (auto& info : shader.descriptor_set_layout_info()) {
        max_descriptor_set = std::max(max_descriptor_set, info.set());
        auto& bindings = descriptor_layout_bindings[info.set()];
        bindings.insert(bindings.end(), info.bindings().begin(), info.bindings().end());
    }
    new_slot.first.set_layouts(max_descriptor_set + 1, descriptor_layout_bindings.data(), shader.push_constants());

    VkResult res;
    VkComputePipelineCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createinfo.stage = shader;
    createinfo.layout = new_slot.first.layout();
    if ((res = vkCreateComputePipelines(m_device, m_persistent_cache, 1, &createinfo, nullptr, &new_slot.first.m_handle)) != VK_SUCCESS) {
        spdlog::critical("vkCreateComputePipelines: {}", res);
        abort();
    }
    return new_slot.first;
}

Pipeline& PipelineFactory::get(const GraphicsPipelineSpecification& in_spec)
{
    const auto& shader_names = in_spec.m_shaders;
    auto& candidates = m_graphics[spec_bucket(in_spec.m_shaders)];
    for (auto& c : candidates) {
        if (c.second == in_spec)
            return c.first;
    }

    std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptor_layout_bindings(m_device.limits().maxBoundDescriptorSets);
    std::vector<VkPushConstantRange> push_constant_ranges;
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    uint32_t max_descriptor_set = 0;
    auto& new_slot = candidates.emplace_back(Pipeline(m_device, VK_PIPELINE_BIND_POINT_GRAPHICS), in_spec);
    for (const auto& shader_name : shader_names) {
        auto& shader = m_shaders.get(shader_name);
        for (auto& info : shader.descriptor_set_layout_info()) {
            max_descriptor_set = std::max(max_descriptor_set, info.set());
            auto& bindings = descriptor_layout_bindings[info.set()];
            bindings.insert(bindings.end(), info.bindings().begin(), info.bindings().end());
        }
        push_constant_ranges.insert(push_constant_ranges.end(), shader.push_constants().begin(), shader.push_constants().end());
        shader_stages.push_back(shader);
    }
    new_slot.first.set_layouts(max_descriptor_set + 1, descriptor_layout_bindings.data(), push_constant_ranges);

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = in_spec.m_vertex_input_bindings.size();
    vertex_input_state.pVertexBindingDescriptions = in_spec.m_vertex_input_bindings.data();
    vertex_input_state.vertexAttributeDescriptionCount = in_spec.m_vertex_input_attributes.size();
    vertex_input_state.pVertexAttributeDescriptions = in_spec.m_vertex_input_attributes.data();
    VkPipelineColorBlendStateCreateInfo color_blend_state;
    memcpy(&color_blend_state, &in_spec.m_pod.color_blend_state, sizeof(VkPipelineColorBlendStateCreateInfo));
    color_blend_state.attachmentCount = in_spec.m_color_blend_attachments.size();
    color_blend_state.pAttachments = in_spec.m_color_blend_attachments.data();

    VkResult res;
    VkGraphicsPipelineCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    createinfo.stageCount = shader_stages.size();
    createinfo.pStages = shader_stages.data();
    createinfo.pVertexInputState = &vertex_input_state;
    createinfo.pInputAssemblyState = &in_spec.m_pod.input_assembly_state;
    createinfo.pTessellationState = &in_spec.m_pod.tessellation_state;
    createinfo.pViewportState = &s_viewport_state;
    createinfo.pRasterizationState = &in_spec.m_pod.rasterization_state;
    createinfo.pMultisampleState = &in_spec.m_pod.multisample_state;
    createinfo.pDepthStencilState = &in_spec.m_pod.depth_stencil_state;
    createinfo.pColorBlendState = &color_blend_state;
    createinfo.pDynamicState = &s_graphics_dynamic_state;
    createinfo.layout = new_slot.first.layout();
    createinfo.renderPass = in_spec.m_pod.render_pass;
    createinfo.subpass = in_spec.m_pod.subpass_index;
    if ((res = vkCreateGraphicsPipelines(m_device, m_persistent_cache, 1, &createinfo, nullptr, &new_slot.first.m_handle)) != VK_SUCCESS) {
        spdlog::critical("vkCreateGraphicsPipelines: {}", res);
        abort();
    }
    return new_slot.first;
}

bool PipelineFactory::ComputePipelineSpecification::operator==(const ComputePipelineSpecification& other) const
{
    return m_shaders == other.m_shaders;
}

PipelineFactory::GraphicsPipelineSpecification::GraphicsPipelineSpecification(std::vector<std::string>&& shaders)
    : m_shaders(shaders)
{
    memset(&m_pod, 0, sizeof(m_pod));
    m_pod.input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_pod.tessellation_state.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    m_pod.rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_pod.rasterization_state.lineWidth = 1.f;
    m_pod.multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_pod.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_pod.depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_pod.color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
}
bool PipelineFactory::GraphicsPipelineSpecification::operator==(const GraphicsPipelineSpecification& other) const
{
    if (m_shaders != other.m_shaders)
        return false;
    if (memcmp(&m_pod, &other.m_pod, sizeof(m_pod)) != 0)
        return false;
    if (m_vertex_input_attributes.size() != other.m_vertex_input_attributes.size())
        return false;
    if (memcmp(m_vertex_input_attributes.data(), other.m_vertex_input_attributes.data(), m_vertex_input_attributes.size() * sizeof(VkVertexInputAttributeDescription)) != 0)
        return false;
    if (m_vertex_input_bindings.size() != other.m_vertex_input_bindings.size())
        return false;
    if (memcmp(m_vertex_input_bindings.data(), other.m_vertex_input_bindings.data(), m_vertex_input_bindings.size() * sizeof(VkVertexInputBindingDescription)) != 0)
        return false;
    if (m_color_blend_attachments.size() != other.m_color_blend_attachments.size())
        return false;
    if (memcmp(m_color_blend_attachments.data(), other.m_color_blend_attachments.data(), m_color_blend_attachments.size() * sizeof(VkPipelineColorBlendAttachmentState)) != 0)
        return false;
    return true;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_vertex_input_attribute(uint32_t location, uint32_t binding, VkFormat format, size_t offset)
{
    VkVertexInputAttributeDescription& attr = m_vertex_input_attributes.emplace_back();
    attr.location = location;
    attr.binding = binding;
    attr.format = format;
    attr.offset = offset;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_vertex_input_binding(uint32_t binding, size_t stride, bool by_instance)
{
    VkVertexInputBindingDescription& s = m_vertex_input_bindings.emplace_back();
    s.binding = binding;
    s.stride = stride;
    s.inputRate = by_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_primitive_topology(VkPrimitiveTopology topology, bool enable_restart)
{
    m_pod.input_assembly_state.topology = topology;
    m_pod.input_assembly_state.primitiveRestartEnable = enable_restart;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_tessellation_patch_control_points(uint32_t n_points)
{
    m_pod.tessellation_state.patchControlPoints = n_points;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_depth_clamp(bool enable)
{
    m_pod.rasterization_state.depthClampEnable = enable;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_rasterizer_discard(bool enable)
{
    m_pod.rasterization_state.rasterizerDiscardEnable = enable;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_polygon_mode(VkPolygonMode polygon_mode)
{
    m_pod.rasterization_state.polygonMode = polygon_mode;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_cull_mode(VkCullModeFlagBits cull_mode)
{
    m_pod.rasterization_state.cullMode = cull_mode;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_front_face(VkFrontFace front_face)
{
    m_pod.rasterization_state.frontFace = front_face;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_depth_bias(bool enable, float constant_factor, float clamp, float slope_factor)
{
    m_pod.rasterization_state.depthBiasEnable = enable;
    m_pod.rasterization_state.depthBiasConstantFactor = constant_factor;
    m_pod.rasterization_state.depthBiasClamp = clamp;
    m_pod.rasterization_state.depthBiasSlopeFactor = slope_factor;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_multisample_samples(int samples)
{
    m_pod.multisample_state.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_sample_shading(bool enable, float min_fraction)
{
    if (enable) {
        m_pod.multisample_state.sampleShadingEnable = true;
        m_pod.multisample_state.minSampleShading = min_fraction;
    } else {
        m_pod.multisample_state.sampleShadingEnable = false;
    }
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_depth_test(bool enable, VkCompareOp compare_op)
{
    m_pod.depth_stencil_state.depthTestEnable = enable;
    m_pod.depth_stencil_state.depthCompareOp = compare_op;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_depth_write(bool enable)
{
    m_pod.depth_stencil_state.depthWriteEnable = enable;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_depth_bounds_test(bool enable, float min, float max)
{

    m_pod.depth_stencil_state.depthBoundsTestEnable = enable;
    m_pod.depth_stencil_state.minDepthBounds = min;
    m_pod.depth_stencil_state.maxDepthBounds = max;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_stencil_test(bool enable)
{
    m_pod.depth_stencil_state.stencilTestEnable = enable;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_stencil_test_parameters(bool front_face, VkCompareOp compare_op,
    VkStencilOp pass_op, VkStencilOp fail_op, VkStencilOp depth_fail_op, uint32_t compare_mask, uint32_t write_mask, uint32_t ref_value)
{
    auto& params = front_face ? m_pod.depth_stencil_state.front : m_pod.depth_stencil_state.back;
    params.failOp = fail_op;
    params.passOp = pass_op;
    params.compareOp = compare_op;
    params.depthFailOp = depth_fail_op;
    params.compareMask = compare_mask;
    params.writeMask = write_mask;
    params.reference = ref_value;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_attachment_color_blend_info(size_t index, bool enabled,
    VkBlendOp blend_op, VkBlendFactor src_factor, VkBlendFactor dst_factor, VkColorComponentFlags color_write_mask)
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
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_attachment_alpha_blend_info(size_t index, VkBlendOp blend_op,
    VkBlendFactor src_factor, VkBlendFactor dst_factor, bool write_alpha)
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
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_color_blend_constants(float r, float g, float b, float a)
{
    m_pod.color_blend_state.blendConstants[0] = r;
    m_pod.color_blend_state.blendConstants[1] = g;
    m_pod.color_blend_state.blendConstants[2] = b;
    m_pod.color_blend_state.blendConstants[3] = a;
    return *this;
}
PipelineFactory::GraphicsPipelineSpecification& PipelineFactory::GraphicsPipelineSpecification::set_render_pass(VkRenderPass render_pass, uint32_t subpass_index)
{
    m_pod.render_pass = render_pass;
    m_pod.subpass_index = subpass_index;
    return *this;
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
Sampler::Builder& Sampler::Builder::with_anisotropy(float ratio)
{
    m_createinfo.anisotropyEnable = ratio > 0;
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
