/*

*************************************************************************

Retrocycles -- TRON-aesthetic fork of Armagetron Advanced.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de) and contributors.

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

***************************************************************************

*/

#pragma once

#include "aa_config.h"

#ifndef DEDICATED
#ifdef HAVE_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace vk {

// Owns the Vulkan instance, device, swapchain and per-frame synchronisation.
// One global instance lives in VulkanRenderer.  All resource creation helpers
// the renderer needs (buffers, images, memory) live here so the renderer stays
// focused on translating the rRenderer interface into draw commands.
class VulkanContext {
public:
    VulkanContext()  = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Bring up instance/device/swapchain for the given SDL window (which must
    // have been created with SDL_WINDOW_VULKAN).  Returns false on any failure.
    bool init(SDL_Window* window);
    void shutdown();

    bool isReady() const { return device_ != VK_NULL_HANDLE; }

    // ---- Per-frame lifecycle ----
    // beginFrame acquires a swapchain image, begins the primary command buffer
    // and the render pass (clearing color+depth).  Returns the command buffer to
    // record into, or VK_NULL_HANDLE if the frame should be skipped (e.g. the
    // swapchain is being recreated).
    VkCommandBuffer beginFrame();
    // endFrame ends the render pass, submits and presents.
    void endFrame();

    void requestResize() { framebufferResized_ = true; }

    // ---- Accessors used by VulkanRenderer / pipeline ----
    VkDevice         device()        const { return device_; }
    VkPhysicalDevice physicalDevice()const { return physical_; }
    VkRenderPass     renderPass()    const { return renderPass_; }
    VkExtent2D       extent()        const { return extent_; }
    VkCommandBuffer  currentCmd()    const { return currentCmd_; }
    uint32_t         frameIndex()    const { return currentFrame_; }
    static constexpr uint32_t kFramesInFlight = 2;

    // ---- Debug info (for the --gfx-dbg overlay) ----
    const std::string& deviceName()    const { return deviceName_; }
    uint32_t            apiVersion()    const { return apiVersion_; }
    uint32_t            driverVersion() const { return driverVersion_; }
    VkFormat             swapchainFormat() const { return swapchainFormat_; }

    // ---- Resource helpers ----
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    // Create a buffer + backing memory.  Caller owns destruction.
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory) const;

    // One-shot command buffer for transfers/layout transitions.
    VkCommandBuffer beginSingleTimeCommands() const;
    void            endSingleTimeCommands(VkCommandBuffer cmd) const;

private:
    bool createInstance();
    bool createSurface(SDL_Window* window);
    bool pickPhysicalDevice();
    void captureDeviceInfo(const VkPhysicalDeviceProperties& props);
    bool createLogicalDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createDepthResources();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandResources();
    bool createSyncObjects();

    void destroySwapchain();
    bool recreateSwapchain();

    SDL_Window*      window_   = nullptr;

    VkInstance       instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_  = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice         device_   = VK_NULL_HANDLE;

    std::string      deviceName_;
    uint32_t         apiVersion_    = 0;
    uint32_t         driverVersion_ = 0;

    uint32_t         graphicsQueueFamily_ = 0;
    uint32_t         presentQueueFamily_  = 0;
    VkQueue          graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue          presentQueue_  = VK_NULL_HANDLE;

    VkSwapchainKHR   swapchain_ = VK_NULL_HANDLE;
    VkFormat         swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent_ = {0, 0};
    std::vector<VkImage>       swapImages_;
    std::vector<VkImageView>   swapViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkFormat         depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage          depthImage_  = VK_NULL_HANDLE;
    VkDeviceMemory   depthMemory_ = VK_NULL_HANDLE;
    VkImageView      depthView_   = VK_NULL_HANDLE;

    VkRenderPass     renderPass_ = VK_NULL_HANDLE;
    VkCommandPool    commandPool_ = VK_NULL_HANDLE;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore>     imageAvailable_;
    std::vector<VkSemaphore>     renderFinished_;
    std::vector<VkFence>         inFlight_;

    uint32_t currentFrame_ = 0;
    uint32_t imageIndex_   = 0;
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE;
    bool framebufferResized_ = false;
};

} // namespace vk

#endif // HAVE_VULKAN
#endif // DEDICATED
