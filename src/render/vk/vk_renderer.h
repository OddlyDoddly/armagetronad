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
#include "rRender.h"
#include "vk_context.h"
#include "vk_math.h"
#include <vector>

struct SDL_Window;

namespace vk {

// A vertex as fed to the Vulkan batch pipeline.  Matches batch.vert.
struct VkVertex {
    float x, y, z, w;
    float r, g, b, a;
    float s, t;
};

// rRenderer implementation backed by Vulkan.  Mirrors ModernGLRenderer: the
// engine keeps emitting Begin*/Vertex/Color/TexCoord and matrix calls; this
// translates them into a host-visible vertex stream and records draws into the
// frame command buffer.  Vulkan has no fixed-function matrix stack, so we keep
// our own (vkMat4) for model-view / projection / texture.
class VulkanRenderer : public rRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&)            = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    static bool IsSupported();

    // Bring up the Vulkan device against the (Vulkan-capable) SDL window.
    bool init(SDL_Window* window);

    // Human-readable device/API info for the --gfx-dbg overlay.
    tString debugInfo() const;

    // Frame hooks driven by the engine's existing clear/swap sites.
    void beginFrame();
    void endFrame();

    // ---- rRenderer interface ----
    void Vertex(REAL x, REAL y)                 override;
    void Vertex(REAL x, REAL y, REAL z)         override;
    void Vertex3(REAL* x)                       override;
    void Vertex(REAL x, REAL y, REAL z, REAL w) override;

    void TexCoord(REAL u, REAL v)                 override;
    void TexCoord(REAL u, REAL v, REAL w)         override;
    void TexCoord(REAL u, REAL v, REAL w, REAL t) override;

    void Color(REAL r, REAL g, REAL b)        override;
    void Color(REAL r, REAL g, REAL b, REAL a)override;

    void End(bool force = true) override;

    void BeginLines()         override;
    void BeginTriangles()     override;
    void BeginQuads()         override;
    void BeginLineStrip()     override;
    void BeginTriangleStrip() override;
    void BeginQuadStrip()     override;
    void BeginTriangleFan()   override;
    void BeginLineLoop()      override;

    void ProjMatrix()  override;
    void ModelMatrix() override;
    void TexMatrix()   override;
    void PushMatrix()  override;
    void PopMatrix()   override;
    void MultMatrix(REAL mdata[4][4])           override;
    void IdentityMatrix()                       override;
    void ScaleMatrix(REAL f)                    override;
    void ScaleMatrix(REAL f1, REAL f2, REAL f3) override;
    void TranslateMatrix(REAL x1, REAL x2, REAL x3) override;

protected:
    void ReallySetFlag(flag f, bool c) override;

private:
    enum class Prim {
        Lines, LineStrip, LineLoop,
        Triangles, TriangleStrip, TriangleFan,
        Quads, QuadStrip, None
    };

    bool createPipelines();
    bool createDefaultTexture();
    void flush();
    // Reallocate the current frame's vertex buffer to at least desiredSize_.
    // Only safe to call at frame start (the frame's in-flight fence is signaled),
    // never mid-frame: recorded draws reference the live buffer until submit.
    void growFrameBufferIfNeeded();

    // Expand the current raw primitive into triangle-list or line-list output.
    void expandInto(std::vector<VkVertex>& out, bool& outIsLines) const;

    std::vector<vkMat4>& activeStack();

    VulkanContext ctx_;

    VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_     = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_       = VK_NULL_HANDLE;
    VkDescriptorSet       descSet_        = VK_NULL_HANDLE;
    VkPipeline            trianglePipeline_ = VK_NULL_HANDLE;
    VkPipeline            linePipeline_     = VK_NULL_HANDLE;

    // Default 1x1 white texture so the textured shader yields pure vertex color
    // until per-texture binding is wired through rTexture.
    VkImage        whiteImage_  = VK_NULL_HANDLE;
    VkDeviceMemory whiteMemory_ = VK_NULL_HANDLE;
    VkImageView    whiteView_   = VK_NULL_HANDLE;
    VkSampler      sampler_     = VK_NULL_HANDLE;

    // Per-frame host-visible vertex buffer (mapped, grows on demand).
    struct FrameBuffer {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void*          mapped = nullptr;
        VkDeviceSize   size   = 0;
    };
    FrameBuffer frames_[VulkanContext::kFramesInFlight];
    VkDeviceSize frameWriteOffset_ = 0;
    // High-water mark for required vertex bytes in a frame; grown when a frame
    // overflows, applied to the buffer at the next frame start.
    VkDeviceSize desiredSize_ = 1u << 20; // 1 MiB initial

    // Matrix stacks
    std::vector<vkMat4> modelView_;
    std::vector<vkMat4> projection_;
    std::vector<vkMat4> texture_;
    enum class MatrixMode { Model, Proj, Tex } matrixMode_ = MatrixMode::Model;

    // Current immediate-mode state
    Prim                  prim_ = Prim::None;
    std::vector<VkVertex> raw_;
    float curR_ = 1, curG_ = 1, curB_ = 1, curA_ = 1;
    float curS_ = 0, curT_ = 0;

    bool ready_ = false;
};

// Global accessor (null unless the Vulkan backend is active).
VulkanRenderer* GetVulkanRenderer();

} // namespace vk

// Frame hooks callable from non-Vulkan TUs (no-op unless Vulkan is active).
void sr_vkBeginFrame();
void sr_vkEndFrame();

#endif // HAVE_VULKAN
#endif // DEDICATED
