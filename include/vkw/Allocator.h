#pragma once
#include "fs.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

namespace vkw {

class Allocator;
class GarbageCollector;

enum class MemoryUsage {
    DeviceLocal,
    HostLocal,
    HostToDevice,
    HostCached,
    LazilyAllocated,
};

class SingleAllocation {
private:
    Allocator* m_allocator;
    uint64_t m_chunk_id;
    size_t m_block_index, m_type_index;
    VkDeviceMemory m_memory;
    VkDeviceAddress m_offset;
    VkDeviceSize m_size;

    friend class Allocator;
    friend class GarbageCollector;

public:
    SingleAllocation()
        : m_allocator(nullptr)
        , m_chunk_id(0)
        , m_block_index(0)
        , m_type_index(0)
        , m_memory(VK_NULL_HANDLE)
        , m_offset(0)
        , m_size(0) {};
    inline operator bool() const { return m_allocator && m_chunk_id != 0 && m_size != 0; };
    VkDeviceMemory memory() const { return m_memory; };
    VkDeviceAddress offset() const { return m_offset; };
    VkDeviceSize size() const { return m_size; };

    void free();
};

template <unsigned int N>
class Allocation : public std::array<SingleAllocation, N> {
    static_assert(N == 1 || N == 2);
};

class Device;
template <unsigned int>
class Buffer;
template <unsigned int>
class Image;

class Allocator {
private:
    struct Subchunk {
        class Type {
        public:
            enum Value {
                Free,
                Linear,
                Image
            };

            Type() = default;
            constexpr Type(Value v)
                : value(v) {};
            constexpr operator Value() const { return value; };
            explicit operator bool() const = delete;

            bool has_conflict(const Type& other) const
            {
                return value != Free && other.value != Free && value != other.value;
            }

        private:
            Value value;
        };
        VkDeviceSize m_size;
        VkDeviceAddress m_offset;
        Type m_ty;
        uint64_t m_prev, m_next;
    };
    class DMemBlock {
    private:
        VkDeviceMemory m_handle;
        size_t m_size;
        bool m_best_fit;
        std::atomic_uint8_t m_map_count;
        std::atomic_uint64_t m_counter;
        void* m_address;
        std::mutex m_mtx;
        std::map<uint64_t, Subchunk> m_chunks;

        uint64_t next_chunk_id();
        void merge_free_chunks(uint64_t left, uint64_t right);

        friend class Allocator;

    public:
        DMemBlock(VkDeviceMemory handle, VkDeviceSize size, bool best_fit);
        DMemBlock(const DMemBlock&) = delete;
        bool allocate(VkDeviceSize size, VkDeviceSize alignment, bool linear, VkDeviceSize granularity, SingleAllocation&);
        void free(uint64_t id);
        VkDeviceSize allocated() const;
    };
    using Pool = std::vector<std::unique_ptr<DMemBlock>>;

    const Device& m_device;
    VkDeviceSize m_buffer_image_granularity;
    bool m_best_fit;
    bool m_integrated_gpu;

    std::vector<VkMemoryHeap> m_heaps;
    std::vector<VkMemoryType> m_types;
    std::vector<Pool> m_pools;
    std::deque<std::mutex> m_pool_mtx;

    bool allocate_defensive(VkMemoryRequirements requirements, VkMemoryPropertyFlags r_flags, VkMemoryPropertyFlags p_flags, bool linear, bool dedicated, SingleAllocation& out);
    bool allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags, size_t type_index, bool linear, bool dedicated, SingleAllocation& out);
    size_t find_type_index(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) const;
    std::unique_ptr<DMemBlock> create_memory_block(VkDeviceSize size, size_t type_index);
    size_t insert_block(Pool& pool, std::unique_ptr<DMemBlock>& block);
    void clear();

    VkMemoryPropertyFlags required_flags(MemoryUsage usage) const;
    VkMemoryPropertyFlags preferred_flags(MemoryUsage usage) const;

public:
    Allocator(const Device& device, bool best_fit = false);
    Allocator(const Allocator&) = delete;
    ~Allocator();

    const Device& device() const { return m_device; }

    bool allocate(VkBuffer buffer, MemoryUsage usage, SingleAllocation& out);
    bool allocate(VkImage image, MemoryUsage usage, SingleAllocation& out);
    void free(SingleAllocation&);

    void* map_memory(const SingleAllocation&);
    void unmap_memory(const SingleAllocation&);
    void flush_memory(const SingleAllocation&) const;
    void invalidate(const SingleAllocation&) const;
    void write_mapped(const SingleAllocation& dst, const void* src, size_t len);

    template <unsigned int N>
    bool allocate(const Buffer<N>& buffer, MemoryUsage usage, Allocation<N>& out)
    {
        int i;
        for (i = 0; i < N; i++) {
            if (allocate(buffer.m_handle[i], usage, out[i]) == false) {
                free(out);
                return false;
            }
        }
        return true;
    }
    template <unsigned int N>
    bool allocate(const Image<N>& image, MemoryUsage usage, Allocation<N>& out)
    {
        int i;
        for (i = 0; i < N; i++) {
            if (allocate(image.m_handle[i], usage, out[i]) == false) {
                free(out);
                return false;
            }
        }
        return true;
    }
    template <typename T, unsigned int N>
    bool reallocate(const T& t, MemoryUsage usage, Allocation<N>& out)
    {
        free(out);
        return allocate(t, usage, out);
    }
    template <unsigned int N>
    void free(Allocation<N>& allocation)
    {
        for (SingleAllocation& s : allocation)
            free(s);
    }
    template <unsigned int N>
    void write_mapped(const Allocation<N>& dst, const void* src, size_t len);
};

}
