#define VK_ENABLE_BETA_EXTENSIONS
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <set>
#include <spdlog/spdlog.h>
#include <spng.h>
#include <vulkan/vulkan.h>

VkInstance create_instance()
{
    VkResult res;
    VkInstance instance;
    VkApplicationInfo appinfo {};
    appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appinfo.pApplicationName = "mipmap generator";
    appinfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t n = 0;
    VkInstanceCreateFlags instance_create_flags = 0;
    std::vector<const char*> instance_extensions;
    std::vector<VkExtensionProperties> available_instance_extensions;
    if ((res = vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr)) != VK_SUCCESS) {
        spdlog::critical("vkEnumerateInstanceExtensionProperties: {}", res);
        abort();
    }
    available_instance_extensions.resize(n);
    if ((res = vkEnumerateInstanceExtensionProperties(nullptr, &n, available_instance_extensions.data())) != VK_SUCCESS) {
        spdlog::critical("vkEnumerateInstanceExtensionProperties: {}", res);
        abort();
    }
    for (auto& ext : available_instance_extensions) {
        if (strcmp(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, ext.extensionName) == 0) {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            instance_create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }

    VkInstanceCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createinfo.flags = instance_create_flags;
    createinfo.pApplicationInfo = &appinfo;
    createinfo.enabledLayerCount = 0;
    createinfo.enabledExtensionCount = instance_extensions.size();
    createinfo.ppEnabledExtensionNames = instance_extensions.data();
    if ((res = vkCreateInstance(&createinfo, nullptr, &instance)) != VK_SUCCESS) {
        spdlog::critical("vkCreateInstance: {}", res);
        abort();
    }
    return instance;
}

VkPhysicalDevice pick_physical_device(VkInstance instance, uint32_t& graphics_queue_index)
{
    uint32_t device_count = 0;
    std::vector<VkPhysicalDevice> devices;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    devices.resize(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    for (auto& device : devices) {
        VkPhysicalDeviceProperties device_props;
        vkGetPhysicalDeviceProperties(device, &device_props);

        uint32_t qfprop_count;
        bool graphics_queue_found = false;
        std::vector<VkQueueFamilyProperties> qfprop;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qfprop_count, nullptr);
        qfprop.resize(qfprop_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qfprop_count, qfprop.data());
        for (uint32_t i = 0; i < qfprop_count; i++) {
            if (qfprop[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphics_queue_index = i;
                graphics_queue_found = true;
                return device;
            }
        }
        spdlog::debug("{}: skipping: no queue family supports graphics", device_props.deviceName);
    }
    return VK_NULL_HANDLE;
}

VkDevice create_logical_device(VkInstance instance, VkPhysicalDevice hwd, uint32_t graphics_queue_index)
{
    uint32_t available_ext_count = 0;
    std::vector<VkExtensionProperties> available_exts;
    std::vector<const char*> extensions;
    vkEnumerateDeviceExtensionProperties(hwd, nullptr, &available_ext_count, nullptr);
    available_exts.resize(available_ext_count);
    vkEnumerateDeviceExtensionProperties(hwd, nullptr, &available_ext_count, available_exts.data());
    for (auto& ext : available_exts) {
        if (strcmp(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, ext.extensionName) == 0)
            extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    float one = 1;
    VkDeviceQueueCreateInfo queue_createinfo {};
    queue_createinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_createinfo.queueFamilyIndex = graphics_queue_index;
    queue_createinfo.queueCount = 1;
    queue_createinfo.pQueuePriorities = &one;

    VkDeviceCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createinfo.queueCreateInfoCount = 1;
    createinfo.pQueueCreateInfos = &queue_createinfo;
    createinfo.enabledExtensionCount = extensions.size();
    createinfo.ppEnabledExtensionNames = extensions.data();
    createinfo.pEnabledFeatures = nullptr;

    VkDevice device;
    VkResult res = vkCreateDevice(hwd, &createinfo, nullptr, &device);
    if (res != VK_SUCCESS) {
        spdlog::critical("vkCreateDevice: {}", res);
        abort();
    }
    return device;
}

VkCommandPool create_command_pool(VkDevice device, uint32_t graphics_queue_family_index)
{
    VkResult res;
    VkCommandPool pool;
    VkCommandPoolCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createinfo.queueFamilyIndex = graphics_queue_family_index;
    createinfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if ((res = vkCreateCommandPool(device, &createinfo, nullptr, &pool)) != VK_SUCCESS) {
        spdlog::critical("vkCreateCommandPool: {}", res);
        abort();
    }
    return pool;
}

void transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, VkCommandBuffer cbuffer)
{
    VkImageMemoryBarrier barrier {};
    VkPipelineStageFlags src_stage, dst_stage;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = barrier.subresourceRange.levelCount = 1;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        spdlog::critical("unsupported layout transition");
        abort();
    }

    vkCmdPipelineBarrier(cbuffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void generate_mipmaps(VkImage image, VkImage atlas, VkFormat image_format, const VkExtent2D& image_extent, uint32_t mip_levels, VkCommandBuffer cbuffer)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mip_position = 0;
    int32_t mip_width = image_extent.width, mip_height = image_extent.height;
    for (uint32_t i = 1; i < mip_levels; i++) {
        // The last mip-level needs to be a transfer source.
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit {};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mip_width, mip_height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { std::max(mip_width / 2, 1), std::max(mip_height / 2, 1), 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        vkCmdBlitImage(cbuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        VkImageCopy copy {};
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.mipLevel = i;
        copy.srcSubresource.baseArrayLayer = 0;
        copy.srcSubresource.layerCount = 1;
        copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.dstSubresource.mipLevel = 0;
        copy.dstSubresource.baseArrayLayer = 0;
        copy.dstSubresource.layerCount = 1;
        copy.srcOffset = { 0, 0, 0 };
        copy.dstOffset = { 0, mip_position, 0 };
        copy.extent.width = blit.dstOffsets[1].x;
        copy.extent.height = blit.dstOffsets[1].y;
        copy.extent.depth = blit.dstOffsets[1].z;
        spdlog::debug("write mipmap of size {},{} to {}", copy.extent.width, copy.extent.height, mip_position);
        vkCmdCopyImage(cbuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        mip_position += blit.dstOffsets[1].y;
        if (mip_width > 1)
            mip_width /= 2;
        if (mip_height > 1)
            mip_height /= 2;
    }

    // the last mip-level is not yet a transfer source
    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

void submit_and_wait(VkQueue queue, VkCommandBuffer cbuffer)
{
    VkResult res;
    VkSubmitInfo submitinfo {};
    submitinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitinfo.commandBufferCount = 1;
    submitinfo.pCommandBuffers = &cbuffer;
    if ((res = vkQueueSubmit(queue, 1, &submitinfo, VK_NULL_HANDLE)) != VK_SUCCESS) {
        spdlog::critical("vkQueueSubmit: {}", res);
        abort();
    }
    vkQueueWaitIdle(queue);
}

uint32_t find_memory_type(VkPhysicalDevice hwd, uint32_t bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memprops;
    vkGetPhysicalDeviceMemoryProperties(hwd, &memprops);
    for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
        if ((bits & 1) == 1) {
            if ((memprops.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        bits >>= 1;
    }
    return 0;
}

void decode_png(const std::string& in_file, std::unique_ptr<char[]>& out_data, size_t& out_length, VkExtent2D& out_extent)
{
    int res = 0;
    spng_ctx* ctx = spng_ctx_new(0);
    FILE* pngf = fopen(in_file.c_str(), "rb");
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    spng_set_chunk_limits(ctx, 1 << 26, 1 << 26);
    if ((res = spng_set_png_file(ctx, pngf)) != 0) {
        spdlog::critical("spng_set_png_file: {}", spng_strerror(res));
        abort();
    }
    struct spng_ihdr ihdr;
    if ((res = spng_get_ihdr(ctx, &ihdr)) != 0) {
        spdlog::critical("spng_get_ihdr: {}", spng_strerror(res));
        abort();
    }
    out_extent.width = ihdr.width;
    out_extent.height = ihdr.height;
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
    fclose(pngf);
}

void encode_png(const std::string& out_file, const void* image, size_t length, size_t width, size_t height)
{
    int res = 0;
    spng_ctx* ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    FILE* pngf = fopen(out_file.c_str(), "wb");
    if ((res = spng_set_png_file(ctx, pngf)) != 0) {
        spdlog::critical("spng_set_png_file: {}", spng_strerror(res));
        abort();
    }

    struct spng_ihdr ihdr { };
    ihdr.width = width;
    ihdr.height = height;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.bit_depth = 8;
    spng_set_ihdr(ctx, &ihdr);
    if ((res = spng_encode_image(ctx, image, length, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE)) != 0) {
        spdlog::critical("spng_encode_image: {}", spng_strerror(res));
        abort();
    }

    spng_ctx_free(ctx);
    fclose(pngf);
}

int main(int argc, char** argv)
{
    std::string in_image, out_image;
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " input_image.png [input_image.mipdata.png]" << std::endl;
        return 1;
    }
    in_image = argv[1];
    if (argc == 2) {
        out_image = in_image.substr(0, in_image.length() - 4) + ".mipdata.png";
        spdlog::info("output: {}", out_image);
    } else {
        out_image = argv[2];
    }

    VkResult res;
    VkInstance instance = create_instance();
    uint32_t queue_family_index;
    VkPhysicalDevice hwd = pick_physical_device(instance, queue_family_index);
    if (hwd == VK_NULL_HANDLE) {
        spdlog::critical("no usable physical devices were found");
        abort();
    }

    VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB;
    {
        VkFormatProperties format_props;
        vkGetPhysicalDeviceFormatProperties(hwd, image_format, &format_props);
        if (!(format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            spdlog::critical("VK_FORMAT_R8G8B8A8_SRGB does not support linear blitting! get a better dev box!");
            abort();
        }
    }

    VkDevice device = create_logical_device(instance, hwd, queue_family_index);
    VkQueue queue;
    vkGetDeviceQueue(device, queue_family_index, 0, &queue);
    VkCommandPool command_pool = create_command_pool(device, queue_family_index);

    VkDeviceMemory host_mem, device_mem;
    VkMemoryRequirements mem_reqs[4];
    VkBuffer host_image, host_mipmap;
    VkImage device_image, device_mipmap;
    VkExtent2D image_extent;
    uint32_t mip_levels;
    {
        std::unique_ptr<char[]> image_data;
        size_t image_length;
        decode_png(in_image, image_data, image_length, image_extent);
        mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(image_extent.width, image_extent.height)))) + 1;

        VkBufferCreateInfo bufferinfo {};
        bufferinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferinfo.size = image_length;
        bufferinfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ((res = vkCreateBuffer(device, &bufferinfo, nullptr, &host_image)) != VK_SUCCESS) {
            spdlog::critical("vkCreateBuffer: {}", res);
            abort();
        }

        bufferinfo.size = image_length / 2;
        bufferinfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ((res = vkCreateBuffer(device, &bufferinfo, nullptr, &host_mipmap)) != VK_SUCCESS) {
            spdlog::critical("vkCreateBuffer: {}", res);
            abort();
        }

        VkImageCreateInfo imageinfo {};
        imageinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageinfo.imageType = VK_IMAGE_TYPE_2D;
        imageinfo.format = image_format;
        imageinfo.extent = { image_extent.width, image_extent.height, 1 };
        imageinfo.mipLevels = mip_levels;
        imageinfo.arrayLayers = 1;
        imageinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageinfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if ((res = vkCreateImage(device, &imageinfo, nullptr, &device_image)) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }

        imageinfo.extent = { image_extent.width / 2, image_extent.height, 1 };
        imageinfo.mipLevels = 1;
        if ((res = vkCreateImage(device, &imageinfo, nullptr, &device_mipmap)) != VK_SUCCESS) {
            spdlog::critical("vkCreateImage: {}", res);
            abort();
        }

        vkGetBufferMemoryRequirements(device, host_image, mem_reqs + 0);
        vkGetBufferMemoryRequirements(device, host_mipmap, mem_reqs + 1);
        vkGetImageMemoryRequirements(device, device_image, mem_reqs + 2);
        vkGetImageMemoryRequirements(device, device_mipmap, mem_reqs + 3);
        assert(mem_reqs[0].memoryTypeBits == mem_reqs[1].memoryTypeBits);
        assert(mem_reqs[2].memoryTypeBits == mem_reqs[3].memoryTypeBits);

        VkMemoryAllocateInfo allocinfo {};
        allocinfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocinfo.allocationSize = mem_reqs[0].size + mem_reqs[1].size;
        allocinfo.memoryTypeIndex = find_memory_type(hwd, mem_reqs[0].memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if ((res = vkAllocateMemory(device, &allocinfo, nullptr, &host_mem)) != VK_SUCCESS) {
            spdlog::critical("vkAllocateMemory: {}", res);
            abort();
        }

        allocinfo.allocationSize = mem_reqs[2].size + mem_reqs[3].size;
        allocinfo.memoryTypeIndex = find_memory_type(hwd, mem_reqs[2].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if ((res = vkAllocateMemory(device, &allocinfo, nullptr, &device_mem)) != VK_SUCCESS) {
            spdlog::critical("vkAllocateMemory: {}", res);
            abort();
        }

        vkBindBufferMemory(device, host_image, host_mem, 0);
        vkBindBufferMemory(device, host_mipmap, host_mem, mem_reqs[0].size);
        vkBindImageMemory(device, device_image, device_mem, 0);
        vkBindImageMemory(device, device_mipmap, device_mem, mem_reqs[2].size);

        void* host_ptr;
        if ((res = vkMapMemory(device, host_mem, 0, mem_reqs[0].size, 0, &host_ptr)) != VK_SUCCESS) {
            spdlog::critical("vkMapMemory: {}", res);
            abort();
        }
        memcpy(host_ptr, image_data.get(), image_length);
        vkUnmapMemory(device, host_mem);
    }

    VkCommandBuffer cbuffer;
    {
        VkCommandBufferAllocateInfo allocinfo {};
        allocinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocinfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocinfo.commandPool = command_pool;
        allocinfo.commandBufferCount = 1;
        if ((res = vkAllocateCommandBuffers(device, &allocinfo, &cbuffer)) != VK_SUCCESS) {
            spdlog::critical("vkAllocateCommandBuffers: {}", res);
            abort();
        }
    }
    VkCommandBufferBeginInfo begininfo {};
    begininfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begininfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cbuffer, &begininfo);
    transition_image_layout(device_image, image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cbuffer);
    transition_image_layout(device_mipmap, image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cbuffer);
    {
        VkBufferImageCopy region {};
        region.bufferOffset = region.bufferRowLength = region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { image_extent.width, image_extent.height, 1 };
        vkCmdCopyBufferToImage(cbuffer, host_image, device_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    {
        VkClearColorValue nocolor;
        memset(&nocolor.float32, 0, sizeof(decltype(nocolor.float32[0])) * 4);
        VkImageSubresourceRange range;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = range.baseArrayLayer = 0;
        range.levelCount = range.layerCount = 1;
        vkCmdClearColorImage(cbuffer, device_mipmap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &nocolor, 1, &range);
    }
    transition_image_layout(device_image, image_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cbuffer);
    vkEndCommandBuffer(cbuffer);
    submit_and_wait(queue, cbuffer);
    vkResetCommandPool(device, command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    vkBeginCommandBuffer(cbuffer, &begininfo);
    generate_mipmaps(device_image, device_mipmap, image_format, image_extent, mip_levels, cbuffer);
    vkEndCommandBuffer(cbuffer);
    submit_and_wait(queue, cbuffer);
    vkResetCommandPool(device, command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    vkBeginCommandBuffer(cbuffer, &begininfo);
    {
        VkBufferImageCopy region {};
        region.bufferOffset = region.bufferImageHeight = region.bufferRowLength = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.baseArrayLayer = region.imageSubresource.mipLevel = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { image_extent.width / 2, image_extent.height, 1 };
        vkCmdCopyImageToBuffer(cbuffer, device_mipmap, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host_mipmap, 1, &region);
    }
    vkEndCommandBuffer(cbuffer);
    submit_and_wait(queue, cbuffer);
    vkResetCommandPool(device, command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    void* mipmap_buffer;
    vkMapMemory(device, host_mem, mem_reqs[0].size, VK_WHOLE_SIZE, 0, &mipmap_buffer);
    encode_png(out_image, mipmap_buffer, image_extent.width * image_extent.height * 2, image_extent.width / 2, image_extent.height);
    vkUnmapMemory(device, host_mem);

    vkFreeCommandBuffers(device, command_pool, 1, &cbuffer);
    vkFreeMemory(device, device_mem, nullptr);
    vkFreeMemory(device, host_mem, nullptr);
    vkDestroyBuffer(device, host_mipmap, nullptr);
    vkDestroyImage(device, device_mipmap, nullptr);
    vkDestroyImage(device, device_image, nullptr);
    vkDestroyBuffer(device, host_image, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
