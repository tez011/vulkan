#define VK_ENABLE_BETA_EXTENSIONS
#include "vkw/vkw.h"
#include <set>
#include <spdlog/spdlog.h>

#ifdef NDEBUG
#define ENABLE_VALIDATION_LAYERS 0
constexpr static const char** INSTANCE_LAYERS = nullptr;
constexpr static uint32_t INSTANCE_LAYERS_COUNT = 0;
#else
#define ENABLE_VALIDATION_LAYERS 1
constexpr static const char* INSTANCE_LAYERS[] = { "VK_LAYER_KHRONOS_validation" };
constexpr static uint32_t INSTANCE_LAYERS_COUNT = 1;
#endif

namespace vkw {
PFN_vkDestroyDebugUtilsMessengerEXT Device::s_vkDestroyDebugUtilsMessenger = nullptr;

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* cb_data,
    void* user_data)
{
    spdlog::level::level_enum which_level;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        which_level = spdlog::level::err;
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        which_level = spdlog::level::warn;
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        which_level = (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? spdlog::level::info : spdlog::level::debug;
    else
        which_level = spdlog::level::debug;

    spdlog::log(which_level, "[vulkan] {}", cb_data->pMessage);
    return VK_FALSE;
}

Device::Device(GLFWwindow* window)
    : m_window(window)
{
    VkResult res;

    // create instance
    {
        uint32_t n;
        const char** exts = glfwGetRequiredInstanceExtensions(&n);
        std::vector<const char*> instance_extensions(exts, exts + n);
        if (ENABLE_VALIDATION_LAYERS)
            instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkInstanceCreateFlags instance_create_flags = 0;
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

        VkApplicationInfo appinfo {};
        appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appinfo.apiVersion = VK_API_VERSION;

        VkInstanceCreateInfo createinfo {};
        createinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createinfo.flags = instance_create_flags;
        createinfo.pApplicationInfo = &appinfo;
        createinfo.enabledLayerCount = INSTANCE_LAYERS_COUNT;
        createinfo.ppEnabledLayerNames = INSTANCE_LAYERS;
        createinfo.enabledExtensionCount = instance_extensions.size();
        createinfo.ppEnabledExtensionNames = instance_extensions.data();
        if ((res = vkCreateInstance(&createinfo, nullptr, &m_instance)) != VK_SUCCESS) {
            spdlog::critical("vkCreateInstance: {}", res);
            abort();
        }
    }

    // create debug messenger
    if (ENABLE_VALIDATION_LAYERS) {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)
            glfwGetInstanceProcAddress(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (s_vkDestroyDebugUtilsMessenger == nullptr)
            s_vkDestroyDebugUtilsMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
                glfwGetInstanceProcAddress(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (create == nullptr || s_vkDestroyDebugUtilsMessenger == nullptr) {
            spdlog::critical("extension " VK_EXT_DEBUG_UTILS_EXTENSION_NAME " not present");
            abort();
        }

        VkDebugUtilsMessengerCreateInfoEXT createinfo {};
        createinfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createinfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createinfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createinfo.pfnUserCallback = vk_debug_callback;
        res = create(m_instance, &createinfo, nullptr, &m_debug_messenger);
        if (res != VK_SUCCESS) {
            spdlog::critical("vkCreateDebugUtilsMessengerEXT: {}", res);
            abort();
        }
    } else {
        m_debug_messenger = VK_NULL_HANDLE;
    }

    res = glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface);
    if (res != VK_SUCCESS) {
        spdlog::critical("glfwCreateWindowSurface: {}", res);
        abort();
    }

    m_hwd = pick_physical_device();
    if (m_hwd == VK_NULL_HANDLE) {
        spdlog::critical("no usable physical devices were found");
        abort();
    }
    create_logical_device();
    m_swapchain = std::make_unique<Swapchain>(m_device, m_surface, m_window, m_hwd);
}

Device::~Device()
{
    vkDeviceWaitIdle(m_device);

    m_swapchain.reset();
    vkDestroyDevice(m_device, nullptr);
    if (m_debug_messenger != VK_NULL_HANDLE)
        s_vkDestroyDebugUtilsMessenger(m_instance, m_debug_messenger, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    glfwDestroyWindow(m_window);
}

VkPhysicalDevice Device::pick_physical_device()
{
    uint32_t device_count = 0;
    std::vector<VkPhysicalDevice> devices;
    vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
    devices.resize(device_count);
    vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());
    for (auto& device : devices) {
        VkPhysicalDeviceProperties device_props;
        vkGetPhysicalDeviceProperties(device, &device_props);

        uint32_t qfprop_count;
        std::vector<VkQueueFamilyProperties> qfprop;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qfprop_count, nullptr);
        qfprop.resize(qfprop_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qfprop_count, qfprop.data());
        bool has_good_queue = false;
        for (uint32_t i = 0; i < qfprop_count; i++) {
            const auto& queue_family = qfprop[i];
            if ((queue_family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                VkBool32 present_support = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &present_support);
                if (present_support)
                    has_good_queue = true;
            }
        }
        if (has_good_queue == false) {
            spdlog::debug("{}: skipping: no queue family supports graphics, compute, and presentation", device_props.deviceName);
            continue;
        }

        uint32_t available_ext_count = 0;
        std::vector<VkExtensionProperties> available_exts;
        bool has_portability_subset = false;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_ext_count, nullptr);
        available_exts.resize(available_ext_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_ext_count, available_exts.data());
        std::set<std::string> required_exts = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        for (const auto& ext : available_exts) {
            if (strcmp(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, ext.extensionName) == 0)
                has_portability_subset = true;
            auto it = required_exts.find(ext.extensionName);
            if (it != required_exts.end())
                required_exts.erase(it);
        }
        if (!required_exts.empty()) {
            for (const auto& missing_ext : required_exts)
                spdlog::critical("{}: skipping: missing required extension {}", device_props.deviceName, missing_ext);
            continue;
        }

        VkPhysicalDevicePortabilitySubsetFeaturesKHR portability_features {};
        portability_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
        VkPhysicalDeviceVulkan12Features available_features12 {};
        available_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        available_features12.pNext = has_portability_subset ? &portability_features : nullptr;
        VkPhysicalDeviceVulkan11Features available_features11 {};
        available_features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        available_features11.pNext = &available_features12;
        VkPhysicalDeviceFeatures2 available_features {}, features {};
        available_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        available_features.pNext = &available_features11;
        vkGetPhysicalDeviceFeatures2(device, &available_features);

#define DEMAND_FEATURE(STRUCTURE, FIELD)                                                                   \
    if (STRUCTURE.FIELD == VK_FALSE) {                                                                     \
        spdlog::debug("{}: skipping: required feature " #FIELD " not available", device_props.deviceName); \
        continue;                                                                                          \
    }
        DEMAND_FEATURE(available_features.features, depthClamp);
        DEMAND_FEATURE(available_features.features, sampleRateShading);
        if (has_portability_subset) {
            DEMAND_FEATURE(portability_features, constantAlphaColorBlendFactors);
            DEMAND_FEATURE(portability_features, events);
        }
#undef DEMAND_FEATURE

        uint32_t surface_format_count, surface_present_mode_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &surface_format_count, nullptr);
        if (surface_format_count == 0) {
            spdlog::debug("{}: skipping: no supported surface formats", device_props.deviceName);
            continue;
        }

        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &surface_present_mode_count, nullptr);
        if (surface_present_mode_count == 0) {
            spdlog::debug("{}: skipping: no supported surface present modes", device_props.deviceName);
            continue;
        }

        return device;
    }
    return VK_NULL_HANDLE;
}

static void fill_queue_priorities(std::vector<float>& priorities, size_t queue_count)
{
    priorities.assign(queue_count, 0.f);
    priorities[0] = 1.f;
    if (queue_count > 4)
        priorities[1] = 1.f;
    if (queue_count > 8)
        priorities[2] = 1.f;
    if (queue_count > 15)
        priorities[3] = 1.f;
}

static float* allocate_queue_properties(size_t count)
{
    float* f = new float[count];
    memset(f, 0, sizeof(float) * count);
    f[0] = 1.f;
    if (count > 4)
        f[1] = 1.f;
    return f;
}

std::vector<VkDeviceQueueCreateInfo> Device::describe_device_queues()
{
    VkResult res;
    uint32_t qfprop_count;
    std::vector<VkQueueFamilyProperties> qfprop;
    std::pair<int32_t, size_t> best_transfer_queue(-1, std::numeric_limits<size_t>::max());
    vkGetPhysicalDeviceQueueFamilyProperties(m_hwd, &qfprop_count, nullptr);
    qfprop.resize(qfprop_count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_hwd, &qfprop_count, qfprop.data());

    int32_t combined_queue = -1, async_compute_queue = -1;
    for (uint32_t i = 0; i < qfprop_count && combined_queue == -1; i++) {
        if ((qfprop[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            VkBool32 present_support = false;
            if ((res = vkGetPhysicalDeviceSurfaceSupportKHR(m_hwd, i, m_surface, &present_support)) != VK_SUCCESS) {
                spdlog::critical("vkGetPhysicalDeviceSurfaceSupportKHR({}): {}", i, res);
                abort();
            }
            if (present_support)
                combined_queue = i;
        }
    }
    for (uint32_t i = 0; i < qfprop_count && async_compute_queue == -1; i++) {
        if ((qfprop[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 && (qfprop[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
            async_compute_queue = i;
    }
    for (uint32_t i = 0; i < qfprop_count && async_compute_queue == -1; i++) {
        if ((qfprop[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 && (qfprop[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
            async_compute_queue = i;
    }
    for (uint32_t i = 0; i < qfprop_count; i++) {
        if (i == combined_queue || i == async_compute_queue)
            continue;
        if (qfprop[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            size_t granularity = qfprop[i].minImageTransferGranularity.width * qfprop[i].minImageTransferGranularity.height;
            if (granularity < best_transfer_queue.second)
                best_transfer_queue = { i, granularity };
        }
    }

    std::vector<VkDeviceQueueCreateInfo> createinfos;
    VkDeviceQueueCreateInfo combined_ci {};
    combined_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    combined_ci.queueFamilyIndex = combined_queue;
    combined_ci.queueCount = qfprop[combined_queue].queueCount;
    combined_ci.pQueuePriorities = allocate_queue_properties(combined_ci.queueCount);
    createinfos.push_back(combined_ci);
    if (async_compute_queue != -1) {
        VkDeviceQueueCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = async_compute_queue;
        ci.queueCount = std::min(4U, qfprop[async_compute_queue].queueCount);
        ci.pQueuePriorities = allocate_queue_properties(ci.queueCount);
        createinfos.push_back(ci);
    }
    if (best_transfer_queue.first != -1) {
        VkDeviceQueueCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = best_transfer_queue.first;
        ci.queueCount = std::min(4U, qfprop[best_transfer_queue.first].queueCount);
        ci.pQueuePriorities = allocate_queue_properties(ci.queueCount);
        createinfos.push_back(ci);
    }

    m_queue_family_indexes.combined = combined_queue;
    m_queue_family_indexes.combined_count = combined_queue == -1 ? 0 : qfprop[combined_queue].queueCount;
    m_queue_family_indexes.compute = async_compute_queue;
    m_queue_family_indexes.compute_count = async_compute_queue == -1 ? 0 : qfprop[async_compute_queue].queueCount;
    m_queue_family_indexes.transfer = best_transfer_queue.first;
    m_queue_family_indexes.combined_count = best_transfer_queue.first == -1 ? 0 : qfprop[best_transfer_queue.first].queueCount;
    return createinfos;
}

void Device::create_logical_device()
{
    uint32_t available_ext_count = 0;
    std::vector<VkExtensionProperties> available_exts;
    std::vector<const char*> extensions;
    vkEnumerateDeviceExtensionProperties(m_hwd, nullptr, &available_ext_count, nullptr);
    available_exts.resize(available_ext_count);
    vkEnumerateDeviceExtensionProperties(m_hwd, nullptr, &available_ext_count, available_exts.data());
    for (auto& ext : available_exts) {
        if (strcmp(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME, ext.extensionName) == 0)
            extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, ext.extensionName) == 0)
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(m_hwd, &properties);
    spdlog::info("selecting device {}", properties.deviceName);

    VkPhysicalDeviceVulkan12Features available_features12 {};
    available_features12.sType = m_device_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan11Features available_features11 {};
    available_features11.sType = m_device_features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    available_features11.pNext = &available_features12;
    m_device_features11.pNext = &m_device_features12;
    VkPhysicalDeviceFeatures2 available_features {};
    available_features.sType = m_device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    available_features.pNext = &available_features11;
    m_device_features.pNext = &m_device_features11;
    vkGetPhysicalDeviceFeatures2(m_hwd, &available_features);

    m_device_features.features.depthClamp = true;
    m_device_features.features.sampleRateShading = true;
    // Enable features in features{,11,12} if they're on in available_features{,11,12}.

    std::vector<VkDeviceQueueCreateInfo> queue_createinfos = describe_device_queues();
    VkDeviceCreateInfo createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createinfo.pNext = &m_device_features;
    createinfo.queueCreateInfoCount = queue_createinfos.size();
    createinfo.pQueueCreateInfos = queue_createinfos.data();
    createinfo.enabledExtensionCount = extensions.size();
    createinfo.ppEnabledExtensionNames = extensions.data();
    createinfo.pEnabledFeatures = nullptr;

    VkResult res = vkCreateDevice(m_hwd, &createinfo, nullptr, &m_device);
    if (res != VK_SUCCESS) {
        spdlog::critical("vkCreateDevice: {}", res);
        abort();
    }

    // clean up after describe_device_queues()
    for (auto& ci : queue_createinfos)
        delete[] ci.pQueuePriorities;
}

int32_t Device::queue_family_index(QueueFamilyType t) const
{
    switch (t) {
    case QueueFamilyType::Combined:
    case QueueFamilyType::Graphics:
    case QueueFamilyType::Compute:
        return m_queue_family_indexes.combined;
    case QueueFamilyType::AsyncCompute:
        return m_queue_family_indexes.compute;
    case QueueFamilyType::Transfer:
        if (m_queue_family_indexes.transfer == -1)
            return m_queue_family_indexes.combined;
        else
            return m_queue_family_indexes.transfer;
    default:
        return -1;
    }
}

uint32_t Device::queue_count(QueueFamilyType t) const
{
    switch (t) {
    case QueueFamilyType::Combined:
    case QueueFamilyType::Graphics:
    case QueueFamilyType::Compute:
        return m_queue_family_indexes.combined_count;
    case QueueFamilyType::AsyncCompute:
        return m_queue_family_indexes.compute_count;
    case QueueFamilyType::Transfer:
        if (m_queue_family_indexes.transfer == -1)
            return m_queue_family_indexes.combined_count;
        else
            return m_queue_family_indexes.transfer_count;
    default:
        return 0;
    }
}

VkQueue Device::queue(QueueFamilyType t, size_t index) const
{
    VkQueue q = VK_NULL_HANDLE;
    uint32_t family_index = queue_family_index(t);
    if (family_index == -1)
        return VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, family_index, index % queue_count(t), &q);
    return q;
}

void Device::acquire_next_image(VkSemaphore ready_signal)
{
    VkResult res = vkAcquireNextImageKHR(m_device, *m_swapchain, std::numeric_limits<uint64_t>::max(), ready_signal, VK_NULL_HANDLE, &m_swapchain_image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        wait_for_window_foreground();

        GarbageCollector retirer;
        std::unique_ptr<Swapchain> tmp_swapchain = std::make_unique<Swapchain>(m_device, m_surface, m_window, m_hwd, *m_swapchain);
        tmp_swapchain.swap(m_swapchain); // m_swapchain is new, tmp_swapchain is the retired handle.
        retirer.add(std::move(tmp_swapchain));
        m_recreate_swapchain_cb(*this, retirer);
        m_retiring.push_back(std::make_pair(std::move(retirer), 3));
    } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        spdlog::critical("vkAcquireNextImageKHR: {}", res);
        abort();
    }
}

QueueSubmission Device::submit_commands() const
{
    return QueueSubmission(*this);
}

void Device::present_image(const std::initializer_list<VkSemaphore>& _wait_sem)
{
    VkSwapchainKHR active_swapchain = *m_swapchain;
    VkPresentInfoKHR present_info {};
    std::vector<VkSemaphore> wait_sem(_wait_sem);
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = wait_sem.size();
    present_info.pWaitSemaphores = wait_sem.data();
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &active_swapchain;
    present_info.pImageIndices = &m_swapchain_image_index;

    VkQueue present_queue;
    vkGetDeviceQueue(m_device, m_queue_family_indexes.combined, 0, &present_queue);
    VkResult res = vkQueuePresentKHR(present_queue, &present_info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        wait_for_window_foreground();

        GarbageCollector retirer;
        std::unique_ptr<Swapchain> tmp_swapchain(new Swapchain(m_device, m_surface, m_window, m_hwd, *m_swapchain));
        tmp_swapchain.swap(m_swapchain);
        retirer.add(std::move(tmp_swapchain));
        m_recreate_swapchain_cb(*this, retirer);
        m_retiring.push_back(std::make_pair(std::move(retirer), 3));
    } else if (res != VK_SUCCESS) {
        spdlog::critical("vkQueuePresentKHR: {}", res);
        abort();
    }

    if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR) {
        auto it = m_retiring.begin();
        while (it != m_retiring.end()) {
            if (it->second > 0)
                (it++)->second -= 1;
            else {
                it->first.retire(*this);
                it = m_retiring.erase(it);
            }
        }
        m_frame_number++;
    }
}

bool Device::wait_for_fences(const std::initializer_list<VkFence>& fences, bool wait_all, uint64_t timeout) const
{
    std::vector<VkFence> owned_fences(fences);
    VkResult res = vkWaitForFences(m_device, owned_fences.size(), owned_fences.data(), wait_all, timeout);
    if (res == VK_SUCCESS)
        return true;
    else if (res == VK_TIMEOUT)
        return false;
    else {
        spdlog::critical("vkWaitForFences(): {}", res);
        abort();
    }
}

void Device::wait_for_window_foreground()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &width, &height);
    }
}

Swapchain::Swapchain(VkDevice device, VkSurfaceKHR surface, GLFWwindow* window, VkPhysicalDevice hwd, VkSwapchainKHR old_swapchain)
    : m_device(device)
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t n = 0;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(hwd, surface, &capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(hwd, surface, &n, nullptr);
    formats.resize(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(hwd, surface, &n, formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(hwd, surface, &n, nullptr);
    present_modes.resize(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(hwd, surface, &n, present_modes.data());

    if (std::any_of(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& fmt) {
            return fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        })) {
        m_surface_format.format = VK_FORMAT_B8G8R8A8_SRGB;
        m_surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    } else {
        m_surface_format = formats[0];
    }

    if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        m_extent.width = std::clamp(static_cast<uint32_t>(w), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        m_extent.height = std::clamp(static_cast<uint32_t>(h), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    } else {
        m_extent = capabilities.currentExtent;
    }

    uint32_t image_count = capabilities.minImageCount + 2;
    if (capabilities.maxImageCount > 0)
        image_count = std::min(image_count, capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createinfo {};
    createinfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createinfo.surface = surface;
    createinfo.minImageCount = image_count;
    createinfo.imageFormat = m_surface_format.format;
    createinfo.imageColorSpace = m_surface_format.colorSpace;
    createinfo.imageExtent = m_extent;
    createinfo.imageArrayLayers = 1;
    createinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // render directly to here. Also common is VK_IMAGE_USAGE_TRANSFER_DST_BIT, but that requires us to blit final image to swapchain.
    createinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createinfo.preTransform = capabilities.currentTransform;
    createinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createinfo.clipped = VK_TRUE;
    createinfo.oldSwapchain = old_swapchain;
    if (std::find(present_modes.begin(), present_modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != present_modes.end())
        createinfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    else
        createinfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkResult res = vkCreateSwapchainKHR(device, &createinfo, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        spdlog::critical("vkCreateSwapchainKHR: {}", res);
        abort();
    }

    vkGetSwapchainImagesKHR(device, m_handle, &image_count, nullptr);
    m_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, m_handle, &image_count, m_images.data());
    m_image_views.resize(image_count);

    for (size_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo image_view_createinfo {};
        image_view_createinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_createinfo.image = m_images[i];
        image_view_createinfo.format = m_surface_format.format;
        image_view_createinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_createinfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_createinfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_createinfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_createinfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_createinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_createinfo.subresourceRange.baseMipLevel = 0;
        image_view_createinfo.subresourceRange.levelCount = 1;
        image_view_createinfo.subresourceRange.baseArrayLayer = 0;
        image_view_createinfo.subresourceRange.layerCount = 1;

        if ((res = vkCreateImageView(device, &image_view_createinfo, nullptr, &m_image_views[i])) != VK_SUCCESS) {
            spdlog::critical("vkCreateImageView: {}", res);
            abort();
        }
    }
}

Swapchain::~Swapchain()
{
    for (auto& image_view : m_image_views)
        vkDestroyImageView(m_device, image_view, nullptr);
    vkDestroySwapchainKHR(m_device, m_handle, nullptr);
}

void GarbageCollector::add(Framebuffer& framebuffer)
{
    m_framebuffers.insert(m_framebuffers.end(), framebuffer.m_handles.begin(), framebuffer.m_handles.end());
}

void GarbageCollector::retire(Device& device)
{
    for (auto& x : m_framebuffers)
        vkDestroyFramebuffer(device, x, nullptr);
    for (auto& x : m_images)
        vkDestroyImage(device, x, nullptr);
    for (auto& x : m_image_views)
        vkDestroyImageView(device, x, nullptr);
    for (auto& x : m_allocations)
        x.free();
}

QueueSubmission& QueueSubmission::wait_on(VkSemaphore sem, VkPipelineStageFlags stage)
{
    m_submits.back().m_wait_sem.push_back(sem);
    m_submits.back().m_wait_stages.push_back(stage);
    return *this;
}

QueueSubmission& QueueSubmission::signal(VkSemaphore sem)
{
    m_submits.back().m_signal_sem.push_back(sem);
    return *this;
}

QueueSubmission& QueueSubmission::add(const CommandBuffer& cb)
{
    m_submits.back().m_command_buffers.push_back(VkCommandBuffer(cb));
    return *this;
}

QueueSubmission& QueueSubmission::next()
{
    m_submits.emplace_back();
    return *this;
}

void QueueSubmission::to_queue(QueueFamilyType ty, size_t index, VkFence signal)
{
    VkQueue queue = m_device.queue(ty, index);
    if (queue == VK_NULL_HANDLE) {
        spdlog::error("QueueSubmission::submit(ty={}, index={}): no such queue", ty, index);
        return;
    }

    std::vector<VkSubmitInfo> submit_info(m_submits.size());
    for (size_t i = 0; i < m_submits.size(); i++) {
        VkSubmitInfo& si = submit_info[i];
        one_submission_t& store = m_submits[i];
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = store.m_wait_sem.size();
        si.pWaitSemaphores = store.m_wait_sem.data();
        si.pWaitDstStageMask = store.m_wait_stages.data();
        si.commandBufferCount = store.m_command_buffers.size();
        si.pCommandBuffers = store.m_command_buffers.data();
        si.signalSemaphoreCount = store.m_signal_sem.size();
        si.pSignalSemaphores = store.m_signal_sem.data();
    }

    VkResult res = vkQueueSubmit(queue, submit_info.size(), submit_info.data(), signal);
    if (res != VK_SUCCESS) {
        spdlog::critical("vkQueueSubmit: {}", res);
        abort();
    }
}

}
