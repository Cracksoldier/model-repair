#include "VulkanSmoothing.hpp"

#ifdef MODELREPAIR_HAVE_VULKAN

#include "smooth_laplacian_spv.h"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace modelrepair {

namespace PMP = CGAL::Polygon_mesh_processing;

// ─────────────────────────────────────────────── Vulkan helpers ──

static uint32_t find_mem_type(VkPhysicalDevice pd, uint32_t type_bits,
                               VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & required) == required)
            return i;
    return UINT32_MAX;
}

struct Buf {
    VkBuffer       buf  = VK_NULL_HANDLE;
    VkDeviceMemory mem  = VK_NULL_HANDLE;
    VkDeviceSize   size = 0;
};

static bool make_buf(VkDevice dev, VkPhysicalDevice pd,
                     VkDeviceSize sz,
                     VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags mem_props,
                     Buf& out)
{
    out.size = sz;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = sz;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, nullptr, &out.buf) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, out.buf, &mr);

    uint32_t mt = find_mem_type(pd, mr.memoryTypeBits, mem_props);
    if (mt == UINT32_MAX) return false;

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &mai, nullptr, &out.mem) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(dev, out.buf, out.mem, 0);
    return true;
}

static void destroy_buf(VkDevice dev, Buf& b)
{
    if (b.buf) { vkDestroyBuffer(dev, b.buf, nullptr); b.buf = VK_NULL_HANDLE; }
    if (b.mem) { vkFreeMemory(dev, b.mem, nullptr);    b.mem = VK_NULL_HANDLE; }
}

// Upload data from CPU into a device-local buffer via a transient staging buffer.
static bool upload(VkDevice dev, VkPhysicalDevice pd, VkQueue queue,
                   VkCommandPool pool, Buf& dst, const void* data, VkDeviceSize sz)
{
    Buf stage;
    if (!make_buf(dev, pd, sz,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  stage))
        return false;

    void* ptr;
    vkMapMemory(dev, stage.mem, 0, sz, 0, &ptr);
    std::memcpy(ptr, data, sz);
    vkUnmapMemory(dev, stage.mem);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cbai, &cb);

    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    VkBufferCopy region{0, 0, sz};
    vkCmdCopyBuffer(cb, stage.buf, dst.buf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    destroy_buf(dev, stage);
    return true;
}

// ─────────────────────────────────────────────── Impl ──

struct VulkanSmoothing::Impl {
    VkInstance       instance    = VK_NULL_HANDLE;
    VkPhysicalDevice phys_dev   = VK_NULL_HANDLE;
    VkDevice         device     = VK_NULL_HANDLE;
    VkQueue          queue      = VK_NULL_HANDLE;
    uint32_t         queue_fam  = 0;
    VkCommandPool    cmd_pool   = VK_NULL_HANDLE;

    // Five device-local GPU buffers
    Buf pos_a, pos_b;        // ping-pong position buffers (vec4 × V)
    Buf row_off_buf;         // CSR row offsets (uint32 × V+1)
    Buf col_idx_buf;         // CSR column indices (uint32 × nnz)
    Buf weights_buf;         // cotangent × crease weights (float × nnz)

    VkShaderModule        shader_mod   = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout  = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool    = VK_NULL_HANDLE;
    VkDescriptorSet       desc_sets[2] = {};  // [0]=A→B, [1]=B→A
    VkPipelineLayout      pipe_layout  = VK_NULL_HANDLE;
    VkPipeline            pipeline     = VK_NULL_HANDLE;

    uint32_t vertex_count = 0;
    float    lambda_      = 0.5f;
    uint32_t ping_pong_   = 0;   // which desc_set to use next
    bool     valid_       = false;

    ~Impl();
};

VulkanSmoothing::Impl::~Impl()
{
    if (device) {
        vkDeviceWaitIdle(device);
        if (pipeline)    vkDestroyPipeline(device, pipeline, nullptr);
        if (pipe_layout) vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        if (desc_pool)   vkDestroyDescriptorPool(device, desc_pool, nullptr);
        if (desc_layout) vkDestroyDescriptorSetLayout(device, desc_layout, nullptr);
        if (shader_mod)  vkDestroyShaderModule(device, shader_mod, nullptr);
        if (cmd_pool)    vkDestroyCommandPool(device, cmd_pool, nullptr);
        destroy_buf(device, pos_a);
        destroy_buf(device, pos_b);
        destroy_buf(device, row_off_buf);
        destroy_buf(device, col_idx_buf);
        destroy_buf(device, weights_buf);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

// ─────────────────────────────────────────────── constructor ──

VulkanSmoothing::VulkanSmoothing(Mesh& mesh, double crease_angle, float lambda)
{
    impl_ = new Impl;
    impl_->lambda_ = lambda;

    SurfMesh& M = mesh.cgal();
    const auto V = static_cast<uint32_t>(M.num_vertices());
    impl_->vertex_count = V;
    if (V == 0) return;

    // ── Vulkan instance (no extensions needed for compute-only)
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "modelrepair";
    ai.apiVersion       = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS)
        return;

    // ── Physical device: first with a compute queue family
    uint32_t pd_count = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &pd_count, nullptr);
    if (pd_count == 0) return;
    std::vector<VkPhysicalDevice> pds(pd_count);
    vkEnumeratePhysicalDevices(impl_->instance, &pd_count, pds.data());

    for (auto pd : pds) {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, qfp.data());
        for (uint32_t i = 0; i < qf_count; ++i) {
            if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                impl_->phys_dev  = pd;
                impl_->queue_fam = i;
                break;
            }
        }
        if (impl_->phys_dev != VK_NULL_HANDLE) break;
    }
    if (impl_->phys_dev == VK_NULL_HANDLE) return;

    // ── Logical device + queue
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dqci.queueFamilyIndex = impl_->queue_fam;
    dqci.queueCount       = 1;
    dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &dqci;
    if (vkCreateDevice(impl_->phys_dev, &dci, nullptr, &impl_->device) != VK_SUCCESS)
        return;
    vkGetDeviceQueue(impl_->device, impl_->queue_fam, 0, &impl_->queue);

    VkDevice         dev = impl_->device;
    VkPhysicalDevice pd  = impl_->phys_dev;

    // ── Command pool
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = impl_->queue_fam;
    if (vkCreateCommandPool(dev, &cpci, nullptr, &impl_->cmd_pool) != VK_SUCCESS)
        return;

    // ── Build CSR weight matrix on CPU (same formula as the CPU Laplacian path;
    //    face normals are computed once from the initial mesh — approximation that
    //    is acceptable for typical smooth() calls of 5–20 iterations).
    using V3 = Kernel::Vector_3;
    const double cos_thresh = std::cos(crease_angle * std::numbers::pi / 180.0);

    auto [fnormals, created] =
        M.add_property_map<SurfMesh::Face_index, V3>("f:vk_smooth_n");
    (void)created;
    PMP::compute_face_normals(M, fnormals);

    // Adjacency list: adj[v] = list of (neighbor_index, combined_weight)
    std::vector<std::vector<std::pair<uint32_t, float>>> adj(V);

    for (auto v : vertices(M)) {
        if (M.is_border(v)) continue;
        const uint32_t vi = v.idx();

        for (auto h : halfedges_around_target(v, M)) {
            auto f1 = face(h, M);
            auto f2 = face(opposite(h, M), M);

            double crease_w = 1.0;
            if (f1 != SurfMesh::null_face() && f2 != SurfMesh::null_face()) {
                const V3& n1 = fnormals[f1];
                const V3& n2 = fnormals[f2];
                double len = std::sqrt(CGAL::to_double(n1 * n1))
                           * std::sqrt(CGAL::to_double(n2 * n2));
                double dot_norm = (len < 1e-12) ? -1.0
                                  : CGAL::to_double(n1 * n2) / len;
                double denom = std::max(1.0 - cos_thresh, 1e-6);
                crease_w = std::max(0.0, std::min(1.0,
                    (dot_norm - cos_thresh) / denom));
            }
            if (crease_w < 1e-6) continue;

            auto s = source(h, M);
            double w = 0.0;

            if (f1 != SurfMesh::null_face()) {
                auto w1 = target(next(h, M), M);
                auto a  = M.point(s) - M.point(w1);
                auto b  = M.point(v) - M.point(w1);
                auto cx = CGAL::cross_product(a, b);
                double cl = std::sqrt(CGAL::to_double(cx * cx));
                if (cl > 1e-12) w += CGAL::to_double(a * b) / cl;
            }
            if (f2 != SurfMesh::null_face()) {
                auto w2 = target(next(opposite(h, M), M), M);
                auto a  = M.point(v) - M.point(w2);
                auto b  = M.point(s) - M.point(w2);
                auto cx = CGAL::cross_product(a, b);
                double cl = std::sqrt(CGAL::to_double(cx * cx));
                if (cl > 1e-12) w += CGAL::to_double(a * b) / cl;
            }

            w = std::max(0.0, w) * crease_w;
            if (w < 1e-12) continue;
            adj[vi].push_back({static_cast<uint32_t>(s.idx()),
                               static_cast<float>(w)});
        }
    }

    M.remove_property_map(fnormals);

    // Flatten adj into CSR arrays
    std::vector<uint32_t> row_offsets(V + 1, 0);
    for (uint32_t i = 0; i < V; ++i)
        row_offsets[i + 1] = row_offsets[i]
                           + static_cast<uint32_t>(adj[i].size());
    const uint32_t nnz = row_offsets[V];

    std::vector<uint32_t> col_indices(nnz);
    std::vector<float>    weights_vec(nnz);
    for (uint32_t i = 0; i < V; ++i)
        for (uint32_t k = 0; k < static_cast<uint32_t>(adj[i].size()); ++k) {
            col_indices[row_offsets[i] + k] = adj[i][k].first;
            weights_vec[row_offsets[i] + k] = adj[i][k].second;
        }

    // ── Pack initial positions as float vec4 (xyz + 0-padding)
    std::vector<float> pos_data(V * 4, 0.0f);
    for (auto v : vertices(M)) {
        const uint32_t i = v.idx();
        const auto& p = M.point(v);
        pos_data[i * 4 + 0] = static_cast<float>(CGAL::to_double(p.x()));
        pos_data[i * 4 + 1] = static_cast<float>(CGAL::to_double(p.y()));
        pos_data[i * 4 + 2] = static_cast<float>(CGAL::to_double(p.z()));
    }

    // ── Allocate GPU buffers
    const VkDeviceSize pos_bytes = V * 4 * sizeof(float);
    const VkDeviceSize row_bytes = (V + 1) * sizeof(uint32_t);
    // Allocate at least 4 bytes so the buffer is valid even when nnz == 0
    const VkDeviceSize ci_bytes  = std::max(VkDeviceSize{4}, nnz * sizeof(uint32_t));
    const VkDeviceSize wt_bytes  = std::max(VkDeviceSize{4}, nnz * sizeof(float));

    constexpr VkMemoryPropertyFlags DEV  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    constexpr VkBufferUsageFlags    POS  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    constexpr VkBufferUsageFlags    CSR  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!make_buf(dev, pd, pos_bytes, POS, DEV, impl_->pos_a)) return;
    if (!make_buf(dev, pd, pos_bytes, POS, DEV, impl_->pos_b)) return;
    if (!make_buf(dev, pd, row_bytes, CSR, DEV, impl_->row_off_buf)) return;
    if (!make_buf(dev, pd, ci_bytes,  CSR, DEV, impl_->col_idx_buf)) return;
    if (!make_buf(dev, pd, wt_bytes,  CSR, DEV, impl_->weights_buf)) return;

    // ── Upload initial positions and CSR data
    if (!upload(dev, pd, impl_->queue, impl_->cmd_pool,
                impl_->pos_a, pos_data.data(), pos_bytes)) return;
    if (!upload(dev, pd, impl_->queue, impl_->cmd_pool,
                impl_->row_off_buf, row_offsets.data(), row_bytes)) return;
    if (nnz > 0) {
        if (!upload(dev, pd, impl_->queue, impl_->cmd_pool,
                    impl_->col_idx_buf, col_indices.data(), nnz * sizeof(uint32_t)))
            return;
        if (!upload(dev, pd, impl_->queue, impl_->cmd_pool,
                    impl_->weights_buf, weights_vec.data(), nnz * sizeof(float)))
            return;
    }

    // ── Shader module from embedded SPIR-V
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = smooth_laplacian_spv_size;
    smci.pCode    = smooth_laplacian_spv;
    if (vkCreateShaderModule(dev, &smci, nullptr, &impl_->shader_mod) != VK_SUCCESS)
        return;

    // ── Descriptor set layout: 5 storage-buffer bindings
    VkDescriptorSetLayoutBinding bindings[5] = {};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 5;
    dslci.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(dev, &dslci, nullptr,
                                    &impl_->desc_layout) != VK_SUCCESS)
        return;

    // ── Descriptor pool (2 sets × 5 SSBOs)
    VkDescriptorPoolSize pool_sz{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = 2;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &pool_sz;
    if (vkCreateDescriptorPool(dev, &dpci, nullptr, &impl_->desc_pool) != VK_SUCCESS)
        return;

    VkDescriptorSetLayout layouts[2] = {impl_->desc_layout, impl_->desc_layout};
    VkDescriptorSetAllocateInfo dsai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool     = impl_->desc_pool;
    dsai.descriptorSetCount = 2;
    dsai.pSetLayouts        = layouts;
    if (vkAllocateDescriptorSets(dev, &dsai, impl_->desc_sets) != VK_SUCCESS)
        return;

    // Helper: bind one SSBO binding in a descriptor set
    auto write_ssbo = [&](VkDescriptorSet ds, uint32_t bind,
                          VkBuffer buf, VkDeviceSize sz) {
        VkDescriptorBufferInfo dbi{buf, 0, sz};
        VkWriteDescriptorSet   wd{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wd.dstSet          = ds;
        wd.dstBinding      = bind;
        wd.descriptorCount = 1;
        wd.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wd.pBufferInfo     = &dbi;
        vkUpdateDescriptorSets(dev, 1, &wd, 0, nullptr);
    };

    // desc_sets[0]: reads pos_a, writes pos_b
    write_ssbo(impl_->desc_sets[0], 0, impl_->pos_a.buf,       pos_bytes);
    write_ssbo(impl_->desc_sets[0], 1, impl_->pos_b.buf,       pos_bytes);
    write_ssbo(impl_->desc_sets[0], 2, impl_->row_off_buf.buf, row_bytes);
    write_ssbo(impl_->desc_sets[0], 3, impl_->col_idx_buf.buf, ci_bytes);
    write_ssbo(impl_->desc_sets[0], 4, impl_->weights_buf.buf, wt_bytes);

    // desc_sets[1]: reads pos_b, writes pos_a
    write_ssbo(impl_->desc_sets[1], 0, impl_->pos_b.buf,       pos_bytes);
    write_ssbo(impl_->desc_sets[1], 1, impl_->pos_a.buf,       pos_bytes);
    write_ssbo(impl_->desc_sets[1], 2, impl_->row_off_buf.buf, row_bytes);
    write_ssbo(impl_->desc_sets[1], 3, impl_->col_idx_buf.buf, ci_bytes);
    write_ssbo(impl_->desc_sets[1], 4, impl_->weights_buf.buf, wt_bytes);

    // ── Pipeline layout: descriptor set + 8-byte push constant (vertex_count, lambda)
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &impl_->desc_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(dev, &plci, nullptr, &impl_->pipe_layout) != VK_SUCCESS)
        return;

    // ── Compute pipeline
    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = impl_->shader_mod;
    stage.pName  = "main";
    VkComputePipelineCreateInfo cpci2{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci2.stage  = stage;
    cpci2.layout = impl_->pipe_layout;
    if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci2, nullptr,
                                  &impl_->pipeline) != VK_SUCCESS)
        return;

    impl_->ping_pong_ = 0;
    impl_->valid_     = true;
}

VulkanSmoothing::~VulkanSmoothing() { delete impl_; }

bool VulkanSmoothing::valid() const { return impl_ && impl_->valid_; }

// ─────────────────────────────────────────────── run ──

void VulkanSmoothing::run(unsigned int n_iterations,
                           std::function<void(unsigned int)> on_iteration)
{
    if (!valid() || n_iterations == 0) return;
    auto& im = *impl_;
    VkDevice dev = im.device;

    struct PushConstants { uint32_t vertex_count; float lambda; };
    const PushConstants pc{im.vertex_count, im.lambda_};
    const uint32_t groups = (im.vertex_count + 255u) / 256u;

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = im.cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cbai, &cb);

    for (unsigned int i = 0; i < n_iterations; ++i) {
        VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &cbbi);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                im.pipe_layout, 0, 1,
                                &im.desc_sets[im.ping_pong_], 0, nullptr);
        vkCmdPushConstants(cb, im.pipe_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &pc);
        vkCmdDispatch(cb, groups, 1, 1);

        // Shader-write → shader-read barrier for the next iteration
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);

        vkEndCommandBuffer(cb);

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        vkQueueSubmit(im.queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(im.queue);

        // Flip ping-pong: next iteration reads from the buffer just written
        im.ping_pong_ ^= 1u;

        if (on_iteration) on_iteration(i + 1);
    }

    vkFreeCommandBuffers(dev, im.cmd_pool, 1, &cb);
}

// ─────────────────────────────────────────────── download ──

void VulkanSmoothing::download(Mesh& mesh)
{
    if (!valid()) return;
    auto& im = *impl_;
    VkDevice         dev = im.device;
    VkPhysicalDevice pd  = im.phys_dev;

    // After N iterations, ping_pong_ == N % 2.
    // ping_pong_=0 → last desc_sets[1] was used → wrote pos_a → final = pos_a
    // ping_pong_=1 → last desc_sets[0] was used → wrote pos_b → final = pos_b
    Buf& final_buf = (im.ping_pong_ == 0) ? im.pos_a : im.pos_b;
    const VkDeviceSize pos_bytes = im.vertex_count * 4 * sizeof(float);

    // Staging buffer for readback
    Buf stage;
    if (!make_buf(dev, pd, pos_bytes,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  stage))
        return;

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = im.cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cbai, &cb);

    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    VkBufferCopy region{0, 0, pos_bytes};
    vkCmdCopyBuffer(cb, final_buf.buf, stage.buf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(im.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(im.queue);
    vkFreeCommandBuffers(dev, im.cmd_pool, 1, &cb);

    // Map and copy back into CGAL mesh
    const float* data;
    vkMapMemory(dev, stage.mem, 0, pos_bytes, 0,
                reinterpret_cast<void**>(const_cast<float**>(&data)));
    SurfMesh& M = mesh.cgal();
    for (auto v : vertices(M)) {
        const uint32_t i = v.idx();
        M.point(v) = Point3(static_cast<double>(data[i * 4 + 0]),
                            static_cast<double>(data[i * 4 + 1]),
                            static_cast<double>(data[i * 4 + 2]));
    }
    vkUnmapMemory(dev, stage.mem);
    destroy_buf(dev, stage);
}

} // namespace modelrepair

#endif // MODELREPAIR_HAVE_VULKAN
