#include "vkw/Render.h"
#include <spdlog/spdlog.h>

namespace vkw {

CommandPool::CommandPool(const Device& device, QueueFamilyType ty, size_t primary, size_t secondary, bool transient)
    : m_device(device)
{
    auto queue_family_index = device.queue_family_index(ty);
    if (queue_family_index == -1) {
        spdlog::critical("CommandPool: queue family type {} does not exist on this hardware", ty);
        abort();
    }

    VkResult res;
    VkCommandPoolCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createinfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createinfo.queueFamilyIndex = queue_family_index;
    if (transient)
        createinfo.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    std::vector<VkCommandBuffer> pHandles(primary), sHandles(secondary);
    for (int i = 0; i < 2; i++) {
        if ((res = vkCreateCommandPool(m_device, &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateCommandPool: {}", res);
            abort();
        }
        if ((res = vkResetCommandPool(m_device, m_handle[i], VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT)) != VK_SUCCESS) {
            spdlog::critical("vkResetCommandPool: {}", res);
            abort();
        }

        VkCommandBufferAllocateInfo allocinfo {};
        allocinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocinfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocinfo.commandPool = m_handle[i];
        allocinfo.commandBufferCount = primary;
        if ((res = vkAllocateCommandBuffers(m_device, &allocinfo, pHandles.data())) != VK_SUCCESS) {
            spdlog::critical("vkAllocateCommandBuffers(PRIMARY, {}): {}", primary, res);
            abort();
        }
        for (size_t j = 0; j < primary; j++)
            m_buffers[2 * i].push_back(CommandBuffer(*this, pHandles[j], allocinfo.level));

        if (secondary > 0) {
            allocinfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            allocinfo.commandBufferCount = secondary;
            if ((res = vkAllocateCommandBuffers(m_device, &allocinfo, sHandles.data())) != VK_SUCCESS) {
                spdlog::critical("vkAllocateCommandBuffers(SECONDARY, {}): {}", secondary, res);
                abort();
            }
            for (size_t j = 0; j < secondary; j++)
                m_buffers[2 * i + 1].push_back(CommandBuffer(*this, sHandles[j], allocinfo.level));
        }
    }
}

CommandPool::~CommandPool()
{
    std::vector<VkCommandBuffer> command_buffers[2];
    command_buffers[0].reserve(m_buffers[0].size() + m_buffers[1].size());
    command_buffers[1].reserve(m_buffers[0].size() + m_buffers[1].size());
    for (int i = 0; i < 4; i++) {
        for (auto it = m_buffers[i].begin(); it != m_buffers[i].end(); ++it) {
            command_buffers[i / 2].push_back(it->m_handle);
        }
    }

    for (int i = 0; i < 2; i++)
        vkFreeCommandBuffers(m_device, m_handle[i], command_buffers[i].size(), command_buffers[i].data());
    for (int i = 0; i < 2; i++)
        vkDestroyCommandPool(m_device, m_handle[i], nullptr);
}

void CommandPool::trim()
{
    vkTrimCommandPool(m_device, m_handle[m_device.current_frame() % 2], 0);
}

void CommandPool::reset(bool release_resources)
{
    VkResult res;
    VkCommandPoolResetFlags flags = 0;
    if (release_resources)
        flags |= VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;

    if ((res = vkResetCommandPool(m_device, m_handle[m_device.current_frame() % 2], flags)) != VK_SUCCESS) {
        spdlog::critical("vkResetCommandPool: {}", res);
        abort();
    }
}

CommandBuffer& CommandPool::get(VkCommandBufferLevel level, size_t index)
{
    auto& buffers = m_buffers[((m_device.current_frame() % 2) * 2) + (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ? 0 : 1)];
    if (index < buffers.size())
        return buffers[index];

    spdlog::critical("CommandPool::acquire(level={}, frame={}, index={}): only {} command buffers available",
        level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ? "PRIMARY" : "SECONDARY", index, m_device.current_frame() % 2, buffers.size());
    abort();
}

void CommandBuffer::begin(const RenderPass* render_pass, size_t subpass, VkFramebuffer framebuffer, bool one_time_submit)
{
    VkResult res;
    VkCommandBufferBeginInfo begin_info {};
    VkCommandBufferInheritanceInfo inheritance {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    if (one_time_submit)
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (m_level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
        begin_info.pNext = &inheritance;
        inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritance.renderPass = render_pass ? VkRenderPass(*render_pass) : VK_NULL_HANDLE;
        inheritance.subpass = subpass;
        inheritance.framebuffer = framebuffer;
        // TODO: occlusion query and pipeline statistic query settings: currently not used
    }

    if ((res = vkBeginCommandBuffer(*this, &begin_info)) != VK_SUCCESS) {
        spdlog::critical("vkBeginCommandBuffer: {}", res);
        abort();
    }
}

void CommandBuffer::begin_render_pass(const RenderPass& render_pass, const Framebuffer& framebuffer, int32_t x, int32_t y, uint32_t w, uint32_t h, VkSubpassContents contents)
{
    VkRenderPassBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = render_pass;
    begin_info.framebuffer = framebuffer;
    begin_info.renderArea.offset = { x, y };
    begin_info.renderArea.extent = { w, h };
    begin_info.clearValueCount = render_pass.attachment_count();
    begin_info.pClearValues = render_pass.clear_values().data();

    vkCmdBeginRenderPass(*this, &begin_info, contents);
}

void CommandBuffer::bind_descriptor_set(uint32_t set_number, VkDescriptorSet handle)
{
    vkCmdBindDescriptorSets(*this, m_bound_pipeline_bind_point, m_bound_pipeline_layout, set_number, 1, &handle, 0, nullptr);
}

void CommandBuffer::bind_index_buffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
    vkCmdBindIndexBuffer(*this, buffer, offset, type);
}

void CommandBuffer::bind_pipeline(const Pipeline& p)
{
    m_bound_pipeline_bind_point = p.bind_point();
    m_bound_pipeline_layout = p.layout();
    vkCmdBindPipeline(*this, p.bind_point(), p);
}

void CommandBuffer::bind_vertex_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset)
{
    vkCmdBindVertexBuffers(*this, binding, 1, &buffer, &offset);
}

void CommandBuffer::set_viewport(float x, float y, float w, float h, float min_depth, float max_depth)
{
    VkViewport viewport;
    viewport.x = x;
    viewport.y = y;
    viewport.width = w;
    viewport.height = h;
    viewport.minDepth = min_depth;
    viewport.maxDepth = max_depth;
    vkCmdSetViewport(*this, 0, 1, &viewport);
}

void CommandBuffer::set_scissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D scissor;
    scissor.offset = { x, y };
    scissor.extent = { w, h };
    vkCmdSetScissor(*this, 0, 1, &scissor);
}

void CommandBuffer::end()
{
    VkResult res = vkEndCommandBuffer(*this);
    if (res != VK_SUCCESS) {
        spdlog::critical("vkEndCommandBuffer: {}", res);
        abort();
    }
}

}
