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

HostBuffer::HostBuffer(Allocator& allocator, fs::istream&& input)
    : HostBuffer(allocator, std::move(input), { static_cast<uint32_t>(input.length()), 0, 0 })
{
}

HostBuffer::HostBuffer(Allocator& allocator, fs::istream&& input, const VkExtent3D& extent)
    : m_allocator(allocator)
    , m_extent(extent)
    , m_buffer(create_buffer(allocator.device(), input.length()))
{
    std::vector<char> slurp_buffer(input.length());
    input.read(slurp_buffer.data(), slurp_buffer.size());
    allocator.allocate(m_buffer, MemoryUsage::HostLocal, m_buffer_allocation);
    allocator.write_mapped(m_buffer_allocation, slurp_buffer.data(), slurp_buffer.size());
}

HostBuffer::~HostBuffer()
{
    m_allocator.free(m_buffer_allocation);
    vkDestroyBuffer(m_allocator.device(), m_buffer, nullptr);
}

VkBuffer HostBuffer::create_buffer(VkDevice device, size_t len)
{
    VkResult res;
    VkBuffer out_buffer;
    VkBufferCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createinfo.size = len;
    createinfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    createinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if ((res = vkCreateBuffer(device, &createinfo, nullptr, &out_buffer)) == VK_SUCCESS)
        return out_buffer;

    spdlog::critical("vkCreateBuffer: {}", res);
    abort();
}

HostImage::HostImage(Allocator& allocator, fs::file&& input, const VkExtent3D& extent, VkFormat format)
    : HostBuffer(allocator, extent)
    , m_format(format)
    , m_mip_levels(1)
{
    std::unique_ptr<char[]> decoded;
    size_t decoded_len = 0;

    if (input.extension() == "png") {
        decode_png(fs::istream(input), decoded, decoded_len, m_extent);
        m_format = VK_FORMAT_R8G8B8A8_SRGB;
    } else {
        decode_raw(fs::istream(input), decoded, decoded_len);
    }

    m_buffer = create_buffer(allocator.device(), decoded_len);
    allocator.allocate(m_buffer, MemoryUsage::HostLocal, m_buffer_allocation);
    allocator.write_mapped(m_buffer_allocation, decoded.get(), decoded_len);
}

HostImage::HostImage(Allocator& allocator, fs::file&& input, fs::file&& mipmap_input)
    : HostBuffer(allocator)
{
    std::unique_ptr<char[]> decoded[2];
    size_t decoded_len[2];

#ifndef NDEBUG
    if (input.extension() != mipmap_input.extension()) {
        spdlog::critical("HostImage: mipmap atlas format {} must match base image format {}", mipmap_input.extension(), input.extension());
        abort();
    }
#endif
    if (input.extension() == "png") {
        decode_png(fs::istream(mipmap_input), decoded[1], decoded_len[1], m_extent);
        decode_png(fs::istream(input), decoded[0], decoded_len[0], m_extent);
        m_format = VK_FORMAT_R8G8B8A8_SRGB;
    } else {
        spdlog::critical("HostImage: unknown image format {}", input.extension());
        abort();
    }

    m_buffer = create_buffer(allocator.device(), decoded_len[0]);
    allocator.allocate(m_buffer, MemoryUsage::HostLocal, m_buffer_allocation);
    allocator.write_mapped(m_buffer_allocation, decoded[0].get(), decoded_len[0]);

    m_mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_extent.width, m_extent.height)))) + 1;
    m_mip_buffer = create_buffer(allocator.device(), decoded_len[1]);
    allocator.allocate(m_mip_buffer, MemoryUsage::HostLocal, m_mip_allocation);
    allocator.write_mapped(m_mip_allocation, decoded[1].get(), decoded_len[1]);
}

HostImage::~HostImage()
{
    if (m_mip_allocation)
        m_allocator.free(m_mip_allocation);
    if (m_mip_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_allocator.device(), m_mip_buffer, nullptr);
}

template <unsigned int N>
void HostImage::copy_to_image(Image<N>& out, CommandBuffer& command)
{
    std::array<VkImageMemoryBarrier, N> barrier {};
    for (int i = 0; i < N; i++) {
        barrier[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[i].image = out[i];
        barrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier[i].subresourceRange.baseMipLevel = 0;
        barrier[i].subresourceRange.levelCount = m_mip_levels;
        barrier[i].subresourceRange.baseArrayLayer = 0;
        barrier[i].subresourceRange.layerCount = 1;
        barrier[i].srcAccessMask = 0;
        barrier[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, N, barrier.data());

    std::vector<VkBufferImageCopy> copies(m_mip_levels);
    int32_t mip_position = 0;
    for (int i = 0; i < m_mip_levels; i++) {
        copies[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copies[i].imageSubresource.mipLevel = i;
        copies[i].imageSubresource.baseArrayLayer = 0;
        copies[i].imageSubresource.layerCount = 1;
        copies[i].imageOffset = { 0, 0, 0 };
        copies[i].imageExtent.width = m_extent.width >> i;
        copies[i].imageExtent.height = m_extent.height >> i;
        copies[i].imageExtent.depth = 1;
        if (i == 0) {
            // base image, tightly packed
            copies[i].bufferOffset = copies[i].bufferRowLength = copies[i].bufferImageHeight = 0;
        } else {
            // mip image
            copies[i].bufferOffset = FormatWidth(m_format) * (mip_position * m_extent.width / 2);
            copies[i].bufferRowLength = m_extent.width / 2;
            copies[i].bufferImageHeight = m_extent.height;
            mip_position += copies[i].imageExtent.height;
        }
    }
    for (int i = 0; i < N; i++) {
        vkCmdCopyBufferToImage(command, m_buffer, out[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, copies.data());
        vkCmdCopyBufferToImage(command, m_mip_buffer, out[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mip_levels - 1, copies.data() + 1);
    }

    for (int i = 0; i < N; i++) {
        barrier[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, N, barrier.data());
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

void HostImage::decode_png(fs::istream& input, std::unique_ptr<char[]>& out_data, size_t& out_length, VkExtent3D& out_extent)
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

void HostImage::decode_raw(fs::istream& input, std::unique_ptr<char[]>& out_data, size_t& out_length)
{
    out_length = input.length();
    out_data = std::make_unique<char[]>(out_length);
    input.read(out_data.get(), out_length);
}

template void HostImage::copy_to_image(Image<1>&, CommandBuffer&);
template void HostImage::copy_to_image(Image<2>&, CommandBuffer&);

size_t FormatWidth(VkFormat fmt)
{
    switch (fmt) {
    case VK_FORMAT_UNDEFINED:
        return 0;
    case VK_FORMAT_R4G4_UNORM_PACK8:
        return 1;
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        return 2;
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_USCALED:
    case VK_FORMAT_R8_SSCALED:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
        return 1;
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8_USCALED:
    case VK_FORMAT_R8G8_SSCALED:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8_SRGB:
        return 2;
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8_SNORM:
    case VK_FORMAT_R8G8B8_USCALED:
    case VK_FORMAT_R8G8B8_SSCALED:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8_SNORM:
    case VK_FORMAT_B8G8R8_USCALED:
    case VK_FORMAT_B8G8R8_SSCALED:
    case VK_FORMAT_B8G8R8_UINT:
    case VK_FORMAT_B8G8R8_SINT:
    case VK_FORMAT_B8G8R8_SRGB:
        return 3;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_USCALED:
    case VK_FORMAT_R8G8B8A8_SSCALED:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SNORM:
    case VK_FORMAT_B8G8R8A8_USCALED:
    case VK_FORMAT_B8G8R8A8_SSCALED:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
        return 4;
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16_USCALED:
    case VK_FORMAT_R16_SSCALED:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT:
        return 2;
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_USCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16_UNORM:
    case VK_FORMAT_R16G16B16_SNORM:
    case VK_FORMAT_R16G16B16_USCALED:
    case VK_FORMAT_R16G16B16_SSCALED:
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16_SFLOAT:
        return 6;
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_USCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64_SFLOAT:
        return 8;
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64_SFLOAT:
        return 16;
    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64_SFLOAT:
        return 24;
    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_R64G64B64A64_SFLOAT:
        return 32;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return 4;
    case VK_FORMAT_D16_UNORM:
        return 2;
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        return 4;
    case VK_FORMAT_S8_UINT:
        return 1;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return 3;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 5;
    default:
        spdlog::critical("vkw::FormatWidth({}): unknown", fmt);
        abort();
    }
}
}
