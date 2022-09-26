#include "vkw/vkw.h"
#include <set>
#include <spdlog/spdlog.h>

namespace vkw {

static constexpr auto NO_SUCH_MEMTYPE = std::numeric_limits<size_t>::max();
static VkDeviceAddress align_down(VkDeviceAddress offset, VkDeviceSize alignment)
{
    return offset & ~(alignment - 1);
}

static VkDeviceAddress align_up(VkDeviceAddress offset, VkDeviceSize alignment)
{
    return align_down(offset + alignment - 1, alignment);
}

static bool on_same_page(
    VkDeviceAddress a_offset, VkDeviceSize a_size,
    VkDeviceAddress b_offset,
    VkDeviceSize page_size)
{
    VkDeviceAddress a_end = a_offset + a_size - 1,
                    a_page_end = align_down(a_end, page_size);
    VkDeviceAddress b_page_start = align_down(b_offset, page_size);
    return a_page_end == b_page_start;
}

Allocator::Allocator(const Device& device, bool best_fit)
    : m_device(device)
    , m_best_fit(best_fit)
{
    VkPhysicalDeviceProperties device_props;
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceProperties(device.hwd(), &device_props);
    vkGetPhysicalDeviceMemoryProperties(device.hwd(), &mem_props);

    m_heaps.resize(mem_props.memoryHeapCount);
    m_heaps.assign(mem_props.memoryHeaps, mem_props.memoryHeaps + mem_props.memoryHeapCount);
    m_types.resize(mem_props.memoryTypeCount);
    m_types.assign(mem_props.memoryTypes, mem_props.memoryTypes + mem_props.memoryTypeCount);
    m_pools.resize(mem_props.memoryTypeCount);
    m_pool_mtx.resize(mem_props.memoryTypeCount);
    m_buffer_image_granularity = device_props.limits.bufferImageGranularity;
    m_integrated_gpu = device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

Allocator::~Allocator()
{
    clear();
}

bool Allocator::allocate_defensive(VkMemoryRequirements requirements, VkMemoryPropertyFlags r_flags, VkMemoryPropertyFlags p_flags, bool linear, bool dedicated, SingleAllocation& out)
{
    auto type_index = find_type_index(requirements, r_flags | p_flags);
    if (type_index != NO_SUCH_MEMTYPE) {
        return allocate(requirements, r_flags | p_flags, type_index, linear, dedicated, out);
    }

    // 6. If failed, choose some other memory type that fits only the required usage.
    if (p_flags) {
        type_index = find_type_index(requirements, r_flags);
        if (type_index != NO_SUCH_MEMTYPE) {
            return allocate(requirements, r_flags | p_flags, type_index, linear, dedicated, out);
        }
    }
    return false;
}

bool Allocator::allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags, size_t type_index, bool linear, bool dedicated, SingleAllocation& out)
{
    size_t heap_index = m_types[type_index].heapIndex;
    VkDeviceSize memblock_size = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? (1 << 24) : (1 << 26);
    dedicated |= requirements.size > memblock_size;
    if (m_heaps[heap_index].size < requirements.size)
        return false;

    std::lock_guard lck(m_pool_mtx[type_index]);
    std::unique_ptr<DMemBlock> new_block = nullptr;
    if (dedicated == false) {
        // 1. Try to find a free range of memory in existing blocks.
        auto& pool = m_pools[type_index];
        for (size_t i = 0; i < pool.size(); i++) {
            if (!pool[i])
                continue;

            out.m_allocator = this;
            out.m_block_index = i;
            out.m_type_index = type_index;
            out.m_memory = pool[i]->m_handle;
            if (pool[i]->allocate(requirements.size, requirements.alignment, linear, m_buffer_image_granularity, out))
                return true;
        }

        // 2. If failed, try to create a new block of memory with the preferred size.
        // 3. If failed, try to create a block with size/2, size/4, size/8.
        for (int i = 0; i < 4; i++) {
            new_block = create_memory_block(memblock_size >> i, type_index);
            if (new_block)
                break;
        }
    }

    // if we seek a dedicated allocation, go straight here:
    if (new_block == nullptr) {
        new_block = create_memory_block(requirements.size, type_index);
        if (!new_block) {
            spdlog::critical("fatal: could not allocate block of size {}", requirements.size);
            abort();
        }
    }

    out.m_allocator = this;
    out.m_block_index = insert_block(m_pools[type_index], new_block);
    out.m_type_index = type_index;
    out.m_memory = m_pools[type_index][out.m_block_index]->m_handle;
    return m_pools[type_index][out.m_block_index]->allocate(requirements.size, requirements.alignment, linear, m_buffer_image_granularity, out);
}

size_t Allocator::find_type_index(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) const
{
    for (size_t i = 0; i < m_types.size(); i++) {
        bool is_required_type = requirements.memoryTypeBits & (1 << i);
        bool has_required_props = (m_types[i].propertyFlags & flags) == flags;
        if (is_required_type && has_required_props)
            return i;
    }
    return NO_SUCH_MEMTYPE;
}

std::unique_ptr<Allocator::DMemBlock> Allocator::create_memory_block(VkDeviceSize size, size_t type_index)
{
    VkMemoryAllocateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    createinfo.allocationSize = size;
    createinfo.memoryTypeIndex = static_cast<uint32_t>(type_index);

    VkDeviceMemory handle;
    VkResult res = vkAllocateMemory(m_device, &createinfo, nullptr, &handle);
    if (res == VK_SUCCESS)
        return std::make_unique<Allocator::DMemBlock>(handle, size, m_best_fit);
    else if (res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_OUT_OF_HOST_MEMORY)
        return nullptr;
    else {
        spdlog::critical("vkAllocateMemory: {}", res);
        abort();
    }
}

size_t Allocator::insert_block(Pool& pool, std::unique_ptr<DMemBlock>& block)
{
    size_t pool_entries = pool.size();
    for (size_t i = 0; i < pool_entries; i++) {
        if (bool(pool[i]) == false) {
            pool[i].swap(block);
            assert(bool(block) == false);
            return i;
        }
    }
    pool.push_back(std::move(block));
    return pool_entries;
}

void Allocator::clear()
{
    for (size_t i = 0; i < m_pools.size(); i++) {
        std::lock_guard lck(m_pool_mtx[i]);
        for (auto& itt : m_pools[i]) {
            if (itt)
                vkFreeMemory(m_device, itt->m_handle, nullptr);
        }
    }
    m_pools.clear();
}

VkMemoryPropertyFlags Allocator::required_flags(MemoryUsage usage) const
{
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        if (m_integrated_gpu)
            return 0;
        else
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case MemoryUsage::HostLocal:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case MemoryUsage::HostToDevice:
    case MemoryUsage::HostCached:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    case MemoryUsage::LazilyAllocated:
        return VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    default:
        return 0;
    }
}

VkMemoryPropertyFlags Allocator::preferred_flags(MemoryUsage usage) const
{
    switch (usage) {
    case MemoryUsage::HostCached:
        return VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    default:
        return 0;
    }
}

bool Allocator::allocate(VkBuffer buffer, MemoryUsage usage, SingleAllocation& out)
{
    if (out)
        spdlog::error("Allocator::allocate(): passed allocation pointing to {}: {}", (uintptr_t)out.m_memory, out.m_offset);

    VkMemoryDedicatedRequirements dedicated {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 requirements {};
    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = &dedicated;
    VkBufferMemoryRequirementsInfo2 info {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    info.buffer = buffer;
    vkGetBufferMemoryRequirements2(m_device, &info, &requirements);

    bool success = allocate_defensive(requirements.memoryRequirements, required_flags(usage), preferred_flags(usage), true, dedicated.requiresDedicatedAllocation, out);
    if (success) {
        VkResult res = vkBindBufferMemory(m_device, buffer, out.memory(), out.offset());
        if (res == VK_SUCCESS)
            return true;
        else {
            spdlog::critical("vkBindBufferMemory: {}", res);
            abort();
        }
    } else {
        return false;
    }
}

bool Allocator::allocate(VkImage image, MemoryUsage usage, SingleAllocation& out)
{
    if (out)
        spdlog::error("Allocator::allocate(): passed allocation pointing to {}: {}", (uintptr_t)out.m_memory, out.m_offset);

    VkMemoryDedicatedRequirements dedicated {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 requirements {};
    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = &dedicated;
    VkImageMemoryRequirementsInfo2 info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    info.image = image;
    vkGetImageMemoryRequirements2(m_device, &info, &requirements);

    bool success = allocate_defensive(requirements.memoryRequirements, required_flags(usage), preferred_flags(usage), true, dedicated.requiresDedicatedAllocation, out);
    if (success) {
        VkResult res = vkBindImageMemory(m_device, image, out.memory(), out.offset());
        if (res == VK_SUCCESS)
            return true;
        else {
            spdlog::critical("vkBindImageMemory: {}", res);
            abort();
        }
    } else {
        return false;
    }
}

void Allocator::free(SingleAllocation& a)
{
    if (a) {
        std::lock_guard lck(m_pool_mtx[a.m_type_index]);
        auto& pool = m_pools[a.m_type_index];
        auto& block = pool[a.m_block_index];
        assert(block);
        block->free(a.m_chunk_id);

        if (block->allocated() == 0) {
            if (std::count_if(pool.begin(), pool.end(), [](std::unique_ptr<DMemBlock>& b) { return bool(b); }) > 1) {
                std::unique_ptr<DMemBlock> owned_block = nullptr;
                owned_block.swap(block);

                if (owned_block->m_address)
                    vkUnmapMemory(m_device, owned_block->m_handle);
                vkFreeMemory(m_device, owned_block->m_handle, nullptr);
            }
        }
    }
}

void* Allocator::map_memory(const SingleAllocation& a)
{
    std::lock_guard lck(m_pool_mtx[a.m_type_index]);
    auto& block = m_pools[a.m_type_index][a.m_block_index];
    if (m_types[a.m_type_index].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        if (block->m_address == nullptr) {
            VkResult res;
            if ((res = vkMapMemory(m_device, block->m_handle, 0, VK_WHOLE_SIZE, 0, &block->m_address)) != VK_SUCCESS) {
                spdlog::critical("vkMapMemory: {}", res);
                abort();
            }
            block->m_map_count.store(1);
        } else {
            block->m_map_count.fetch_add(1);
        }

        uintptr_t address = reinterpret_cast<uintptr_t>(block->m_address) + a.m_offset;
        return reinterpret_cast<void*>(address);
    }
    return nullptr;
}

void Allocator::unmap_memory(const SingleAllocation& a)
{
    std::lock_guard lck(m_pool_mtx[a.m_type_index]);
    auto& block = m_pools[a.m_type_index][a.m_block_index];
    if (block->m_address != nullptr) {
        if (block->m_map_count.fetch_sub(1) == 1) {
            block->m_address = nullptr;
            vkUnmapMemory(m_device, block->m_handle);
        }
    }
}

void Allocator::flush_memory(const SingleAllocation& a) const
{
    VkMappedMemoryRange range {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = a.m_memory;
    range.offset = a.m_offset;
    range.size = a.m_size;

    VkResult res;
    if ((res = vkFlushMappedMemoryRanges(m_device, 1, &range)) != VK_SUCCESS) {
        spdlog::critical("vkFlushMappedMemoryRanges: {}", res);
        abort();
    }
}

void Allocator::invalidate(const SingleAllocation& a) const
{
    VkMappedMemoryRange range {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = a.m_memory;
    range.offset = a.m_offset;
    range.size = a.m_size;

    VkResult res;
    if ((res = vkInvalidateMappedMemoryRanges(m_device, 1, &range)) != VK_SUCCESS) {
        spdlog::critical("vkInvalidateMappedMemoryRanges: {}", res);
        abort();
    }
}

void Allocator::write_mapped(const SingleAllocation& dst, const void* src, size_t len)
{
    void* dst_addr = map_memory(dst);
    memcpy(dst_addr, src, len);
    unmap_memory(dst);
}

template <>
void Allocator::write_mapped(const Allocation<1>& dst, const void* src, size_t len)
{
    write_mapped(dst[0], src, len);
}
template <>
void Allocator::write_mapped(const Allocation<2>& dst, const void* src, size_t len)
{
    write_mapped(dst[m_device.current_frame() % 2], src, len);
}

Allocator::DMemBlock::DMemBlock(VkDeviceMemory handle, VkDeviceSize size, bool best_fit)
    : m_handle(handle)
    , m_size(size)
    , m_best_fit(best_fit)
    , m_map_count(0)
    , m_counter(2)
    , m_address(nullptr)
{
    m_chunks[1].m_size = size;
    m_chunks[1].m_offset = 0;
    m_chunks[1].m_ty = Allocator::Subchunk::Type::Free;
    m_chunks[1].m_prev = 0;
    m_chunks[1].m_next = 0;
}

uint64_t Allocator::DMemBlock::next_chunk_id()
{
    if (m_counter.load(std::memory_order::memory_order_relaxed) == std::numeric_limits<uint64_t>::max())
        return 0;
    else
        return m_counter.fetch_add(1);
}

void Allocator::DMemBlock::merge_free_chunks(uint64_t li, uint64_t ri)
{
    Subchunk& right = m_chunks[ri];
    Subchunk& left = m_chunks[li];

    left.m_next = right.m_next;
    left.m_size += right.m_size;
    if (right.m_next != 0)
        m_chunks[right.m_next].m_prev = li;
    m_chunks.erase(ri);
}

bool Allocator::DMemBlock::allocate(VkDeviceSize size, VkDeviceSize alignment, bool linear, VkDeviceSize granularity, SingleAllocation& out)
{
    if (size > m_size - allocated())
        return false;

    uint64_t best_fit_id = 0, best_offset, best_chunk_size, best_aligned_size;
    best_chunk_size = best_aligned_size = std::numeric_limits<uint64_t>::max();

    std::lock_guard lck(m_mtx);
    for (auto& it : m_chunks) {
        Subchunk& chunk = it.second;
        if (chunk.m_ty == Subchunk::Type::Free && chunk.m_size >= size) {
            uint64_t offset = align_up(chunk.m_offset, alignment);
            if (chunk.m_prev != 0) {
                Subchunk& prev_chunk = m_chunks[chunk.m_prev];
                if (chunk.m_ty.has_conflict(prev_chunk.m_ty) && on_same_page(prev_chunk.m_offset, prev_chunk.m_size, offset, granularity))
                    offset = align_up(offset, granularity);
            }

            uint64_t padding = offset - chunk.m_offset, aligned_size = padding + size;
            if (chunk.m_size < aligned_size)
                continue;

            if (chunk.m_next != 0) {
                Subchunk& next_chunk = m_chunks[chunk.m_next];
                if (chunk.m_ty.has_conflict(next_chunk.m_ty) && on_same_page(offset, size, next_chunk.m_offset, granularity))
                    continue;
            }

            if (best_fit_id == 0 || chunk.m_size < best_chunk_size) {
                best_fit_id = it.first;
                best_aligned_size = aligned_size;
                best_offset = offset;
                best_chunk_size = chunk.m_size;
            }
            if (m_best_fit == false)
                break;
        }
    }

    if (best_fit_id == 0)
        return false;

    if (best_chunk_size > best_aligned_size) {
        // Subdivide the best chunk.
        uint64_t parent_id = best_fit_id, child_id = next_chunk_id();
        if (child_id == 0)
            return false;

        m_chunks[child_id].m_size = best_aligned_size;
        m_chunks[child_id].m_offset = m_chunks[parent_id].m_offset;
        m_chunks[child_id].m_ty = linear ? Subchunk::Type::Linear : Subchunk::Type::Image;
        m_chunks[child_id].m_prev = m_chunks[parent_id].m_prev;
        m_chunks[child_id].m_next = best_fit_id;
        m_chunks[parent_id].m_prev = child_id;
        m_chunks[parent_id].m_offset += best_aligned_size;
        m_chunks[parent_id].m_size -= best_aligned_size;
        if (m_chunks[child_id].m_prev != 0)
            m_chunks[m_chunks[child_id].m_prev].m_next = child_id;

        out.m_chunk_id = child_id;
        out.m_size = best_aligned_size;
        out.m_offset = best_offset;
        return true;
    } else {
        // The best chunk is optimal; use it directly.
        m_chunks[best_fit_id].m_ty = linear ? Subchunk::Type::Linear : Subchunk::Type::Image;
        out.m_chunk_id = best_fit_id;
        out.m_size = best_aligned_size;
        out.m_offset = best_offset;
        return true;
    }
}
void Allocator::DMemBlock::free(uint64_t id)
{
    auto it = m_chunks.find(id);
    if (it != m_chunks.end()) {
        std::lock_guard lck(m_mtx);
        it->second.m_ty = Subchunk::Type::Free;
        uint64_t prev_id = it->second.m_prev, next_id = it->second.m_next;
        if (next_id && m_chunks[next_id].m_ty == Subchunk::Type::Free)
            merge_free_chunks(id, next_id);
        if (prev_id && m_chunks[prev_id].m_ty == Subchunk::Type::Free)
            merge_free_chunks(prev_id, id);
    }
}
VkDeviceSize Allocator::DMemBlock::allocated() const
{
    VkDeviceSize total = 0;
    for (auto& ch : m_chunks) {
        if (ch.second.m_ty != Allocator::Subchunk::Type::Free)
            total += ch.second.m_size;
    }
    return total;
}

void SingleAllocation::free()
{
    if (m_allocator)
        m_allocator->free(*this);
}

}
