#include "vkw/vkw.h"
#include <ktx.h>
#include <spdlog/spdlog.h>
#include <spng.h>
#include <utility>

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
Buffer<N>::Buffer(Allocator& allocator, VkDeviceSize size)
    : m_allocation(allocator)
    , m_size(size)
{
    m_handle.fill(VK_NULL_HANDLE);
}

template <unsigned int N>
Buffer<N>::Buffer(Allocator& allocator, MemoryUsage mem_usage, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags)
    : m_allocation(allocator)
    , m_size(size)
{
    create_empty(allocator, mem_usage, usage, size, queue_families, flags);
}

template <unsigned int N>
Buffer<N>::Buffer(Buffer<N>& src_buffer, MemoryUsage mem_usage, VkBufferUsageFlags usage, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags)
    : m_allocation(src_buffer.m_allocation.allocator())
    , m_size(src_buffer.size())
{
    create_empty(m_allocation.allocator(), mem_usage, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_size, queue_families, flags);
}

template <unsigned int N>
Buffer<N>::~Buffer()
{
    m_allocation.free();
    for (VkBuffer& buffer : m_handle)
        vkDestroyBuffer(device(), buffer, nullptr);
}

template <unsigned int N>
void Buffer<N>::create_empty(Allocator& allocator, MemoryUsage mem_usage, VkBufferUsageFlags usage, VkDeviceSize size, const std::initializer_list<QueueFamilyType>& queue_families, VkBufferCreateFlags flags)
{
    VkResult res;
    VkBufferCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createinfo.flags = flags;
    createinfo.size = size;
    createinfo.usage = usage;
    if (queue_families.size() > 0) {
        std::vector<uint32_t> qfv = unique_queue_families(device(), queue_families);
        createinfo.sharingMode = qfv.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
        createinfo.queueFamilyIndexCount = qfv.size();
        createinfo.pQueueFamilyIndices = qfv.data();
    } else {
        createinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    for (int i = 0; i < N; i++) {
        if ((res = vkCreateBuffer(device(), &createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateBuffer: {}", res);
            abort();
        }
        if (allocator.allocate(m_handle[i], mem_usage, m_allocation[i]) == false) {
            spdlog::critical("could not allocate memory for buffer({})", size);
            abort();
        }
    }
}

template <unsigned int N>
void Buffer<N>::copy_from(Buffer<N>& src_buffer, CommandBuffer& cmd, VkDeviceSize src_offset)
{
    VkBufferCopy copy;
    copy.srcOffset = src_offset;
    copy.dstOffset = 0;
    copy.size = std::min(src_buffer.m_size - src_offset, m_size);
    for (unsigned int i = 0; i < N; i++)
        vkCmdCopyBuffer(cmd, src_buffer.m_handle[i], m_handle[i], 1, &copy);
}

template class Buffer<1>;
template class Buffer<2>;

template <unsigned int N>
HostBuffer<N>::HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, size_t length)
    : Buffer<N>(allocator, length)
{
    if (length > 0) {
        this->create_empty(allocator, vkw::MemoryUsage::HostLocal, usage, length);
    }
}

template <unsigned int N>
HostBuffer<N>::HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, fs::istream&& input, size_t length)
    : HostBuffer(allocator, usage, length)
{
    for (int i = 0; i < N; i++) {
        void* mapped_buffer = allocator.map_memory(this->m_allocation[i]);
        input.read(reinterpret_cast<char*>(mapped_buffer), length);
        allocator.unmap_memory(this->m_allocation[i]);
    }
}

template <unsigned int N>
HostBuffer<N>::HostBuffer(Allocator& allocator, VkBufferUsageFlags usage, const void* input, size_t length)
    : HostBuffer(allocator, usage, length)
{
    for (int i = 0; i < N; i++) {
        void* mapped_buffer = allocator.map_memory(this->m_allocation[i]);
        memcpy(mapped_buffer, input, length);
        allocator.unmap_memory(this->m_allocation[i]);
    }
}

template <unsigned int N>
void HostBuffer<N>::write_mapped(const void* data, size_t length)
{
    Allocator& allocator = this->m_allocation.allocator();
    SingleAllocation& active_buffer = this->m_allocation[allocator.device().current_frame() % N];
    void* mapped_buffer = allocator.map_memory(active_buffer);
    memcpy(mapped_buffer, data, length);
    allocator.unmap_memory(active_buffer);
}

template class HostBuffer<1>;
template class HostBuffer<2>;

static int png_stream_read(spng_ctx* ctx, void* user, void* dst, size_t len)
{
    std::istream* input = reinterpret_cast<fs::istream*>(user);
    input->read(reinterpret_cast<char*>(dst), len);
    if (input->good())
        return SPNG_OK;
    else if (input->eof())
        return SPNG_IO_EOF;
    else
        return SPNG_IO_ERROR;
}

struct ktx_fs_istream {
    ktxStream handle;
    fs::istream* stream;

    static KTX_error_code read(ktxStream* str, void* dst, const ktx_size_t count);
    static KTX_error_code skip(ktxStream* str, const ktx_size_t count);
    static KTX_error_code getpos(ktxStream* str, ktx_off_t* const offset);
    static KTX_error_code setpos(ktxStream* str, const ktx_off_t offset);
    static KTX_error_code getsize(ktxStream* str, ktx_size_t* const size);

    ktx_fs_istream(fs::istream* s)
        : stream(s)
    {
        assert(&handle == reinterpret_cast<ktxStream*>(this));
        handle.read = read;
        handle.skip = skip;
        handle.write = nullptr;
        handle.getpos = getpos;
        handle.setpos = setpos;
        handle.getsize = getsize;
        handle.destruct = [](ktxStream*) {};
    }
    ktx_fs_istream(ktx_fs_istream&) = delete;
};

KTX_error_code ktx_fs_istream::read(ktxStream* str, void* dst, const ktx_size_t count)
{
    fs::istream* input = reinterpret_cast<ktx_fs_istream*>(str)->stream;
    input->read(reinterpret_cast<char*>(dst), count);
    if (input->good())
        return KTX_SUCCESS;
    else if (input->eof())
        return KTX_FILE_UNEXPECTED_EOF;
    else
        return KTX_FILE_READ_ERROR;
}
KTX_error_code ktx_fs_istream::skip(ktxStream* str, const ktx_size_t count)
{
    fs::istream* input = reinterpret_cast<ktx_fs_istream*>(str)->stream;
    input->seekg(count, std::ios_base::cur);
    if (input->good())
        return KTX_SUCCESS;
    else if (input->eof())
        return KTX_FILE_UNEXPECTED_EOF;
    else
        return KTX_FILE_SEEK_ERROR;
}
KTX_error_code ktx_fs_istream::getpos(ktxStream* str, ktx_off_t* const offset)
{
    fs::istream* input = reinterpret_cast<ktx_fs_istream*>(str)->stream;
    std::streampos pos = input->tellg();
    if (pos >= 0) {
        if (offset)
            *offset = pos;
        return KTX_SUCCESS;
    } else
        return KTX_FILE_READ_ERROR;
}
KTX_error_code ktx_fs_istream::setpos(ktxStream* str, const ktx_off_t offset)
{
    fs::istream* input = reinterpret_cast<ktx_fs_istream*>(str)->stream;
    input->seekg(offset);
    if (input->good())
        return KTX_SUCCESS;
    else if (input->eof())
        return KTX_FILE_UNEXPECTED_EOF;
    else
        return KTX_FILE_SEEK_ERROR;
}
KTX_error_code ktx_fs_istream::getsize(ktxStream* str, ktx_size_t* const size)
{
    fs::istream* input = reinterpret_cast<ktx_fs_istream*>(str)->stream;
    if (size)
        *size = input->length();
    return KTX_SUCCESS;
}

struct ktx_mip_iterate_userdata {
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset;
    uint32_t layer_count;

    ktx_mip_iterate_userdata(uint32_t region_count, uint32_t layer_count)
        : offset(0)
        , layer_count(layer_count)
    {
        regions.reserve(region_count);
    }
};
static KTX_error_code ktx_mip_iterate(int miplevel, int face, int width, int height, int depth, ktx_uint64_t face_lod_size, void* pixels, void* userdata)
{
    ktx_mip_iterate_userdata* ud = reinterpret_cast<ktx_mip_iterate_userdata*>(userdata);
    (void)(pixels);

    VkBufferImageCopy& region = ud->regions.emplace_back();
    region.bufferOffset = ud->offset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = miplevel;
    region.imageSubresource.baseArrayLayer = face;
    region.imageSubresource.layerCount = ud->layer_count;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = depth;
    ud->offset += face_lod_size;
    return KTX_SUCCESS;
}

HostImage::HostImage(Allocator& allocator)
    : Buffer<1>(allocator)
{
    m_createinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    m_createinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    m_createinfo.samples = VK_SAMPLE_COUNT_1_BIT;
    m_createinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    m_createinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

HostImage::HostImage(Allocator& allocator, InputFormat format, fs::istream* stream, const void* encoded, size_t encoded_length, bool mipmap)
    : HostImage(allocator)
{
    if (format == InputFormat::PNG) {
        int res;
        size_t out_length;
        spng_ctx* ctx = spng_ctx_new(0);
        spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
        spng_set_chunk_limits(ctx, 1 << 26, 1 << 26);
        if (stream) {
            if ((res = spng_set_png_stream(ctx, png_stream_read, stream)) != 0) {
                spdlog::critical("spng_set_png_stream: {}", spng_strerror(res));
                abort();
            }
        } else if ((res = spng_set_png_buffer(ctx, encoded, encoded_length)) != 0) {
            spdlog::critical("spng_set_png_buffer: {}", spng_strerror(res));
            abort();
        }

        struct spng_ihdr ihdr;
        if ((res = spng_get_ihdr(ctx, &ihdr)) != 0) {
            spdlog::critical("spng_get_ihdr: {}", spng_strerror(res));
            abort();
        }
        if ((res = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_length)) != 0) {
            spdlog::critical("spng_decoded_image_size: {}", spng_strerror(res));
            abort();
        }

        create_empty(allocator, vkw::MemoryUsage::HostLocal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, out_length);
        void* mapped_buffer = allocator.map_memory(m_allocation[0]);
        if ((res = spng_decode_image(ctx, mapped_buffer, out_length, SPNG_FMT_RGBA8, 0)) != 0) {
            spdlog::critical("spng_decode_image: {}", spng_strerror(res));
            abort();
        }

        m_image_view_type = VK_IMAGE_VIEW_TYPE_2D;
        m_createinfo.imageType = VK_IMAGE_TYPE_2D;
        m_createinfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        m_createinfo.extent = { ihdr.width, ihdr.height, 1 };
        m_createinfo.mipLevels = mipmap ? count_mip_levels(m_createinfo.extent) : 1;
        m_createinfo.arrayLayers = 1;

        VkBufferImageCopy& base_copy = m_copies.emplace_back();
        base_copy.bufferOffset = 0;
        base_copy.bufferRowLength = 0;
        base_copy.bufferImageHeight = 0;
        base_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        base_copy.imageSubresource.mipLevel = 0;
        base_copy.imageSubresource.baseArrayLayer = 0;
        base_copy.imageSubresource.layerCount = 1;
        base_copy.imageOffset = { 0, 0, 0 };
        base_copy.imageExtent = m_createinfo.extent;

        allocator.unmap_memory(m_allocation[0]);
        spng_ctx_free(ctx);
    } else if (format == InputFormat::KTX2) {
        VkResult res;
        ktxTexture* ktx;
        ktxTexture2* ktx2 = nullptr;
        KTX_error_code k_result;
        if (stream) {
            ktx_fs_istream ktx_stream(stream);
            k_result = ktxTexture2_CreateFromStream(&ktx_stream.handle, KTX_TEXTURE_CREATE_NO_FLAGS, &ktx2);
        } else {
            k_result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(encoded), encoded_length, KTX_TEXTURE_CREATE_NO_FLAGS, &ktx2);
        }
        if (k_result != KTX_SUCCESS) {
            spdlog::critical("ktx: failed to load image");
            abort();
        }
        ktx = reinterpret_cast<ktxTexture*>(ktx2);

        switch (ktx->numDimensions) {
        case 1:
            m_createinfo.imageType = VK_IMAGE_TYPE_1D;
            m_image_view_type = ktx->isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            break;
        case 2:
            m_createinfo.imageType = VK_IMAGE_TYPE_2D;
            if (ktx->isCubemap) {
                m_image_view_type = ktx->isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            } else {
                m_image_view_type = ktx->isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            }
            break;
        case 3:
            m_createinfo.imageType = VK_IMAGE_TYPE_3D;
            m_image_view_type = VK_IMAGE_VIEW_TYPE_3D;
            break;
        default:
            abort();
        }
        m_createinfo.extent.width = ktx->baseWidth;
        m_createinfo.extent.height = ktx->baseHeight;
        m_createinfo.extent.depth = ktx->baseDepth;
        m_createinfo.arrayLayers = ktx->numLayers;
        if (ktx->isCubemap) {
            m_createinfo.arrayLayers *= 6;
            m_createinfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        bool should_generate_mipmaps = mipmap && ktx->generateMipmaps;
        if (should_generate_mipmaps)
            m_createinfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        if ((m_createinfo.format = static_cast<VkFormat>(ktx2->vkFormat)) == VK_FORMAT_UNDEFINED) {
            spdlog::critical("ktx: unknown image format");
            abort();
        }

        VkImageFormatProperties image_format_props;
        res = vkGetPhysicalDeviceImageFormatProperties(allocator.device().hwd(), m_createinfo.format, m_createinfo.imageType, m_createinfo.tiling, m_createinfo.usage, 0, &image_format_props);
        if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            spdlog::critical("ktx: format {} not supported by hardware", m_createinfo.format);
            abort();
        } else if (res != VK_SUCCESS) {
            spdlog::critical("vkGetPhysicalDeviceImageFormatProperties: {}", res);
            abort();
        }
        if (ktx->numLayers > image_format_props.maxArrayLayers) {
            spdlog::critical("ktx: image has too many layers ({} > max:{})", ktx->numLayers, image_format_props.maxArrayLayers);
        }
        if (should_generate_mipmaps) {
            VkFormatProperties format_props;
            VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            vkGetPhysicalDeviceFormatProperties(allocator.device().hwd(), m_createinfo.format, &format_props);
            if ((format_props.optimalTilingFeatures & required_features) != required_features) {
                spdlog::critical("ktx: format {} does not support blitting; cannot generate mipmaps as requested", m_createinfo.format);
                abort();
            }
            m_createinfo.mipLevels = count_mip_levels(m_createinfo.extent);
        } else {
            m_createinfo.mipLevels = ktx->numLevels;
        }

        ktx_size_t base_buffer_length = ktxTexture_GetDataSizeUncompressed(ktx);
        create_empty(allocator, vkw::MemoryUsage::HostLocal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, base_buffer_length);

        void* mapped_buffer = allocator.map_memory(m_allocation[0]);
        if ((k_result = ktxTexture_LoadImageData(ktx, (ktx_uint8_t*)mapped_buffer, base_buffer_length)) != KTX_SUCCESS) {
            spdlog::critical("ktxTexture_LoadImageData: {}", ktxErrorString(k_result));
            abort();
        }

        ktx_mip_iterate_userdata ktx_mip_iterate_data(ktx->numLevels, ktx->numFaces * ktx->numLayers);
        if ((k_result = ktxTexture_IterateLevels(ktx, ktx_mip_iterate, &ktx_mip_iterate_data)) != KTX_SUCCESS) {
            spdlog::critical("ktxTexture_IterateLevels: {}", ktxErrorString(k_result));
            abort();
        }
        m_copies = std::move(ktx_mip_iterate_data.regions);

        allocator.unmap_memory(m_allocation[0]);
        ktxTexture_Destroy(ktx);
    } else {
        abort();
    }
}

uint32_t HostImage::count_mip_levels(const VkExtent3D& extent)
{
    uint32_t max_dim = std::max(std::max(extent.width, extent.height), extent.depth);
    return static_cast<uint32_t>(std::floor(std::log2(max_dim))) + 1;
}

HostImage::InputFormat HostImage::input_format(const std::string_view& extension)
{
    if (extension == "png")
        return InputFormat::PNG;
    else if (extension == "ktx2")
        return InputFormat::KTX2;
    else {
        spdlog::critical("HostImage::input_format('{}'): unknown type", extension);
        abort();
    }
}

template <unsigned int N>
Image<N>::Image(Allocator& allocator)
    : m_allocation(allocator)
{
    memset(&m_createinfo, 0, sizeof(VkImageCreateInfo));
    m_createinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    m_handle.fill(VK_NULL_HANDLE);
}

template <unsigned int N>
Image<N>::Image(Allocator& allocator, MemoryUsage mem_usage, VkImageType type, VkImageUsageFlags usage, const VkExtent3D& extent, VkFormat format, int samples, int mip_levels, int layers,
    VkImageTiling tiling, const std::initializer_list<QueueFamilyType>& queue_families, VkImageLayout initial_layout, VkImageCreateFlags flags)
    : Image(allocator)
{
    VkResult res;
    m_createinfo.imageType = type;
    m_createinfo.usage = usage;
    m_createinfo.format = format;
    m_createinfo.extent = extent;
    m_createinfo.samples = static_cast<VkSampleCountFlagBits>(samples);
    m_createinfo.mipLevels = mip_levels;
    m_createinfo.arrayLayers = layers;
    m_createinfo.tiling = tiling;
    m_createinfo.initialLayout = initial_layout;
    m_createinfo.flags = flags;
    m_mem_usage = mem_usage;
    if (queue_families.size() > 0) {
        std::vector<uint32_t> qfv = unique_queue_families(device(), queue_families);
        m_createinfo.sharingMode = qfv.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
        m_createinfo.queueFamilyIndexCount = qfv.size();
        m_createinfo.pQueueFamilyIndices = qfv.data();
    } else {
        m_createinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    for (int i = 0; i < N; i++) {
        if ((res = vkCreateImage(device(), &m_createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }
        if (allocator.allocate(m_handle[i], m_mem_usage, m_allocation[i]) == false) {
            spdlog::critical("could not allocate memory for image");
            abort();
        }
    }
}

template <>
Image<1>::Image(HostImage& src_image, MemoryUsage mem_usage, VkImageUsageFlags usage, VkImageTiling tiling, const std::initializer_list<QueueFamilyType>& queue_families, VkImageCreateFlags flags)
    : m_createinfo(src_image.m_createinfo)
    , m_mem_usage(mem_usage)
    , m_allocation(src_image.m_allocation.allocator())
{
    VkResult res;
    Allocator& allocator = m_allocation.allocator();
    const Device& device = allocator.device();

    m_createinfo.usage |= usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    m_createinfo.tiling = tiling;
    if (queue_families.size() > 0) {
        std::vector<uint32_t> qfv = unique_queue_families(device, queue_families);
        m_createinfo.sharingMode = qfv.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
        m_createinfo.queueFamilyIndexCount = qfv.size();
        m_createinfo.pQueueFamilyIndices = qfv.data();
    }

    if (src_image.m_copies.size() < m_createinfo.mipLevels) {
        // eventually, mipmaps will be generated
        m_createinfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    m_mem_usage = mem_usage;
    if ((res = vkCreateImage(device, &m_createinfo, nullptr, &m_handle[0])) != VK_SUCCESS) {
        spdlog::critical("vkCreateImage: {}", res);
        abort();
    }
    if (allocator.allocate(m_handle[0], m_mem_usage, m_allocation[0]) == false) {
        spdlog::critical("could not allocate memory for image");
        abort();
    }
}

template <unsigned int N>
Image<N>::Image(HostImage& src_image, MemoryUsage mem_usage, VkImageUsageFlags usage, VkImageTiling tiling, const std::initializer_list<QueueFamilyType>& queue_families, VkImageCreateFlags flags)
    : m_allocation(src_image.m_allocation.allocator())
{
    spdlog::critical("Image<{}>::Image(HostImage&, MemoryUsage): invalid usage", N);
    abort();
}

template <unsigned int N>
Image<N>::~Image()
{
    m_allocation.free();
    for (VkImage& image : m_handle)
        vkDestroyImage(device(), image, nullptr);
}

template <unsigned int N>
void Image<N>::copy_from(HostImage& src_image, CommandBuffer& cmd)
{
    VkImageSubresourceRange subresource;
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.baseMipLevel = subresource.baseArrayLayer = 0;
    subresource.levelCount = m_createinfo.mipLevels;
    subresource.layerCount = m_createinfo.arrayLayers;

    for (int i = 0; i < N; i++) {
        cmd.set_image_layout(m_handle[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource);
        vkCmdCopyBufferToImage(cmd, src_image.m_handle[0], m_handle[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, src_image.m_copies.size(), src_image.m_copies.data());
    }

    if (src_image.m_copies.size() < m_createinfo.mipLevels) {
        generate_mipmaps(cmd, src_image.m_copies.size(), m_createinfo.mipLevels, m_createinfo.extent, m_createinfo.arrayLayers);
    }
}

template <unsigned int N>
void Image<N>::generate_mipmaps(CommandBuffer& cmd, uint32_t mip_start, uint32_t mip_end, const VkExtent3D& extent, uint32_t layer_count, VkImageLayout initial_layout, VkImageLayout final_layout)
{
    for (VkImage& image : m_handle) {
        VkImageSubresourceRange subresource {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.baseMipLevel = mip_start - 1;
        subresource.levelCount = 1;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = layer_count;
        cmd.set_image_layout(image, initial_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource);

        for (uint32_t i = mip_start; i < mip_end; i++) {
            VkImageBlit image_blit {};
            image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_blit.srcSubresource.layerCount = layer_count;
            image_blit.srcSubresource.mipLevel = i - 1;
            image_blit.srcOffsets[0] = { 0, 0, 0 };
            image_blit.srcOffsets[1].x = std::max(1U, extent.width >> (i - 1));
            image_blit.srcOffsets[1].y = std::max(1U, extent.height >> (i - 1));
            image_blit.srcOffsets[1].z = std::max(1U, extent.depth >> (i - 1));
            image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_blit.dstSubresource.layerCount = layer_count;
            image_blit.dstSubresource.mipLevel = i;
            image_blit.dstOffsets[0] = { 0, 0, 0 };
            image_blit.dstOffsets[1].x = std::max(1U, extent.width >> i);
            image_blit.dstOffsets[1].y = std::max(1U, extent.height >> i);
            image_blit.dstOffsets[1].z = std::max(1U, extent.depth >> i);

            subresource.baseMipLevel = i;
            subresource.levelCount = 1;

            cmd.set_image_layout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource);
            vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_LINEAR);
            cmd.set_image_layout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource);
        }

        subresource.baseMipLevel = 0;
        subresource.levelCount = mip_end;
        cmd.set_image_layout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, final_layout, subresource);
    }
}

template <unsigned int N>
void Image<N>::resize(const VkExtent3D& new_extent)
{
    VkResult res;
    m_createinfo.extent = new_extent;
    for (int i = 0; i < N; i++) {
        if ((res = vkCreateImage(device(), &m_createinfo, nullptr, &m_handle[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }
    }

    if (m_allocation.allocate(*this, m_mem_usage) == false) {
        spdlog::critical("Image<{}>::resize({}, {}, {}): failed to reallocate", N, new_extent.width, new_extent.height, new_extent.depth);
        abort();
    }
}

template <unsigned int N>
void Image<N>::set_layout(VkImageLayout from, VkImageLayout to, CommandBuffer& cmd, VkImageAspectFlags aspect_flags, VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask)
{
    VkImageSubresourceRange subresource {};
    subresource.aspectMask = aspect_flags;
    subresource.baseArrayLayer = subresource.baseMipLevel = 0;
    subresource.levelCount = m_createinfo.mipLevels;
    subresource.layerCount = m_createinfo.arrayLayers;

    for (VkImage& image : m_handle) {
        cmd.set_image_layout(image, from, to, subresource, src_stage_mask, dst_stage_mask);
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

constexpr size_t FormatWidth(VkFormat fmt)
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
