#pragma once
#include "Allocator.h"
#include "Device.h"

namespace vkw {

constexpr size_t FormatWidth(VkFormat fmt);

template <unsigned int>
class Buffer;
template <unsigned int>
class Image;

template <unsigned int N>
class Allocation {
private:
    Allocator& m_allocator;
    std::array<SingleAllocation, N> m_allocations;

public:
    Allocation(Allocator& allocator)
        : m_allocator(allocator)
    {
    }
    Allocator& allocator() { return m_allocator; }
    const Allocator& allocator() const { return m_allocator; }
    SingleAllocation& operator[](size_t n) { return m_allocations[n]; }
    const SingleAllocation& operator[](size_t n) const { return m_allocations[n]; }
    SingleAllocation& current() { return m_allocations[m_allocator.device().current_frame() % N]; }

    bool allocate(Buffer<N>& buffer, MemoryUsage usage)
    {
        for (unsigned int i = 0; i < N; i++) {
            if (m_allocator.allocate(buffer[i], usage, m_allocations[i]) == false) {
                free();
                return false;
            }
        }
        return true;
    }
    bool allocate(Image<N>& image, MemoryUsage usage)
    {
        for (unsigned int i = 0; i < N; i++) {
            if (m_allocator.allocate(image[i], usage, m_allocations[i]) == false) {
                free();
                return false;
            }
        }
        return true;
    }
    void free()
    {
        for (unsigned int i = 0; i < N; i++)
            m_allocator.free(m_allocations[i]);
    }
};

template <unsigned int N>
class Buffer {
    static_assert(N == 1 || N == 2);

protected:
    std::array<VkBuffer, N> m_handle;
    Allocation<N> m_allocation;
    VkDeviceSize m_size;

    const Device& device() const { return m_allocation.allocator().device(); }
    void create_empty(Allocator& allocator, MemoryUsage mem_usage, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families = {}, VkBufferCreateFlags flags = 0);

public:
    Buffer(Allocator& allocator, VkDeviceSize size = 0);
    Buffer(Allocator& allocator, MemoryUsage mem_usage, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families = {}, VkBufferCreateFlags flags = 0);
    Buffer(Buffer& src_buffer, MemoryUsage mem_usage, VkBufferUsageFlags usage, const std::initializer_list<QueueFamilyType>& queue_families = {}, VkBufferCreateFlags flags = 0);
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = default;
    virtual ~Buffer();

    inline operator VkBuffer() const { return m_handle[device().current_frame() % N]; }
    inline VkBuffer operator[](int i) const { return m_handle[i]; }
    inline VkDeviceSize size() const { return m_size; }
    inline const Allocation<N>& allocation() const { return m_allocation; }

    void copy_from(Buffer& src_buffer, CommandBuffer& cmd, VkDeviceSize src_offset = 0);
};

template <unsigned int N>
class HostBuffer : public Buffer<N> {
    static_assert(N == 1 || N == 2);

public:
    HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, size_t length);
    HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, fs::istream&& input, size_t length);
    HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, const void* input, size_t length);
    HostBuffer(HostBuffer&&) = default;
    virtual ~HostBuffer() { }

    void write_mapped(const void* data, size_t length);
};

class HostImage : public Buffer<1> {
public:
    enum class InputFormat {
        PNG,
        KTX2,
    };
    static uint32_t count_mip_levels(const VkExtent3D& extent);
    static InputFormat input_format(const std::string_view& extension);

private:
    VkImageCreateInfo m_createinfo {};
    VkImageViewType m_image_view_type;
    std::vector<VkBufferImageCopy> m_copies;

    HostImage(Allocator& allocator);
    HostImage(Allocator& allocator, InputFormat format, fs::istream* input, const void* encoded, size_t encoded_length, bool mipmap);

    template <unsigned int>
    friend class Image;

public:
    HostImage(Allocator& allocator, InputFormat format, fs::istream&& input, bool mipmap)
        : HostImage(allocator, format, &input, nullptr, 0, mipmap)
    {
    }
    HostImage(Allocator& allocator, InputFormat format, const void* encoded, size_t encoded_length, bool mipmap)
        : HostImage(allocator, format, nullptr, encoded, encoded_length, mipmap)
    {
    }
    HostImage(const HostImage&) = delete;
    HostImage(HostImage&&) = default;
    virtual ~HostImage() { }

    inline const VkExtent3D& extent() const { return m_createinfo.extent; }
    inline VkFormat format() const { return m_createinfo.format; }
    inline uint32_t layers() const { return m_createinfo.arrayLayers; }
    inline uint32_t mip_levels() const { return m_createinfo.mipLevels; }
};

template <unsigned int N>
class Image {
private:
    VkImageCreateInfo m_createinfo;
    std::array<VkImage, N> m_handle;
    MemoryUsage m_mem_usage;
    Allocation<N> m_allocation;

    const Device& device() const { return m_allocation.allocator().device(); }

public:
    Image(Allocator& allocator);
    Image(Allocator& allocator, MemoryUsage mem_usage, VkImageType type, VkImageUsageFlags usage, const VkExtent3D& extent, VkFormat format, int samples, int mip_levels, int layers,
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, const std::initializer_list<QueueFamilyType>& queue_families = {}, VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlags flags = 0);
    Image(HostImage& src_image, MemoryUsage mem_usage, VkImageUsageFlags usage, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, const std::initializer_list<QueueFamilyType>& queue_families = {}, VkImageCreateFlags flags = 0);
    Image(const Image&) = delete;
    Image(Image&&) = default;
    ~Image();

    inline operator VkImage() const { return m_handle[device().current_frame() % N]; }
    inline VkImage operator[](int i) const { return m_handle[i]; }

    inline const VkExtent3D& extent() const { return m_createinfo.extent; }
    inline VkFormat format() const { return m_createinfo.format; }
    inline uint32_t layers() const { return m_createinfo.arrayLayers; }
    inline uint32_t mip_levels() const { return m_createinfo.mipLevels; }
    inline Allocation<N>& allocation() { return m_allocation; }
    inline const Allocation<N>& allocation() const { return m_allocation; }

    void copy_from(HostImage& image, CommandBuffer& cmd);
    void generate_mipmaps(CommandBuffer& cmd, uint32_t mip_start, uint32_t mip_end, const VkExtent3D& extent, uint32_t layer_count, VkImageLayout initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout final_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    void resize(const VkExtent3D& new_extent);
    void set_layout(VkImageLayout from, VkImageLayout to, CommandBuffer& cmd, VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT, VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
};

template <unsigned int N>
class ImageView {
private:
    const Device& m_device;
    std::array<VkImageView, N> m_handle;

    friend class Image<N>;
    friend class GarbageCollector;

public:
    ImageView(const Device& device)
        : m_device(device)
    {
        m_handle.fill(VK_NULL_HANDLE);
    }
    void create(Image<N>& src_image, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT, const std::array<uint32_t, 2>& array_layers = { 0, 0 }, const std::array<uint32_t, 2>& mip_levels = { 0, 0 });
    ~ImageView();

    operator VkImageView() const;
    inline VkImageView operator[](int i) const { return m_handle[i]; }
};
}
