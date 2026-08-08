/* wubu_vk.c — Vulkan compute accelerator (implementation).
 *
 * Self-contained: instance → device → compute queue → conv1d pipeline →
 * growable host-visible buffer pool. The SPIR-V for the conv shader is
 * embedded (conv_spv.h, generated from build/vk/conv.comp).
 *
 * License: WaefreBeorn-UMV3
 */
#include "wubu_vk.h"
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>

#include "conv_spv.h"   /* build/vk/conv_spv.h — the embedded SPIR-V */

struct WuBuVk {
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue q;
    uint32_t qfam;
    VkCommandPool pool;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    VkDescriptorPool dp;
    VkDescriptorSet ds;
    VkCommandBuffer cb;
    /* buffer pool (grown lazily) */
    VkBuffer b_in, b_w, b_b, b_out;
    VkDeviceMemory m_in, m_w, m_b, m_out;
    size_t cap_in, cap_w, cap_b, cap_out;
};

static int pick_mem_type(WuBuVk *vk, uint32_t type_bits, uint32_t *out_idx) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(vk->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            *out_idx = i;
            return 0;
        }
    }
    return -1;
}

static VkBuffer mk_buf(WuBuVk *vk, VkDeviceSize sz, VkBufferUsageFlags usage,
                       VkDeviceMemory *mem, size_t *cap) {
    VkBuffer b;
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sz, .usage = usage,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (vkCreateBuffer(vk->dev, &bci, NULL, &b) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(vk->dev, b, &mr);
    uint32_t mi = 0;
    if (pick_mem_type(vk, mr.memoryTypeBits, &mi) != 0) return VK_NULL_HANDLE;
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = mr.size, .memoryTypeIndex = mi };
    if (vkAllocateMemory(vk->dev, &mai, NULL, mem) != VK_SUCCESS) return VK_NULL_HANDLE;
    vkBindBufferMemory(vk->dev, b, *mem, 0);
    *cap = (size_t)sz;
    return b;
}

static int ensure_buf(WuBuVk *vk, VkBuffer *b, VkDeviceMemory *m, size_t *cap,
                      size_t need, VkBufferUsageFlags usage) {
    if (*b != VK_NULL_HANDLE && *cap >= need) return 0;
    if (*b != VK_NULL_HANDLE) { vkDestroyBuffer(vk->dev, *b, NULL); vkFreeMemory(vk->dev, *m, NULL); }
    *b = mk_buf(vk, need ? need : 16, usage, m, cap);
    return *b != VK_NULL_HANDLE ? 0 : -1;
}

static void upload(VkDevice dev, VkDeviceMemory mem, const void *data, size_t sz) {
    void *p = NULL;
    vkMapMemory(dev, mem, 0, sz, 0, &p);
    if (data) memcpy(p, data, sz); else memset(p, 0, sz);
    vkUnmapMemory(dev, mem);
}

WuBuVk *wubu_vk_create(void) {
    WuBuVk *vk = (WuBuVk *)calloc(1, sizeof(WuBuVk));
    if (!vk) return NULL;
    VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "wubumedia", .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &ai };
    if (vkCreateInstance(&ici, NULL, &vk->inst) != VK_SUCCESS) goto fail;
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(vk->inst, &n, NULL);
    if (n < 1) goto fail;
    VkPhysicalDevice *devs = (VkPhysicalDevice *)calloc(n, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vk->inst, &n, devs);
    vk->phys = devs[0];
    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nqf, NULL);
    VkQueueFamilyProperties *qfp = (VkQueueFamilyProperties *)calloc(nqf, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nqf, qfp);
    vk->qfam = 0;
    for (uint32_t i = 0; i < nqf; i++) {
        if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { vk->qfam = i; break; }
    }
    free(qfp); free(devs);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = vk->qfam, .queueCount = 1,
                                     .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = 1, .pQueueCreateInfos = &dqci };
    if (vkCreateDevice(vk->phys, &dci, NULL, &vk->dev) != VK_SUCCESS) goto fail;
    vkGetDeviceQueue(vk->dev, vk->qfam, 0, &vk->q);
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                     .queueFamilyIndex = vk->qfam };
    vkCreateCommandPool(vk->dev, &cpci, NULL, &vk->pool);

    VkShaderModule sm;
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize = sizeof(WUBU_VK_CONV_SPV),
                                      .pCode = (const uint32_t *)WUBU_VK_CONV_SPV };
    if (vkCreateShaderModule(vk->dev, &smci, NULL, &sm) != VK_SUCCESS) goto fail;

    VkDescriptorSetLayoutBinding binds[4] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    };
    VkDescriptorSetLayoutCreateInfo dsli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                             .bindingCount = 4, .pBindings = binds };
    vkCreateDescriptorSetLayout(vk->dev, &dsli, NULL, &vk->dsl);
    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 * sizeof(int) };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                       .setLayoutCount = 1, .pSetLayouts = &vk->dsl,
                                       .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
    if (vkCreatePipelineLayout(vk->dev, &pli, NULL, &vk->pl) != VK_SUCCESS) goto fail;

    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &dps };
    vkCreateDescriptorPool(vk->dev, &dpci, NULL, &vk->dp);
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorPool = vk->dp, .descriptorSetCount = 1,
                                         .pSetLayouts = &vk->dsl };
    vkAllocateDescriptorSets(vk->dev, &dsai, &vk->ds);

    VkPipelineShaderStageCreateInfo ss = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                           .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                           .module = sm, .pName = "main" };
    VkComputePipelineCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                       .stage = ss, .layout = vk->pl };
    if (vkCreateComputePipelines(vk->dev, VK_NULL_HANDLE, 1, &cp, NULL, &vk->pipe) != VK_SUCCESS)
        goto fail;
    vkDestroyShaderModule(vk->dev, sm, NULL);

    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = vk->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1 };
    vkAllocateCommandBuffers(vk->dev, &cbai, &vk->cb);
    return vk;

fail:
    wubu_vk_destroy(vk);
    return NULL;
}

void wubu_vk_destroy(WuBuVk *vk) {
    if (!vk) return;
    if (vk->dev) {
        vkQueueWaitIdle(vk->q);
        vkDestroyBuffer(vk->dev, vk->b_in, NULL);  vkFreeMemory(vk->dev, vk->m_in, NULL);
        vkDestroyBuffer(vk->dev, vk->b_w, NULL);   vkFreeMemory(vk->dev, vk->m_w, NULL);
        vkDestroyBuffer(vk->dev, vk->b_b, NULL);   vkFreeMemory(vk->dev, vk->m_b, NULL);
        vkDestroyBuffer(vk->dev, vk->b_out, NULL); vkFreeMemory(vk->dev, vk->m_out, NULL);
        vkDestroyCommandPool(vk->dev, vk->pool, NULL);
        vkDestroyPipeline(vk->dev, vk->pipe, NULL);
        vkDestroyPipelineLayout(vk->dev, vk->pl, NULL);
        vkDestroyDescriptorSetLayout(vk->dev, vk->dsl, NULL);
        vkDestroyDescriptorPool(vk->dev, vk->dp, NULL);
        vkDestroyDevice(vk->dev, NULL);
    }
    if (vk->inst) vkDestroyInstance(vk->inst, NULL);
    free(vk);
}

int wubu_vk_conv1d(WuBuVk *vk,
                   const float *in, int in_ch, int n,
                   const float *w, const float *b,
                   int out_ch, int k, int stride, int pad, int dil,
                   float *out, int n_out) {
    if (!vk || !in || !w || !out) return -1;
    size_t in_sz  = (size_t)in_ch * n * sizeof(float);
    size_t w_sz   = (size_t)out_ch * in_ch * k * sizeof(float);
    size_t b_sz   = (size_t)out_ch * sizeof(float);
    size_t out_sz = (size_t)out_ch * n_out * sizeof(float);
    if (ensure_buf(vk, &vk->b_in, &vk->m_in, &vk->cap_in, in_sz,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return -1;
    if (ensure_buf(vk, &vk->b_w, &vk->m_w, &vk->cap_w, w_sz,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return -1;
    if (ensure_buf(vk, &vk->b_b, &vk->m_b, &vk->cap_b, b_sz,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return -1;
    if (ensure_buf(vk, &vk->b_out, &vk->m_out, &vk->cap_out, out_sz,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return -1;
    upload(vk->dev, vk->m_in, in, in_sz);
    upload(vk->dev, vk->m_w, w, w_sz);
    upload(vk->dev, vk->m_b, b, b_sz);

    VkDescriptorBufferInfo dbi[4] = {
        {vk->b_in, 0, in_sz}, {vk->b_w, 0, w_sz}, {vk->b_b, 0, b_sz}, {vk->b_out, 0, out_sz},
    };
    VkWriteDescriptorSet wds[4] = {0};
    for (int i = 0; i < 4; i++) {
        wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[i].dstSet = vk->ds; wds[i].dstBinding = i; wds[i].descriptorCount = 1;
        wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(vk->dev, 4, wds, 0, NULL);

    VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(vk->cb, &cbbi);
    vkCmdBindPipeline(vk->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipe);
    vkCmdBindDescriptorSets(vk->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vk->pl, 0, 1, &vk->ds, 0, NULL);
    int pc[8] = { in_ch, n, out_ch, k, stride, pad, dil, n_out };
    vkCmdPushConstants(vk->cb, vk->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
    vkCmdDispatch(vk->cb, (n_out + 127) / 128, (out_ch + 3) / 4, 1);
    vkEndCommandBuffer(vk->cb);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
                        .pCommandBuffers = &vk->cb };
    vkQueueSubmit(vk->q, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk->q);

    void *p = NULL;
    vkMapMemory(vk->dev, vk->m_out, 0, out_sz, 0, &p);
    memcpy(out, p, out_sz);
    vkUnmapMemory(vk->dev, vk->m_out);
    return 0;
}
