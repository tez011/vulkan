#pragma once

#define GLFW_INCLUDE_VULKAN
#define VK_API_VERSION VK_API_VERSION_1_2

#include <GLFW/glfw3.h>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace vkw {

class CommandBuffer;
class Framebuffer;
template <unsigned int>
class ImageView;
class SingleAllocation;

class Swapchain {
private:
    VkDevice m_device;

    VkSwapchainKHR m_handle;
    VkSurfaceFormatKHR m_surface_format;
    VkExtent2D m_extent;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_image_views;

public:
    Swapchain(VkDevice device, VkSurfaceKHR surface, GLFWwindow* window, VkPhysicalDevice hwd, VkSwapchainKHR old_swapchain = VK_NULL_HANDLE);
    Swapchain(const Swapchain&) = delete;
    ~Swapchain();

    inline operator VkSwapchainKHR() const { return m_handle; }
    inline size_t image_count() const { return m_images.size(); }
    inline VkFormat format() const { return m_surface_format.format; }
    inline uint32_t width() const { return m_extent.width; }
    inline uint32_t height() const { return m_extent.height; }
    inline const VkImageView& image_view(size_t i) const { return m_image_views[i]; }
};

enum class QueueFamilyType {
    Combined,
    Graphics,
    Compute,
    AsyncCompute,
    Transfer,
};

class GarbageCollector {
private:
    std::unique_ptr<Swapchain> m_swapchain;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_image_views;
    std::vector<SingleAllocation> m_allocations;

    void retire(Device& device);

    friend class Device;

public:
    void add(std::unique_ptr<Swapchain>&& s) { m_swapchain = std::move(s); }
    template <unsigned int N>
    void add(Allocation<N>& x)
    {
        m_allocations.insert(m_allocations.end(), x.begin(), x.end());
        x.fill(SingleAllocation());
    }
    template <unsigned int N>
    void add(Image<N>& x)
    {
        for (int i = 0; i < N; i++)
            m_images.push_back(x[i]);
    }
    template <unsigned int N>
    void add(ImageView<N>& x)
    {
        for (int i = 0; i < N; i++)
            m_image_views.push_back(x[i]);
        x.m_handle.fill(VK_NULL_HANDLE);
    }
    void add(Framebuffer& x);
};

class QueueSubmission {
private:
    typedef struct {
        std::vector<VkSemaphore> m_wait_sem, m_signal_sem;
        std::vector<VkPipelineStageFlags> m_wait_stages;
        std::vector<VkCommandBuffer> m_command_buffers;
    } one_submission_t;

    const Device& m_device;
    std::vector<one_submission_t> m_submits;

    friend class Device;

    QueueSubmission(const Device& device)
        : m_device(device)
        , m_submits(1)
    {
    }

public:
    QueueSubmission& wait_on(VkSemaphore sem, VkPipelineStageFlags stage);
    QueueSubmission& signal(VkSemaphore sem);
    QueueSubmission& add(const CommandBuffer& cb);
    QueueSubmission& next();
    void to_queue(QueueFamilyType ty, size_t index, VkFence signal = VK_NULL_HANDLE);
};

class Device {
private:
    static PFN_vkDestroyDebugUtilsMessengerEXT s_vkDestroyDebugUtilsMessenger;
    static constexpr int num_required_device_extensions = 1;

    // borrowed members
    GLFWwindow* m_window;

    // owned members
    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_hwd;
    VkDevice m_device;
    std::unique_ptr<Swapchain> m_swapchain;
    std::deque<std::pair<GarbageCollector, uint32_t>> m_retiring;
    uint32_t m_swapchain_image_index;
    std::atomic_uint32_t m_frame_number = 0;
    std::function<void(const Device&, GarbageCollector&)> m_recreate_swapchain_cb;

    struct {
        int32_t combined, compute, transfer;
        uint32_t combined_count, compute_count, transfer_count;
    } m_queue_family_indexes;

    VkPhysicalDeviceFeatures2 m_device_features {};
    VkPhysicalDeviceVulkan11Features m_device_features11 {};
    VkPhysicalDeviceVulkan12Features m_device_features12 {};

    VkPhysicalDevice pick_physical_device();
    std::vector<VkDeviceQueueCreateInfo> describe_device_queues();
    void create_logical_device();
    void wait_for_window_foreground();

public:
    Device(GLFWwindow*);
    ~Device();

    inline GLFWwindow* window() const { return m_window; }
    inline VkInstance instance() const { return m_instance; }
    inline VkSurfaceKHR surface() const { return m_surface; }
    inline VkPhysicalDevice hwd() const { return m_hwd; }
    inline operator VkDevice() const { return m_device; }
    inline const Swapchain& swapchain() const { return *m_swapchain; }

    inline const VkPhysicalDeviceFeatures& features10() const { return m_device_features.features; }
    inline const VkPhysicalDeviceVulkan11Features& features11() const { return m_device_features11; }
    inline const VkPhysicalDeviceVulkan12Features& features12() const { return m_device_features12; }

    int32_t queue_family_index(QueueFamilyType t) const;
    uint32_t queue_count(QueueFamilyType t) const;
    VkQueue queue(QueueFamilyType t, size_t index = 0) const;

    void acquire_next_image(VkSemaphore ready_signal);
    inline uint32_t current_frame() const { return m_frame_number; }
    inline uint32_t current_frame_image() const { return m_swapchain_image_index; }
    QueueSubmission submit_commands() const;
    void present_image(const std::initializer_list<VkSemaphore>& wait_sem);
    void on_recreate_swapchain(const std::function<void(const Device&, GarbageCollector&)>& cb) { m_recreate_swapchain_cb = cb; }

    bool wait_for_fences(const std::initializer_list<VkFence>& fences, bool wait_all, uint64_t timeout) const;
};

}
