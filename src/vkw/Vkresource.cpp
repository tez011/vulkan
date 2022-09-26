#include "vkw/vkw.h"
#include <spdlog/spdlog.h>
#include <spng.h>

namespace vkw {

std::vector<uint32_t> unique_queue_families(const Device& device, const std::initializer_list<QueueFamilyType>& queue_families)
{
    if (queue_families.size() == 1) {
        return { static_cast<uint32_t>(device.queue_family_index(*queue_families.begin())) };
    }

    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(device.hwd(), &count, nullptr);
    std::vector<bool> qfmap(count);
    std::vector<uint32_t> out;
    for (auto& ty : queue_families) {
        uint32_t index = device.queue_family_index(ty);
        if (qfmap[index] == false) {
            qfmap[index] = true;
            out.push_back(index);
        }
    }
    return out;
}

template <unsigned int N>
Buffer<N>::Buffer(const Device& device, VkBufferUsageFlags usage)
    : m_device(device)
    , m_usage(usage)
    , m_size(0)
{
    m_handle.fill(VK_NULL_HANDLE);
}

template <unsigned int N>
Buffer<N>::Buffer(const Device& device, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags)
    : m_device(device)
    , m_usage(usage)
    , m_size(size)
{
    create(size, queue_families, flags);
}

template <unsigned int N>
void Buffer<N>::create(VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags)
{
    VkResult res;
    std::vector<uint32_t> qfv = unique_queue_families(m_device, queue_families);
    VkBufferCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createinfo.flags = flags;
    createinfo.size = size;
    createinfo.usage = m_usage;
    createinfo.sharingMode = qfv.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    createinfo.queueFamilyIndexCount = qfv.size();
    createinfo.pQueueFamilyIndices = qfv.data();

    m_size = size;
    for (int i = 0; i < N; i++) {
        if ((res = vkCreateBuffer(m_device, &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateBuffer: {}", res);
            abort();
        }
    }
}

template <unsigned int N>
Buffer<N>::~Buffer()
{
    for (VkBuffer& buffer : m_handle)
        vkDestroyBuffer(m_device, buffer, nullptr);
}

template class Buffer<1>;
template class Buffer<2>;

template <unsigned int N>
Image<N>::Image(const Device& device, VkImageType type, VkImageUsageFlags usage, int samples, int mip_levels, int layers)
    : m_device(device)
    , m_createinfo({ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO })
{
    m_createinfo.imageType = type;
    m_createinfo.mipLevels = mip_levels;
    m_createinfo.arrayLayers = layers;
    m_createinfo.samples = static_cast<VkSampleCountFlagBits>(samples);
    m_createinfo.usage = usage;
    m_handle.fill(VK_NULL_HANDLE);
}

template <unsigned int N>
Image<N>::Image(const Device& device, VkImageType type, VkImageUsageFlags usage, int samples, int mip_levels, int layers,
    VkFormat format, const VkExtent3D& extent, const std::initializer_list<QueueFamilyType>& queue_families,
    VkImageTiling tiling, VkImageLayout initial_layout, VkImageCreateFlags flags)
    : m_device(device)
    , m_createinfo({ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO })
{
    m_createinfo.imageType = type;
    m_createinfo.mipLevels = mip_levels;
    m_createinfo.arrayLayers = layers;
    m_createinfo.samples = static_cast<VkSampleCountFlagBits>(samples);
    m_createinfo.usage = usage;
    create(format, extent, queue_families, tiling, initial_layout, flags);
}

template <unsigned int N>
void Image<N>::create(VkFormat format, const VkExtent3D& extent, const std::initializer_list<QueueFamilyType>& queue_families,
    VkImageTiling tiling, VkImageLayout initial_layout, VkImageCreateFlags flags)
{
    VkResult res;
    std::vector<uint32_t> qfv = unique_queue_families(m_device, queue_families);
    m_createinfo.flags = flags;
    m_createinfo.format = format;
    m_createinfo.extent = extent;
    m_createinfo.tiling = tiling;
    m_createinfo.sharingMode = qfv.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    m_createinfo.queueFamilyIndexCount = qfv.size();
    m_createinfo.pQueueFamilyIndices = qfv.data();
    m_createinfo.initialLayout = initial_layout;

    for (int i = 0; i < N; i++) {
        if ((res = vkCreateImage(m_device, &m_createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }
    }
}

template <unsigned int N>
Image<N>::~Image()
{
    for (VkImage& image : m_handle)
        vkDestroyImage(m_device, image, nullptr);
}

template <unsigned int N>
void Image<N>::resize(const VkExtent3D& new_extent)
{
    VkResult res;
    m_createinfo.extent = new_extent;
    for (int i = 0; i < N; i++) {
        if ((res = vkCreateImage(m_device, &m_createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }
    }
}

template class Image<1>;
template class Image<2>;

template <unsigned int N>
void ImageView<N>::create(vkw::Image<N>& image, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_mask, const std::array<uint32_t, 2>& array_layers, const std::array<uint32_t, 2>& mip_levels)
{
    VkImageViewCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createinfo.viewType = type;
    createinfo.format = format;
    createinfo.subresourceRange.aspectMask = aspect_mask;
    createinfo.subresourceRange.baseArrayLayer = array_layers[0];
    createinfo.subresourceRange.layerCount = array_layers[1] ? array_layers[1] : image.layers();
    createinfo.subresourceRange.baseMipLevel = mip_levels[0];
    createinfo.subresourceRange.levelCount = mip_levels[1] ? mip_levels[1] : image.mip_levels();
    for (int i = 0; i < N; i++) {
        VkResult res;
        createinfo.image = image[i];
        if ((res = vkCreateImageView(m_device, &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImageView: {}", res);
            abort();
        }
    }
}

template <unsigned int N>
ImageView<N>::~ImageView()
{
    for (int i = 0; i < N; i++)
        vkDestroyImageView(m_device, m_handle[i], nullptr);
}

template <unsigned int N>
ImageView<N>::operator VkImageView() const
{
    return m_handle[m_device.current_frame() % N];
}

template class ImageView<1>;
template class ImageView<2>;

HostBuffer::HostBuffer(Allocator& allocator, fs::istream& input)
    : m_allocator(allocator)
    , m_extent({ static_cast<uint32_t>(input.length()), 0, 0 })
{
    size_t len = input.length();
    std::vector<char> slurp_buffer(len);
    input.read(slurp_buffer.data(), len);
    initialize(slurp_buffer.data(), len);
}

HostBuffer::HostBuffer(Allocator& allocator, fs::istream& input, VkExtent3D extent)
    : m_allocator(allocator)
    , m_extent(extent)
{
    size_t len = input.length();
    std::vector<char> slurp_buffer(len);
    input.read(slurp_buffer.data(), len);
    initialize(slurp_buffer.data(), len);
}

HostBuffer::~HostBuffer()
{
    m_allocator.free(m_buffer_allocation);
    vkDestroyBuffer(m_allocator.device(), m_buffer, nullptr);
}

void HostBuffer::initialize(const void* data, size_t len)
{
    VkDevice device = m_allocator.device();
    VkResult res;
    VkBufferCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createinfo.size = len;
    createinfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    createinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if ((res = vkCreateBuffer(device, &createinfo, nullptr, &m_buffer)) != VK_SUCCESS) {
        spdlog::critical("vkCreateBuffer: {}", res);
        abort();
    }

    m_allocator.allocate(m_buffer, MemoryUsage::HostLocal, m_buffer_allocation);
    m_allocator.write_mapped(m_buffer_allocation, data, len);
}

template <unsigned int N>
void HostBuffer::copy_to_image(Image<N>& out, CommandBuffer& command)
{
    std::array<VkImageMemoryBarrier, 2 * N> barrier {};
    for (int i = 0; i < 2 * N; i++) {
        barrier[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier[i].oldLayout = i < N ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier[i].newLayout = i < N ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[i].image = out[i % N];
        barrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier[i].subresourceRange.baseMipLevel = 0;
        barrier[i].subresourceRange.levelCount = 1;
        barrier[i].subresourceRange.baseArrayLayer = 0;
        barrier[i].subresourceRange.layerCount = 1;
        barrier[i].srcAccessMask = i < N ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier[i].dstAccessMask = i < N ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, N, barrier.data());

    VkBufferImageCopy region {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent = m_extent;
    for (int i = 0; i < N; i++)
        vkCmdCopyBufferToImage(command, m_buffer, out[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, N, barrier.data() + N);
}

template void HostBuffer::copy_to_image(Image<1>&, CommandBuffer&);
template void HostBuffer::copy_to_image(Image<2>&, CommandBuffer&);

PNGImage::PNGImage(Allocator& allocator, fs::istream&& input)
    : HostBuffer(allocator)
{
    std::unique_ptr<char[]> decoded;
    size_t decoded_len = 0;
    decode_png(input, decoded, decoded_len, m_extent);
    initialize(decoded.get(), decoded_len);
}

static int png_read(spng_ctx* ctx, void* user, void* dst, size_t length)
{
    fs::istream* input = reinterpret_cast<fs::istream*>(user);
    input->read(reinterpret_cast<char*>(dst), length);
    if (input->good())
        return 0;
    else if (input->eof())
        return SPNG_IO_EOF;
    else
        return SPNG_IO_ERROR;
}

void PNGImage::decode_png(fs::istream& input, std::unique_ptr<char[]>& out_data, size_t& out_length, VkExtent3D& out_extent)
{
    int res = 0;
    spng_ctx* ctx = spng_ctx_new(0);
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    spng_set_chunk_limits(ctx, 1 << 26, 1 << 26);
    if ((res = spng_set_png_stream(ctx, png_read, &input)) != 0) {
        spdlog::critical("spng_set_png_stream: {}", spng_strerror(res));
        abort();
    }

    struct spng_ihdr ihdr;
    if ((res = spng_get_ihdr(ctx, &ihdr)) != 0) {
        spdlog::critical("spng_get_ihdr: {}", spng_strerror(res));
        abort();
    }
    out_extent.width = ihdr.width;
    out_extent.height = ihdr.height;
    out_extent.depth = 1;
    if ((res = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_length)) != 0) {
        spdlog::critical("spng_decoded_image_size: {}", spng_strerror(res));
        abort();
    }

    out_data = std::make_unique<char[]>(out_length);
    if ((res = spng_decode_image(ctx, out_data.get(), out_length, SPNG_FMT_RGBA8, 0)) != 0) {
        spdlog::critical("spng_decode_image: {}", spng_strerror(res));
        abort();
    }
    spng_ctx_free(ctx);
}

}
