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

#include "vk_renderer.h"

#ifndef DEDICATED
#ifdef HAVE_VULKAN

#include <SDL.h>
#include <SDL_vulkan.h>
#include "tConsole.h"

#include <cstddef>
#include <cstring>

// SPIR-V bytecode generated from shaders/batch.{vert,frag} at build time
// (glslangValidator --vn).  See Makefile.am for the rule.
#include "batch_vert.spv.h"
#include "batch_frag.spv.h"

namespace vk {

namespace { VulkanRenderer* s_instance = nullptr; }

VulkanRenderer* GetVulkanRenderer() { return s_instance; }

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

VulkanRenderer::VulkanRenderer() {
    s_instance = this;
    renderer   = this;
    modelView_.push_back(vkMat4::identity());
    projection_.push_back(vkMat4::identity());
    texture_.push_back(vkMat4::identity());
}

VulkanRenderer::~VulkanRenderer() {
    if (ctx_.isReady()) {
        vkDeviceWaitIdle(ctx_.device());
        VkDevice d = ctx_.device();
        for (auto& f : frames_) {
            if (f.memory) vkUnmapMemory(d, f.memory);
            if (f.buffer) vkDestroyBuffer(d, f.buffer, nullptr);
            if (f.memory) vkFreeMemory(d, f.memory, nullptr);
        }
        if (sampler_)     vkDestroySampler(d, sampler_, nullptr);
        if (whiteView_)   vkDestroyImageView(d, whiteView_, nullptr);
        if (whiteImage_)  vkDestroyImage(d, whiteImage_, nullptr);
        if (whiteMemory_) vkFreeMemory(d, whiteMemory_, nullptr);
        if (trianglePipeline_) vkDestroyPipeline(d, trianglePipeline_, nullptr);
        if (linePipeline_)     vkDestroyPipeline(d, linePipeline_, nullptr);
        if (pipelineLayout_)   vkDestroyPipelineLayout(d, pipelineLayout_, nullptr);
        if (descPool_)         vkDestroyDescriptorPool(d, descPool_, nullptr);
        if (descLayout_)       vkDestroyDescriptorSetLayout(d, descLayout_, nullptr);
    }
    ctx_.shutdown();
    if (s_instance == this) s_instance = nullptr;
}

bool VulkanRenderer::IsSupported() {
    // Loading succeeds iff the loader resolved core entry points.
    return SDL_Vulkan_LoadLibrary(nullptr) == 0;
}

// ---------------------------------------------------------------------------
// pipelines / resources
// ---------------------------------------------------------------------------

static VkShaderModule makeModule(VkDevice d, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(d, &ci, nullptr, &m);
    return m;
}

bool VulkanRenderer::createPipelines() {
    VkDevice d = ctx_.device();

    // Descriptor set layout: one combined image sampler at binding 0.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dlci.bindingCount = 1;
    dlci.pBindings    = &binding;
    if (vkCreateDescriptorSetLayout(d, &dlci, nullptr, &descLayout_) != VK_SUCCESS)
        return false;

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset     = 0;
    push.size       = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &descLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &push;
    if (vkCreatePipelineLayout(d, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS)
        return false;

    VkShaderModule vert = makeModule(d, batch_vert_spv, sizeof(batch_vert_spv));
    VkShaderModule frag = makeModule(d, batch_frag_spv, sizeof(batch_frag_spv));
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(VkVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, r) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(VkVertex, s) };

    VkPipelineVertexInputStateCreateInfo vin{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vin.vertexBindingDescriptionCount   = 1;
    vin.pVertexBindingDescriptions      = &bind;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions    = attrs;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable         = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp        = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments    = &blend;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    auto makePipeline = [&](VkPrimitiveTopology topo, VkPipeline& out) -> bool {
        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = topo;

        VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &vin;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState      = &vp;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cb;
        pci.pDynamicState       = &dyn;
        pci.layout              = pipelineLayout_;
        pci.renderPass          = ctx_.renderPass();
        pci.subpass             = 0;
        return vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &pci, nullptr, &out) == VK_SUCCESS;
    };

    bool ok = makePipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, trianglePipeline_) &&
              makePipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST,     linePipeline_);

    vkDestroyShaderModule(d, vert, nullptr);
    vkDestroyShaderModule(d, frag, nullptr);
    return ok;
}

bool VulkanRenderer::createDefaultTexture() {
    VkDevice d = ctx_.device();
    const uint32_t white = 0xFFFFFFFFu;

    // Staging buffer
    VkBuffer staging; VkDeviceMemory stagingMem;
    if (!ctx_.createBuffer(sizeof(white), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingMem))
        return false;
    void* p = nullptr;
    vkMapMemory(d, stagingMem, 0, sizeof(white), 0, &p);
    std::memcpy(p, &white, sizeof(white));
    vkUnmapMemory(d, stagingMem);

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.extent      = { 1, 1, 1 };
    ici.mipLevels   = 1;
    ici.arrayLayers = 1;
    ici.format      = VK_FORMAT_R8G8B8A8_UNORM;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(d, &ici, nullptr, &whiteImage_) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(d, whiteImage_, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = ctx_.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(d, &ai, nullptr, &whiteMemory_);
    vkBindImageMemory(d, whiteImage_, whiteMemory_, 0);

    // Transition + copy
    VkCommandBuffer cmd = ctx_.beginSingleTimeCommands();
    VkImageMemoryBarrier toDst{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image     = whiteImage_;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy copy{};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent      = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, staging, whiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShader = toDst;
    toShader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toShader);
    ctx_.endSingleTimeCommands(cmd);

    vkDestroyBuffer(d, staging, nullptr);
    vkFreeMemory(d, stagingMem, nullptr);

    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image    = whiteImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(d, &vi, nullptr, &whiteView_);

    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(d, &sci, nullptr, &sampler_);

    // Descriptor pool + set
    VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &ps;
    vkCreateDescriptorPool(d, &dpci, nullptr, &descPool_);

    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool     = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &descLayout_;
    vkAllocateDescriptorSets(d, &dsai, &descSet_);

    VkDescriptorImageInfo dii{};
    dii.sampler     = sampler_;
    dii.imageView   = whiteView_;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = descSet_;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &dii;
    vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
    return true;
}

bool VulkanRenderer::init(SDL_Window* window) {
    if (!ctx_.init(window))    return false;
    if (!createPipelines())    { con << "Vulkan: pipeline creation failed\n"; return false; }
    if (!createDefaultTexture()){ con << "Vulkan: default texture failed\n"; return false; }
    ready_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------

void VulkanRenderer::beginFrame() {
    if (!ready_) return;
    ctx_.beginFrame();
    frameWriteOffset_ = 0;
    // The in-flight fence for this frame index was just waited on, so the
    // buffer is no longer referenced by the GPU and can be safely resized.
    growFrameBufferIfNeeded();
}

void VulkanRenderer::endFrame() {
    if (!ready_) return;
    End(true);
    ctx_.endFrame();
}

void VulkanRenderer::growFrameBufferIfNeeded() {
    FrameBuffer& fb = frames_[ctx_.frameIndex()];
    if (fb.size >= desiredSize_)
        return;

    VkDevice d = ctx_.device();
    if (fb.memory) vkUnmapMemory(d, fb.memory);
    if (fb.buffer) vkDestroyBuffer(d, fb.buffer, nullptr);
    if (fb.memory) vkFreeMemory(d, fb.memory, nullptr);

    ctx_.createBuffer(desiredSize_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        fb.buffer, fb.memory);
    fb.size = desiredSize_;
    vkMapMemory(d, fb.memory, 0, desiredSize_, 0, &fb.mapped);
}

// ---------------------------------------------------------------------------
// primitive expansion
// ---------------------------------------------------------------------------

void VulkanRenderer::expandInto(std::vector<VkVertex>& out, bool& outIsLines) const {
    const auto& v = raw_;
    const size_t n = v.size();
    switch (prim_) {
    case Prim::Lines:
        outIsLines = true;
        out.insert(out.end(), v.begin(), v.end());
        break;
    case Prim::LineStrip:
        outIsLines = true;
        for (size_t i = 0; i + 1 < n; ++i) { out.push_back(v[i]); out.push_back(v[i+1]); }
        break;
    case Prim::LineLoop:
        outIsLines = true;
        for (size_t i = 0; i + 1 < n; ++i) { out.push_back(v[i]); out.push_back(v[i+1]); }
        if (n >= 2) { out.push_back(v[n-1]); out.push_back(v[0]); }
        break;
    case Prim::Triangles:
        outIsLines = false;
        out.insert(out.end(), v.begin(), v.end());
        break;
    case Prim::TriangleStrip:
        outIsLines = false;
        for (size_t i = 0; i + 2 < n; ++i) {
            if (i & 1) { out.push_back(v[i+1]); out.push_back(v[i]);   out.push_back(v[i+2]); }
            else       { out.push_back(v[i]);   out.push_back(v[i+1]); out.push_back(v[i+2]); }
        }
        break;
    case Prim::TriangleFan:
        outIsLines = false;
        for (size_t i = 1; i + 1 < n; ++i) { out.push_back(v[0]); out.push_back(v[i]); out.push_back(v[i+1]); }
        break;
    case Prim::Quads:
        outIsLines = false;
        for (size_t i = 0; i + 3 < n; i += 4) {
            out.push_back(v[i]);   out.push_back(v[i+1]); out.push_back(v[i+2]);
            out.push_back(v[i]);   out.push_back(v[i+2]); out.push_back(v[i+3]);
        }
        break;
    case Prim::QuadStrip:
        outIsLines = false;
        for (size_t i = 0; i + 3 < n; i += 2) {
            // strip quad uses verts i, i+1, i+3, i+2
            out.push_back(v[i]);   out.push_back(v[i+1]); out.push_back(v[i+3]);
            out.push_back(v[i]);   out.push_back(v[i+3]); out.push_back(v[i+2]);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------

void VulkanRenderer::flush() {
    if (!ready_ || prim_ == Prim::None || raw_.empty()) {
        raw_.clear();
        prim_ = Prim::None;
        return;
    }
    VkCommandBuffer cmd = ctx_.currentCmd();
    if (cmd == VK_NULL_HANDLE) {
        raw_.clear();
        prim_ = Prim::None;
        return;
    }

    bool isLines = false;
    std::vector<VkVertex> expanded;
    expandInto(expanded, isLines);
    if (expanded.empty()) {
        raw_.clear();
        prim_ = Prim::None;
        return;
    }

    const VkDeviceSize bytes = expanded.size() * sizeof(VkVertex);

    FrameBuffer& fb = frames_[ctx_.frameIndex()];
    if (!fb.mapped || frameWriteOffset_ + bytes > fb.size) {
        // Overflow: cannot grow mid-frame (recorded draws still reference the
        // live buffer).  Bump the high-water mark so the next frame's buffer is
        // large enough, and drop this batch for the current frame only.
        VkDeviceSize need = frameWriteOffset_ + bytes;
        while (desiredSize_ < need) desiredSize_ *= 2;
        raw_.clear();
        prim_ = Prim::None;
        return;
    }

    VkDeviceSize offset = frameWriteOffset_;
    std::memcpy(static_cast<char*>(fb.mapped) + offset, expanded.data(), bytes);
    frameWriteOffset_ += bytes;

    // mvp = clipCorrect * projection * modelView
    // Vulkan clip space: Y points down and depth is [0,1] (GL is [-1,1]).
    vkMat4 clip = vkMat4::identity();
    clip.m[5]  = -1.f;
    clip.m[10] = 0.5f;
    clip.m[14] = 0.5f;
    vkMat4 mvp = clip * (projection_.back() * modelView_.back());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        isLines ? linePipeline_ : trianglePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
        0, 1, &descSet_, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(float) * 16, mvp.m);
    vkCmdBindVertexBuffers(cmd, 0, 1, &fb.buffer, &offset);
    vkCmdDraw(cmd, static_cast<uint32_t>(expanded.size()), 1, 0, 0);

    raw_.clear();
    prim_ = Prim::None;
}

// ---------------------------------------------------------------------------
// rRenderer interface
// ---------------------------------------------------------------------------

void VulkanRenderer::End(bool /*force*/) { flush(); }

void VulkanRenderer::BeginLines()         { End(false); prim_ = Prim::Lines; }
void VulkanRenderer::BeginTriangles()     { End(false); prim_ = Prim::Triangles; }
void VulkanRenderer::BeginQuads()         { End(false); prim_ = Prim::Quads; }
void VulkanRenderer::BeginLineStrip()     { End(false); prim_ = Prim::LineStrip; }
void VulkanRenderer::BeginTriangleStrip() { End(false); prim_ = Prim::TriangleStrip; }
void VulkanRenderer::BeginQuadStrip()     { End(false); prim_ = Prim::QuadStrip; }
void VulkanRenderer::BeginTriangleFan()   { End(false); prim_ = Prim::TriangleFan; }
void VulkanRenderer::BeginLineLoop()      { End(false); prim_ = Prim::LineLoop; }

void VulkanRenderer::Color(REAL r, REAL g, REAL b)         { curR_=r; curG_=g; curB_=b; curA_=1; }
void VulkanRenderer::Color(REAL r, REAL g, REAL b, REAL a) { curR_=r; curG_=g; curB_=b; curA_=a; }

void VulkanRenderer::TexCoord(REAL u, REAL v)                       { curS_=u; curT_=v; }
void VulkanRenderer::TexCoord(REAL u, REAL v, REAL)                 { curS_=u; curT_=v; }
void VulkanRenderer::TexCoord(REAL u, REAL v, REAL, REAL)           { curS_=u; curT_=v; }

void VulkanRenderer::Vertex(REAL x, REAL y)                 { Vertex(x, y, 0, 1); }
void VulkanRenderer::Vertex(REAL x, REAL y, REAL z)         { Vertex(x, y, z, 1); }
void VulkanRenderer::Vertex3(REAL* x)                       { Vertex(x[0], x[1], x[2], 1); }
void VulkanRenderer::Vertex(REAL x, REAL y, REAL z, REAL w) {
    raw_.push_back(VkVertex{ float(x),float(y),float(z),float(w),
                             curR_,curG_,curB_,curA_, curS_,curT_ });
}

// ---------------------------------------------------------------------------
// matrices
// ---------------------------------------------------------------------------

std::vector<vkMat4>& VulkanRenderer::activeStack() {
    switch (matrixMode_) {
    case MatrixMode::Proj: return projection_;
    case MatrixMode::Tex:  return texture_;
    default:               return modelView_;
    }
}

void VulkanRenderer::ProjMatrix()  { matrixMode_ = MatrixMode::Proj; }
void VulkanRenderer::ModelMatrix() { matrixMode_ = MatrixMode::Model; }
void VulkanRenderer::TexMatrix()   { matrixMode_ = MatrixMode::Tex; }

void VulkanRenderer::PushMatrix() { auto& s = activeStack(); s.push_back(s.back()); }
void VulkanRenderer::PopMatrix()  { auto& s = activeStack(); if (s.size() > 1) s.pop_back(); }

void VulkanRenderer::MultMatrix(REAL mdata[4][4]) {
    float f[16];
    for (int i = 0; i < 16; ++i) f[i] = float((&mdata[0][0])[i]);
    auto& s = activeStack();
    s.back() = s.back() * vkMat4::fromColMajor(f);
}

void VulkanRenderer::IdentityMatrix() { activeStack().back() = vkMat4::identity(); }

void VulkanRenderer::ScaleMatrix(REAL f) { auto& s=activeStack(); s.back() = s.back() * vkMat4::scale(f,f,f); }
void VulkanRenderer::ScaleMatrix(REAL f1, REAL f2, REAL f3) { auto& s=activeStack(); s.back() = s.back() * vkMat4::scale(f1,f2,f3); }
void VulkanRenderer::TranslateMatrix(REAL x, REAL y, REAL z){ auto& s=activeStack(); s.back() = s.back() * vkMat4::translate(x,y,z); }

void VulkanRenderer::ReallySetFlag(flag /*f*/, bool /*c*/) {
    // Blend/depth are baked into the pipeline state for this first pass; dynamic
    // per-flag toggling would require additional pipeline permutations.
}

} // namespace vk

// ---------------------------------------------------------------------------
// global frame hooks
// ---------------------------------------------------------------------------

void sr_vkBeginFrame() {
    if (auto* r = vk::GetVulkanRenderer())
        r->beginFrame();
}

void sr_vkEndFrame() {
    if (auto* r = vk::GetVulkanRenderer())
        r->endFrame();
}

#endif // HAVE_VULKAN
#endif // DEDICATED
