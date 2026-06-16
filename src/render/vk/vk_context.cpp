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

#include "vk_context.h"

#ifndef DEDICATED
#ifdef HAVE_VULKAN

#include <SDL.h>
#include <SDL_vulkan.h>
#include "tConsole.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace vk {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

#define VK_CHECK(expr, msg)                                          \
    do {                                                             \
        VkResult _r = (expr);                                        \
        if (_r != VK_SUCCESS) {                                      \
            con << "Vulkan: " << msg << " failed (" << int(_r) << ")\n"; \
            return false;                                            \
        }                                                            \
    } while (0)

VulkanContext::~VulkanContext() {
    shutdown();
}

// ---------------------------------------------------------------------------
// instance / surface
// ---------------------------------------------------------------------------

bool VulkanContext::createInstance() {
    unsigned int extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window_, &extCount, nullptr)) {
        con << "Vulkan: SDL_Vulkan_GetInstanceExtensions failed: " << SDL_GetError() << "\n";
        return false;
    }
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window_, &extCount, exts.data());

    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "Retrocycles";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName        = "Armagetron";
    app.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = extCount;
    ci.ppEnabledExtensionNames = exts.data();

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");
    return true;
}

bool VulkanContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance_, &surface_)) {
        con << "Vulkan: SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << "\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// device selection
// ---------------------------------------------------------------------------

bool VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        con << "Vulkan: no physical devices\n";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Prefer a discrete GPU, otherwise take the first device that has both a
    // graphics queue and present support for our surface.
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (VkPhysicalDevice dev : devices) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());

        bool haveGfx = false, havePresent = false;
        uint32_t gfx = 0, present = 0;
        for (uint32_t i = 0; i < qCount; ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { haveGfx = true; gfx = i; }
            VkBool32 sup = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &sup);
            if (sup) { havePresent = true; present = i; }
        }
        if (!haveGfx || !havePresent)
            continue;

        if (fallback == VK_NULL_HANDLE) {
            fallback = dev;
            graphicsQueueFamily_ = gfx;
            presentQueueFamily_  = present;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physical_ = dev;
            graphicsQueueFamily_ = gfx;
            presentQueueFamily_  = present;
            captureDeviceInfo(props);
            return true;
        }
    }

    if (fallback != VK_NULL_HANDLE) {
        physical_ = fallback;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_, &props);
        captureDeviceInfo(props);
        return true;
    }
    con << "Vulkan: no suitable device with graphics+present\n";
    return false;
}

void VulkanContext::captureDeviceInfo(const VkPhysicalDeviceProperties& props) {
    deviceName_ = props.deviceName;
    apiVersion_ = props.apiVersion;
    driverVersion_ = props.driverVersion;
}

bool VulkanContext::createLogicalDevice() {
    std::set<uint32_t> families = { graphicsQueueFamily_, presentQueueFamily_ };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.f;
    for (uint32_t fam : families) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_FALSE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = devExts;
    ci.pEnabledFeatures        = &features;

    VK_CHECK(vkCreateDevice(physical_, &ci, nullptr, &device_), "vkCreateDevice");

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_,  0, &presentQueue_);
    return true;
}

// ---------------------------------------------------------------------------
// swapchain
// ---------------------------------------------------------------------------

bool VulkanContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapchainFormat_ = chosen.format;

    // FIFO is always available (vsync).
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (caps.currentExtent.width != UINT32_MAX) {
        extent_ = caps.currentExtent;
    } else {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(window_, &w, &h);
        extent_.width  = std::clamp<uint32_t>(uint32_t(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent_.height = std::clamp<uint32_t>(uint32_t(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = presentMode;
    ci.clipped          = VK_TRUE;

    uint32_t families[] = { graphicsQueueFamily_, presentQueueFamily_ };
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapImages_.data());
    return true;
}

bool VulkanContext::createImageViews() {
    swapViews_.resize(swapImages_.size());
    for (size_t i = 0; i < swapImages_.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = swapImages_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format   = swapchainFormat_;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device_, &ci, nullptr, &swapViews_[i]), "vkCreateImageView");
    }
    return true;
}

bool VulkanContext::createDepthResources() {
    depthFormat_ = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.extent        = { extent_.width, extent_.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.format        = depthFormat_;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(device_, &ici, nullptr, &depthImage_), "vkCreateImage(depth)");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, depthImage_, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &depthMemory_), "vkAllocateMemory(depth)");
    vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = depthImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = depthFormat_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &depthView_), "vkCreateImageView(depth)");
    return true;
}

bool VulkanContext::createRenderPass() {
    VkAttachmentDescription color{};
    color.format         = swapchainFormat_;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = depthFormat_;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> atts = { color, depth };
    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(atts.size());
    ci.pAttachments    = atts.data();
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_), "vkCreateRenderPass");
    return true;
}

bool VulkanContext::createFramebuffers() {
    framebuffers_.resize(swapViews_.size());
    for (size_t i = 0; i < swapViews_.size(); ++i) {
        std::array<VkImageView, 2> atts = { swapViews_[i], depthView_ };
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = renderPass_;
        ci.attachmentCount = static_cast<uint32_t>(atts.size());
        ci.pAttachments    = atts.data();
        ci.width           = extent_.width;
        ci.height          = extent_.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]), "vkCreateFramebuffer");
    }
    return true;
}

bool VulkanContext::createCommandResources() {
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = graphicsQueueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &commandPool_), "vkCreateCommandPool");

    commandBuffers_.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()), "vkAllocateCommandBuffers");
    return true;
}

bool VulkanContext::createSyncObjects() {
    imageAvailable_.resize(kFramesInFlight);
    renderFinished_.resize(kFramesInFlight);
    inFlight_.resize(kFramesInFlight);

    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &imageAvailable_[i]), "vkCreateSemaphore");
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &renderFinished_[i]), "vkCreateSemaphore");
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &inFlight_[i]), "vkCreateFence");
    }
    return true;
}

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

bool VulkanContext::init(SDL_Window* window) {
    window_ = window;
    if (!createInstance())        return false;
    if (!createSurface(window))   return false;
    if (!pickPhysicalDevice())    return false;
    if (!createLogicalDevice())   return false;
    if (!createSwapchain())       return false;
    if (!createImageViews())      return false;
    if (!createDepthResources())  return false;
    if (!createRenderPass())      return false;
    if (!createFramebuffers())    return false;
    if (!createCommandResources())return false;
    if (!createSyncObjects())     return false;
    con << "Vulkan: context initialised (" << extent_.width << "x" << extent_.height << ")\n";
    return true;
}

void VulkanContext::destroySwapchain() {
    if (device_ == VK_NULL_HANDLE)
        return;
    for (VkFramebuffer fb : framebuffers_)
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();

    if (depthView_)   { vkDestroyImageView(device_, depthView_, nullptr);  depthView_  = VK_NULL_HANDLE; }
    if (depthImage_)  { vkDestroyImage(device_, depthImage_, nullptr);     depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(device_, depthMemory_, nullptr);      depthMemory_= VK_NULL_HANDLE; }

    for (VkImageView v : swapViews_)
        if (v) vkDestroyImageView(device_, v, nullptr);
    swapViews_.clear();

    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

void VulkanContext::shutdown() {
    if (device_ != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device_);

    destroySwapchain();

    if (renderPass_)  { vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }

    for (VkSemaphore s : imageAvailable_) if (s) vkDestroySemaphore(device_, s, nullptr);
    for (VkSemaphore s : renderFinished_) if (s) vkDestroySemaphore(device_, s, nullptr);
    for (VkFence f : inFlight_)           if (f) vkDestroyFence(device_, f, nullptr);
    imageAvailable_.clear(); renderFinished_.clear(); inFlight_.clear();

    if (commandPool_) { vkDestroyCommandPool(device_, commandPool_, nullptr); commandPool_ = VK_NULL_HANDLE; }
    if (device_)      { vkDestroyDevice(device_, nullptr);   device_   = VK_NULL_HANDLE; }
    if (surface_)     { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    if (instance_)    { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
}

bool VulkanContext::recreateSwapchain() {
    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window_, &w, &h);
    if (w == 0 || h == 0)
        return false; // minimised; skip

    vkDeviceWaitIdle(device_);
    destroySwapchain();

    if (!createSwapchain())      return false;
    if (!createImageViews())     return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers())   return false;
    framebufferResized_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// per-frame
// ---------------------------------------------------------------------------

VkCommandBuffer VulkanContext::beginFrame() {
    if (device_ == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);

    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex_);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        con << "Vulkan: vkAcquireNextImageKHR failed (" << int(acq) << ")\n";
        return VK_NULL_HANDLE;
    }

    vkResetFences(device_, 1, &inFlight_[currentFrame_]);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);

    std::array<VkClearValue, 2> clears{};
    clears[0].color        = {{0.f, 0.f, 0.f, 1.f}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass        = renderPass_;
    rp.framebuffer       = framebuffers_[imageIndex_];
    rp.renderArea.extent = extent_;
    rp.clearValueCount   = static_cast<uint32_t>(clears.size());
    rp.pClearValues      = clears.data();
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = float(extent_.width);
    vp.height   = float(extent_.height);
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{ {0, 0}, extent_ };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    currentCmd_ = cmd;
    return cmd;
}

void VulkanContext::endFrame() {
    if (currentCmd_ == VK_NULL_HANDLE)
        return;

    VkCommandBuffer cmd = currentCmd_;
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &imageAvailable_[currentFrame_];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &renderFinished_[currentFrame_];
    vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_[currentFrame_]);

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished_[currentFrame_];
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain_;
    present.pImageIndices      = &imageIndex_;
    VkResult pr = vkQueuePresentKHR(presentQueue_, &present);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || framebufferResized_)
        recreateSwapchain();

    currentCmd_   = VK_NULL_HANDLE;
    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
}

// ---------------------------------------------------------------------------
// resource helpers
// ---------------------------------------------------------------------------

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    con << "Vulkan: no suitable memory type\n";
    return 0;
}

bool VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props,
                                 VkBuffer& buffer, VkDeviceMemory& memory) const {
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &bi, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, buffer, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &memory), "vkAllocateMemory");
    vkBindBufferMemory(device_, buffer, memory, 0);
    return true;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

} // namespace vk

#endif // HAVE_VULKAN
#endif // DEDICATED
