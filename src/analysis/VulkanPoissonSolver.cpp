#include "VulkanPoissonSolver.hpp"

#ifdef MODELREPAIR_HAVE_VULKAN

#include "poisson_spmv_spv.h"
#include "poisson_reduce_spv.h"
#include "poisson_axpy_spv.h"
#include "poisson_precond_spv.h"
#include "poisson_scalar_spv.h"
#include "poisson_pupdate_spv.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace modelrepair
{

// ── Vulkan helpers (same as VulkanSmoothing.cpp) ──────────────────────────

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
                     VkDeviceSize sz, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags mem_props, Buf& out)
{
    out.size = sz;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = sz;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, nullptr, &out.buf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, out.buf, &mr);

    uint32_t mt = find_mem_type(pd, mr.memoryTypeBits, mem_props);
    if (mt == UINT32_MAX) return false;

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &mai, nullptr, &out.mem) != VK_SUCCESS) return false;

    vkBindBufferMemory(dev, out.buf, out.mem, 0);
    return true;
}

static void destroy_buf(VkDevice dev, Buf& b)
{
    if (b.buf) { vkDestroyBuffer(dev, b.buf, nullptr); b.buf = VK_NULL_HANDLE; }
    if (b.mem) { vkFreeMemory(dev, b.mem, nullptr);    b.mem = VK_NULL_HANDLE; }
}

static bool upload(VkDevice dev, VkPhysicalDevice pd, VkQueue queue,
                   VkCommandPool pool, Buf& dst, const void* data, VkDeviceSize sz)
{
    Buf stage;
    if (!make_buf(dev, pd, sz,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stage))
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
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        destroy_buf(dev, stage);
        return false;
    }

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

// Read one float from a device-local buffer at offset 0.
static float download_float(VkDevice dev, VkPhysicalDevice pd,
                              VkQueue queue, VkCommandPool pool, Buf& src)
{
    Buf stage;
    if (!make_buf(dev, pd, sizeof(float),
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stage))
        return std::numeric_limits<float>::infinity();

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        destroy_buf(dev, stage);
        return std::numeric_limits<float>::infinity();
    }

    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    VkBufferCopy region{0, 0, sizeof(float)};
    vkCmdCopyBuffer(cb, src.buf, stage.buf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cb);

    float val = 0.0f;
    void* ptr;
    vkMapMemory(dev, stage.mem, 0, sizeof(float), 0, &ptr);
    std::memcpy(&val, ptr, sizeof(float));
    vkUnmapMemory(dev, stage.mem);
    destroy_buf(dev, stage);
    return val;
}

// ── Impl ──────────────────────────────────────────────────────────────────

struct VulkanPoissonSolver::Impl
{
    VkInstance       instance  = VK_NULL_HANDLE;
    VkPhysicalDevice phys_dev  = VK_NULL_HANDLE;
    VkDevice         device    = VK_NULL_HANDLE;
    VkQueue          queue     = VK_NULL_HANDLE;
    uint32_t         queue_fam = 0;
    VkCommandPool    cmd_pool  = VK_NULL_HANDLE;

    // GPU buffers: solution, residual, preconditioned residual,
    // search direction, A*p, partial sums, dot products, scalars
    Buf x_buf, r_buf, z_buf, p_buf, ap_buf;
    Buf partial_buf;                // ceil(N/256) floats
    Buf dot_buf;                    // 2 floats: [0]=dot(p,Ap), [1]=dot(r,z)
    Buf scalar_buf;                 // 3 floats: [0]=RZ, [1]=ALPHA, [2]=BETA

    VkShaderModule mod_spmv    = VK_NULL_HANDLE;
    VkShaderModule mod_reduce  = VK_NULL_HANDLE;
    VkShaderModule mod_axpy    = VK_NULL_HANDLE;
    VkShaderModule mod_precond = VK_NULL_HANDLE;
    VkShaderModule mod_scalar  = VK_NULL_HANDLE;
    VkShaderModule mod_pupdate = VK_NULL_HANDLE;

    // Descriptor set layouts: dsl2=2 SSBOs, dsl3=3 SSBOs, dsl4=4 SSBOs
    VkDescriptorSetLayout dsl2 = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl3 = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl4 = VK_NULL_HANDLE;

    VkDescriptorPool desc_pool    = VK_NULL_HANDLE;
    VkDescriptorSet  ds_spmv       = VK_NULL_HANDLE; // {p, Ap}
    VkDescriptorSet  ds_reduce_pAp = VK_NULL_HANDLE; // {p, Ap, partial, dot}
    VkDescriptorSet  ds_reduce_rz  = VK_NULL_HANDLE; // {r, z, partial, dot}
    VkDescriptorSet  ds_scalar     = VK_NULL_HANDLE; // {dot, scalar}
    VkDescriptorSet  ds_axpy_xp    = VK_NULL_HANDLE; // {x, p, scalar}
    VkDescriptorSet  ds_axpy_rap   = VK_NULL_HANDLE; // {r, Ap, scalar}
    VkDescriptorSet  ds_precond    = VK_NULL_HANDLE; // {z, r}
    VkDescriptorSet  ds_pupdate    = VK_NULL_HANDLE; // {p, z, scalar}

    VkPipelineLayout pl_spmv    = VK_NULL_HANDLE;
    VkPipelineLayout pl_reduce  = VK_NULL_HANDLE;
    VkPipelineLayout pl_axpy    = VK_NULL_HANDLE;
    VkPipelineLayout pl_precond = VK_NULL_HANDLE;
    VkPipelineLayout pl_scalar  = VK_NULL_HANDLE;
    VkPipelineLayout pl_pupdate = VK_NULL_HANDLE;

    VkPipeline pipe_spmv    = VK_NULL_HANDLE;
    VkPipeline pipe_reduce  = VK_NULL_HANDLE;
    VkPipeline pipe_axpy    = VK_NULL_HANDLE;
    VkPipeline pipe_precond = VK_NULL_HANDLE;
    VkPipeline pipe_scalar  = VK_NULL_HANDLE;
    VkPipeline pipe_pupdate = VK_NULL_HANDLE;

    uint32_t N          = 0;
    uint32_t W          = 0;
    uint32_t H          = 0;
    uint32_t num_groups = 0;   // ceil(N/256), also = num_partials
    float    initial_rz = 0.0f;
    int      iters_done_ = 0;
    bool     converged_  = false;
    bool     valid_      = false;

    ~Impl();
};

VulkanPoissonSolver::Impl::~Impl()
{
    if (device) {
        vkDeviceWaitIdle(device);
        auto dp = [&](VkPipeline& p) {
            if (p) { vkDestroyPipeline(device, p, nullptr); p = VK_NULL_HANDLE; }
        };
        auto dl = [&](VkPipelineLayout& l) {
            if (l) { vkDestroyPipelineLayout(device, l, nullptr); l = VK_NULL_HANDLE; }
        };
        auto ds = [&](VkDescriptorSetLayout& l) {
            if (l) { vkDestroyDescriptorSetLayout(device, l, nullptr); l = VK_NULL_HANDLE; }
        };
        auto dm = [&](VkShaderModule& m) {
            if (m) { vkDestroyShaderModule(device, m, nullptr); m = VK_NULL_HANDLE; }
        };
        dp(pipe_spmv); dp(pipe_reduce); dp(pipe_axpy);
        dp(pipe_precond); dp(pipe_scalar); dp(pipe_pupdate);
        dl(pl_spmv); dl(pl_reduce); dl(pl_axpy);
        dl(pl_precond); dl(pl_scalar); dl(pl_pupdate);
        if (desc_pool) {
            vkDestroyDescriptorPool(device, desc_pool, nullptr);
            desc_pool = VK_NULL_HANDLE;
        }
        ds(dsl2); ds(dsl3); ds(dsl4);
        dm(mod_spmv); dm(mod_reduce); dm(mod_axpy);
        dm(mod_precond); dm(mod_scalar); dm(mod_pupdate);
        if (cmd_pool) {
            vkDestroyCommandPool(device, cmd_pool, nullptr);
            cmd_pool = VK_NULL_HANDLE;
        }
        destroy_buf(device, x_buf);   destroy_buf(device, r_buf);
        destroy_buf(device, z_buf);   destroy_buf(device, p_buf);
        destroy_buf(device, ap_buf);  destroy_buf(device, partial_buf);
        destroy_buf(device, dot_buf); destroy_buf(device, scalar_buf);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

// ── Constructor ───────────────────────────────────────────────────────────

VulkanPoissonSolver::VulkanPoissonSolver(int W, int H, const std::vector<float>& b)
{
    impl_ = new Impl;
    auto& im = *impl_;
    im.W = static_cast<uint32_t>(W);
    im.H = static_cast<uint32_t>(H);
    im.N = im.W * im.H;
    if (im.N == 0 || static_cast<int>(b.size()) != W * H) return;

    const uint32_t N  = im.N;
    im.num_groups = (N + 255u) / 256u;

    // ── Instance ────────────────────────────────────────────────────────
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "modelrepair";
    ai.apiVersion       = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    if (vkCreateInstance(&ici, nullptr, &im.instance) != VK_SUCCESS) return;

    // ── Physical device ─────────────────────────────────────────────────
    uint32_t pd_count = 0;
    vkEnumeratePhysicalDevices(im.instance, &pd_count, nullptr);
    if (pd_count == 0) return;
    std::vector<VkPhysicalDevice> pds(pd_count);
    vkEnumeratePhysicalDevices(im.instance, &pd_count, pds.data());

    struct Candidate { VkPhysicalDevice pd; uint32_t qf; };
    Candidate discrete{VK_NULL_HANDLE, 0}, any{VK_NULL_HANDLE, 0};

    for (auto pd : pds) {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, qfp.data());
        for (uint32_t i = 0; i < qf_count; ++i) {
            if (!(qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            if (any.pd == VK_NULL_HANDLE) any = {pd, i};
            if (discrete.pd == VK_NULL_HANDLE) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(pd, &props);
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    discrete = {pd, i};
            }
            break;
        }
    }

    const Candidate& chosen = (discrete.pd != VK_NULL_HANDLE) ? discrete : any;
    if (chosen.pd == VK_NULL_HANDLE) return;
    im.phys_dev  = chosen.pd;
    im.queue_fam = chosen.qf;

    // ── Logical device + queue ────────────────────────────────────────────
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dqci.queueFamilyIndex = im.queue_fam;
    dqci.queueCount       = 1;
    dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &dqci;
    if (vkCreateDevice(im.phys_dev, &dci, nullptr, &im.device) != VK_SUCCESS) return;
    vkGetDeviceQueue(im.device, im.queue_fam, 0, &im.queue);

    VkDevice         dev = im.device;
    VkPhysicalDevice pd  = im.phys_dev;

    // ── Command pool ────────────────────────────────────────────────────
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = im.queue_fam;
    if (vkCreateCommandPool(dev, &cpci, nullptr, &im.cmd_pool) != VK_SUCCESS) return;

    // ── Allocate GPU buffers ─────────────────────────────────────────────
    const VkDeviceSize buf_sz  = N * sizeof(float);
    const VkDeviceSize part_sz = std::max(VkDeviceSize{4},
                                          im.num_groups * sizeof(float));
    const VkDeviceSize dot_sz  = 2 * sizeof(float);
    const VkDeviceSize scal_sz = 3 * sizeof(float);

    constexpr VkMemoryPropertyFlags DEV = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    constexpr VkBufferUsageFlags COMPUTE_BUF =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT   |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    if (!make_buf(dev, pd, buf_sz,  COMPUTE_BUF, DEV, im.x_buf))      return;
    if (!make_buf(dev, pd, buf_sz,  COMPUTE_BUF, DEV, im.r_buf))      return;
    if (!make_buf(dev, pd, buf_sz,  COMPUTE_BUF, DEV, im.z_buf))      return;
    if (!make_buf(dev, pd, buf_sz,  COMPUTE_BUF, DEV, im.p_buf))      return;
    if (!make_buf(dev, pd, buf_sz,  COMPUTE_BUF, DEV, im.ap_buf))     return;
    if (!make_buf(dev, pd, part_sz, COMPUTE_BUF, DEV, im.partial_buf)) return;
    if (!make_buf(dev, pd, dot_sz,  COMPUTE_BUF, DEV, im.dot_buf))    return;
    if (!make_buf(dev, pd, scal_sz, COMPUTE_BUF, DEV, im.scalar_buf)) return;

    // ── CPU initialisation: compute z = b/4 and rz = sum(b²/4) ─────────
    // The Dirichlet row (i=0) has b[0]=0 so z[0]=0 and no rz contribution.
    float rz_init = 0.0f;
    std::vector<float> z_init(N, 0.0f);
    for (uint32_t i = 1; i < N; ++i) {
        z_init[i] = b[i] * 0.25f;
        rz_init  += b[i] * z_init[i];
    }
    im.initial_rz = rz_init;

    // Upload initial buffer contents
    const std::vector<float> zeros(N, 0.0f);
    if (!upload(dev, pd, im.queue, im.cmd_pool, im.x_buf, zeros.data(), buf_sz)) return;
    if (!upload(dev, pd, im.queue, im.cmd_pool, im.r_buf, b.data(),     buf_sz)) return;
    if (!upload(dev, pd, im.queue, im.cmd_pool, im.z_buf, z_init.data(), buf_sz)) return;
    if (!upload(dev, pd, im.queue, im.cmd_pool, im.p_buf, z_init.data(), buf_sz)) return;
    const float scalars[3] = {rz_init, 0.0f, 0.0f};
    if (!upload(dev, pd, im.queue, im.cmd_pool, im.scalar_buf, scalars, scal_sz)) return;

    // ── Shader modules ────────────────────────────────────────────────────
    auto make_shader = [&](const uint32_t* spv, size_t sz, VkShaderModule& out) -> bool {
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = sz;
        smci.pCode    = spv;
        return vkCreateShaderModule(dev, &smci, nullptr, &out) == VK_SUCCESS;
    };

    if (!make_shader(poisson_spmv_spv,    poisson_spmv_spv_size,    im.mod_spmv))    return;
    if (!make_shader(poisson_reduce_spv,  poisson_reduce_spv_size,  im.mod_reduce))  return;
    if (!make_shader(poisson_axpy_spv,    poisson_axpy_spv_size,    im.mod_axpy))    return;
    if (!make_shader(poisson_precond_spv, poisson_precond_spv_size, im.mod_precond)) return;
    if (!make_shader(poisson_scalar_spv,  poisson_scalar_spv_size,  im.mod_scalar))  return;
    if (!make_shader(poisson_pupdate_spv, poisson_pupdate_spv_size, im.mod_pupdate)) return;

    // ── Descriptor set layouts ────────────────────────────────────────────
    auto make_dsl = [&](uint32_t n_bindings, VkDescriptorSetLayout& out) -> bool {
        std::vector<VkDescriptorSetLayoutBinding> binds(n_bindings);
        for (uint32_t i = 0; i < n_bindings; ++i) {
            binds[i].binding         = i;
            binds[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[i].descriptorCount = 1;
            binds[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dslci.bindingCount = n_bindings;
        dslci.pBindings    = binds.data();
        return vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &out) == VK_SUCCESS;
    };

    if (!make_dsl(2, im.dsl2)) return;
    if (!make_dsl(3, im.dsl3)) return;
    if (!make_dsl(4, im.dsl4)) return;

    // ── Descriptor pool: 3×dsl2=6 + 3×dsl3=9 + 2×dsl4=8 → 23 SSBOs, 8 sets
    VkDescriptorPoolSize pool_sz_entry{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 23};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = 8;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &pool_sz_entry;
    if (vkCreateDescriptorPool(dev, &dpci, nullptr, &im.desc_pool) != VK_SUCCESS) return;

    auto alloc_ds = [&](VkDescriptorSetLayout dsl, VkDescriptorSet& out) -> bool {
        VkDescriptorSetAllocateInfo dsai{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsai.descriptorPool     = im.desc_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &dsl;
        return vkAllocateDescriptorSets(dev, &dsai, &out) == VK_SUCCESS;
    };

    if (!alloc_ds(im.dsl2, im.ds_spmv))       return;
    if (!alloc_ds(im.dsl4, im.ds_reduce_pAp)) return;
    if (!alloc_ds(im.dsl4, im.ds_reduce_rz))  return;
    if (!alloc_ds(im.dsl2, im.ds_scalar))     return;
    if (!alloc_ds(im.dsl3, im.ds_axpy_xp))    return;
    if (!alloc_ds(im.dsl3, im.ds_axpy_rap))   return;
    if (!alloc_ds(im.dsl2, im.ds_precond))    return;
    if (!alloc_ds(im.dsl3, im.ds_pupdate))    return;

    // Write descriptors
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

    // ds_spmv: binding 0=p, 1=Ap
    write_ssbo(im.ds_spmv, 0, im.p_buf.buf,  buf_sz);
    write_ssbo(im.ds_spmv, 1, im.ap_buf.buf, buf_sz);

    // ds_reduce_pAp: 0=p, 1=Ap, 2=partial, 3=dot
    write_ssbo(im.ds_reduce_pAp, 0, im.p_buf.buf,       buf_sz);
    write_ssbo(im.ds_reduce_pAp, 1, im.ap_buf.buf,      buf_sz);
    write_ssbo(im.ds_reduce_pAp, 2, im.partial_buf.buf, part_sz);
    write_ssbo(im.ds_reduce_pAp, 3, im.dot_buf.buf,     dot_sz);

    // ds_reduce_rz: 0=r, 1=z, 2=partial, 3=dot
    write_ssbo(im.ds_reduce_rz, 0, im.r_buf.buf,       buf_sz);
    write_ssbo(im.ds_reduce_rz, 1, im.z_buf.buf,       buf_sz);
    write_ssbo(im.ds_reduce_rz, 2, im.partial_buf.buf, part_sz);
    write_ssbo(im.ds_reduce_rz, 3, im.dot_buf.buf,     dot_sz);

    // ds_scalar: 0=dot, 1=scalar
    write_ssbo(im.ds_scalar, 0, im.dot_buf.buf,    dot_sz);
    write_ssbo(im.ds_scalar, 1, im.scalar_buf.buf, scal_sz);

    // ds_axpy_xp: 0=x, 1=p, 2=scalar
    write_ssbo(im.ds_axpy_xp, 0, im.x_buf.buf,     buf_sz);
    write_ssbo(im.ds_axpy_xp, 1, im.p_buf.buf,     buf_sz);
    write_ssbo(im.ds_axpy_xp, 2, im.scalar_buf.buf, scal_sz);

    // ds_axpy_rap: 0=r, 1=Ap, 2=scalar
    write_ssbo(im.ds_axpy_rap, 0, im.r_buf.buf,     buf_sz);
    write_ssbo(im.ds_axpy_rap, 1, im.ap_buf.buf,    buf_sz);
    write_ssbo(im.ds_axpy_rap, 2, im.scalar_buf.buf, scal_sz);

    // ds_precond: 0=z, 1=r
    write_ssbo(im.ds_precond, 0, im.z_buf.buf, buf_sz);
    write_ssbo(im.ds_precond, 1, im.r_buf.buf, buf_sz);

    // ds_pupdate: 0=p, 1=z, 2=scalar
    write_ssbo(im.ds_pupdate, 0, im.p_buf.buf,     buf_sz);
    write_ssbo(im.ds_pupdate, 1, im.z_buf.buf,     buf_sz);
    write_ssbo(im.ds_pupdate, 2, im.scalar_buf.buf, scal_sz);

    // ── Pipeline layouts ──────────────────────────────────────────────────
    auto make_pl = [&](VkDescriptorSetLayout dsl, uint32_t push_bytes,
                       VkPipelineLayout& out) -> bool {
        VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &dsl;
        plci.pushConstantRangeCount = (push_bytes > 0) ? 1u : 0u;
        plci.pPushConstantRanges    = (push_bytes > 0) ? &pcr : nullptr;
        return vkCreatePipelineLayout(dev, &plci, nullptr, &out) == VK_SUCCESS;
    };

    // push sizes: spmv=8, reduce=12, axpy=12, precond=4, scalar=4, pupdate=4
    if (!make_pl(im.dsl2,  8, im.pl_spmv))    return;
    if (!make_pl(im.dsl4, 12, im.pl_reduce))  return;
    if (!make_pl(im.dsl3, 12, im.pl_axpy))    return;
    if (!make_pl(im.dsl2,  4, im.pl_precond)) return;
    if (!make_pl(im.dsl2,  4, im.pl_scalar))  return;
    if (!make_pl(im.dsl3,  4, im.pl_pupdate)) return;

    // ── Compute pipelines ─────────────────────────────────────────────────
    auto make_pipeline = [&](VkShaderModule mod, VkPipelineLayout layout,
                              VkPipeline& out) -> bool {
        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = mod;
        stage.pName  = "main";
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage  = stage;
        cpci.layout = layout;
        return vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr,
                                        &out) == VK_SUCCESS;
    };

    if (!make_pipeline(im.mod_spmv,    im.pl_spmv,    im.pipe_spmv))    return;
    if (!make_pipeline(im.mod_reduce,  im.pl_reduce,  im.pipe_reduce))  return;
    if (!make_pipeline(im.mod_axpy,    im.pl_axpy,    im.pipe_axpy))    return;
    if (!make_pipeline(im.mod_precond, im.pl_precond, im.pipe_precond)) return;
    if (!make_pipeline(im.mod_scalar,  im.pl_scalar,  im.pipe_scalar))  return;
    if (!make_pipeline(im.mod_pupdate, im.pl_pupdate, im.pipe_pupdate)) return;

    im.valid_ = true;
}

VulkanPoissonSolver::~VulkanPoissonSolver() { delete impl_; }

bool VulkanPoissonSolver::valid()          const { return impl_ && impl_->valid_;      }
bool VulkanPoissonSolver::converged()      const { return impl_ && impl_->converged_;  }
int  VulkanPoissonSolver::iterations_done() const { return impl_ ? impl_->iters_done_ : 0; }

// ── run ───────────────────────────────────────────────────────────────────

void VulkanPoissonSolver::run(int n_iterations, int batch_size,
                               std::function<bool(int)> on_iteration)
{
    if (!valid() || n_iterations <= 0) return;
    auto& im = *impl_;

    VkDevice         dev = im.device;
    VkPhysicalDevice pd  = im.phys_dev;
    const uint32_t N  = im.N;
    const uint32_t W  = im.W;
    const uint32_t H  = im.H;
    const uint32_t NG = im.num_groups;

    // Compute-to-compute memory barrier
    const VkMemoryBarrier cs_mb = []() {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        return mb;
    }();
    auto barrier = [&](VkCommandBuffer cb) {
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &cs_mb, 0, nullptr, 0, nullptr);
    };

    int iter = 0;
    while (iter < n_iterations) {
        const int this_batch = std::min(batch_size, n_iterations - iter);

        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool        = im.cmd_pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cb;
        if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) break;

        VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &cbbi);

        for (int k = 0; k < this_batch; ++k) {

            // 1. spmv: Ap = L*p  (implicit 5-point Laplacian)
            struct { uint32_t width, height; } pc_spmv{W, H};
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_spmv);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_spmv, 0, 1, &im.ds_spmv, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_spmv,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &pc_spmv);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 2a. reduce pass 0: partial[wg] = Σ_local p[i]*Ap[i]
            struct { uint32_t N, pass, out_idx; } pc_rp0{N, 0u, 0u};
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_reduce);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_reduce, 0, 1, &im.ds_reduce_pAp, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_reduce,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_rp0);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 2b. reduce pass 1: dot[0] = Σ partial
            struct { uint32_t N, pass, out_idx; } pc_rp1{NG, 1u, 0u};
            vkCmdPushConstants(cb, im.pl_reduce,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_rp1);
            vkCmdDispatch(cb, 1, 1, 1);
            barrier(cb);

            // 3. scalar step 0: alpha = rz / dot[0]
            uint32_t pc_s0 = 0u;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_scalar);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_scalar, 0, 1, &im.ds_scalar, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_scalar,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pc_s0);
            vkCmdDispatch(cb, 1, 1, 1);
            barrier(cb);

            // 4. axpy: x += alpha * p  (scalar_index=1 = ALPHA, sign=+1)
            struct { uint32_t N, si; int32_t sign; } pc_axp{N, 1u, +1};
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_axpy);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_axpy, 0, 1, &im.ds_axpy_xp, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_axpy,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_axp);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 5. axpy: r -= alpha * Ap  (scalar_index=1 = ALPHA, sign=-1)
            struct { uint32_t N, si; int32_t sign; } pc_axr{N, 1u, -1};
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_axpy, 0, 1, &im.ds_axpy_rap, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_axpy,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_axr);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 6. precond: z = r / 4  (Jacobi with diag=4; pixel 0 is identity)
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_precond);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_precond, 0, 1, &im.ds_precond, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_precond,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &N);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 7a. reduce pass 0: partial[wg] = Σ_local r[i]*z[i]
            struct { uint32_t N, pass, out_idx; } pc_rz0{N, 0u, 1u};
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_reduce);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_reduce, 0, 1, &im.ds_reduce_rz, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_reduce,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_rz0);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);

            // 7b. reduce pass 1: dot[1] = Σ partial  (new rz)
            struct { uint32_t N, pass, out_idx; } pc_rz1{NG, 1u, 1u};
            vkCmdPushConstants(cb, im.pl_reduce,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &pc_rz1);
            vkCmdDispatch(cb, 1, 1, 1);
            barrier(cb);

            // 8. scalar step 1: beta = dot[1]/rz; rz = dot[1]
            uint32_t pc_s1 = 1u;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_scalar);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_scalar, 0, 1, &im.ds_scalar, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_scalar,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pc_s1);
            vkCmdDispatch(cb, 1, 1, 1);
            barrier(cb);

            // 9. pupdate: p = z + beta * p
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, im.pipe_pupdate);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    im.pl_pupdate, 0, 1, &im.ds_pupdate, 0, nullptr);
            vkCmdPushConstants(cb, im.pl_pupdate,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &N);
            vkCmdDispatch(cb, NG, 1, 1);
            barrier(cb);   // ensure p is ready for next iteration's spmv
        }

        vkEndCommandBuffer(cb);

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        vkQueueSubmit(im.queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(im.queue);
        vkFreeCommandBuffers(dev, im.cmd_pool, 1, &cb);

        iter += this_batch;
        im.iters_done_ = iter;

        if (on_iteration && !on_iteration(iter))
            return;   // cancelled — converged_ stays false
    }

    // Convergence: scalar_buf[0] = rz = dot(r,z) = ||r||²/4
    // Converged when ||r||² / ||b||² ≤ tol², i.e. rz ≤ initial_rz * tol²
    constexpr float tol = 1e-4f;
    const float current_rz = download_float(dev, pd, im.queue, im.cmd_pool, im.scalar_buf);
    im.converged_ = (current_rz <= im.initial_rz * (tol * tol));
}

// ── download ─────────────────────────────────────────────────────────────

std::vector<float> VulkanPoissonSolver::download() const
{
    if (!valid()) return {};
    auto& im = *impl_;
    VkDevice         dev = im.device;
    VkPhysicalDevice pd  = im.phys_dev;
    const uint32_t   N   = im.N;
    const VkDeviceSize sz = N * sizeof(float);

    Buf stage;
    if (!make_buf(dev, pd, sz,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  stage))
        return {};

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = im.cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        destroy_buf(dev, stage);
        return {};
    }

    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    VkBufferCopy region{0, 0, sz};
    vkCmdCopyBuffer(cb, im.x_buf.buf, stage.buf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(im.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(im.queue);
    vkFreeCommandBuffers(dev, im.cmd_pool, 1, &cb);

    std::vector<float> result(N);
    void* ptr;
    vkMapMemory(dev, stage.mem, 0, sz, 0, &ptr);
    std::memcpy(result.data(), ptr, sz);
    vkUnmapMemory(dev, stage.mem);
    destroy_buf(dev, stage);
    return result;
}

} // namespace modelrepair

#endif // MODELREPAIR_HAVE_VULKAN
