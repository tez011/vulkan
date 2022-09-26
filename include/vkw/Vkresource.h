#pragma once
#include "Device.h"

namespace vkw {

template <unsigned int N>
class Buffer {
    static_assert(N == 1 || N == 2);

private:
    const Device& m_device;
    VkBufferUsageFlags m_usage;
    VkDeviceSize m_size;
    std::array<VkBuffer, N> m_handle;

    friend class Allocator;

public:
    Buffer(const Device& device, VkBufferUsageFlags usage);
    Buffer(const Device& device, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags = 0);
    Buffer(const Buffer&) = delete;
    void create(VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags = 0);
    ~Buffer();

    template <typename U = std::enable_if<N == 1, VkBuffer>>
    inline operator U() const { return m_handle[0]; }
    inline VkBuffer operator[](int i) const { return m_handle[i]; }
    inline VkDeviceSize size() const { return m_size; }
};

template <unsigned int>
class ImageView;

template <unsigned int N>
class Image {
private:
    const Device& m_device;
    VkImageCreateInfo m_createinfo;
    std::array<VkImage, N> m_handle;

    friend class Allocator;

public:
    Image(const Device& device, VkImageType type, VkImageUsageFlags usage, int samples, int mip_levels, int layers);
    Image(const Device& device, VkImageType type, VkImageUsageFlags usage, int samples, int mip_levels, int layers,
        VkFormat format, const VkExtent3D& extent, const std::initializer_list<QueueFamilyType>& queue_families,
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlags flags = 0);
    Image(const Image&) = delete;
    void create(VkFormat format, const VkExtent3D& extent, const std::initializer_list<QueueFamilyType>& queue_families,
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlags flags = 0);
    ~Image();

    template <typename U = std::enable_if<N == 1, VkImage>>
    inline operator U() const { return m_handle[0]; }
    inline VkImage operator[](int i) const { return m_handle[i]; }

    inline const VkExtent3D& extent() const { return m_createinfo.extent; }
    inline VkFormat format() const { return m_createinfo.format; }
    inline uint32_t layers() const { return m_createinfo.arrayLayers; }
    inline uint32_t mip_levels() const { return m_createinfo.mipLevels; }

    void resize(const VkExtent3D& new_extent);
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
    void create(Image<N>& image, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT, const std::array<uint32_t, 2>& array_layers = { 0, 0 }, const std::array<uint32_t, 2>& mip_levels = { 0, 0 });
    ~ImageView();

    operator VkImageView() const;
    inline VkImageView operator[](int i) const { return m_handle[i]; }
};

class HostBuffer {
protected:
    Allocator& m_allocator;
    VkExtent3D m_extent;
    VkBuffer m_buffer;
    SingleAllocation m_buffer_allocation;

    HostBuffer(Allocator& allocator)
        : m_allocator(allocator)
        , m_extent({ 0, 0, 0 })
        , m_buffer(VK_NULL_HANDLE)
    {
    }
    void initialize(const void* data, size_t length);

public:
    HostBuffer(Allocator& allocator, fs::istream& input);
    HostBuffer(Allocator& allocator, fs::istream& input, VkExtent3D extent);
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer(HostBuffer&&) = default;
    virtual ~HostBuffer();

    inline const VkExtent3D& extent() const { return m_extent; };

    template <unsigned int N>
    void copy_to_buffer(Buffer<N>& out, CommandBuffer& cbuffer);
    template <unsigned int N>
    void copy_to_image(Image<N>& out, CommandBuffer& cbuffer);
};

class PNGImage : public HostBuffer {
private:
    VkFormat m_format;

    static void decode_png(fs::istream& input, std::unique_ptr<char[]>& out_data, size_t& out_length, VkExtent3D& out_extent);

public:
    PNGImage(Allocator& allocator, fs::istream&& input);
    PNGImage(PNGImage&&) = default;

    inline constexpr VkFormat format() const { return VK_FORMAT_R8G8B8A8_SRGB; }
};

}
