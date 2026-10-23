#ifndef FNV_D3D9_VK_PREPARED_H
#define FNV_D3D9_VK_PREPARED_H
#include "d3d9_vk_backend.h"
/* Trusted offline-linked pair. No inference, compilation or caps changes.
 * Original code must also exist byte-for-byte in the loaded SDP catalog. */
struct fnv_vk_prepared_stage {
    const void *code,*spirv;
    uint32_t code_bytes,spirv_bytes;
    struct fnv_uniform_plan plan;
};
struct fnv_vk_prepared_program {
    struct fnv_vk_prepared_stage stages[2];
    const struct fnv_vk_vertex_input *inputs;
    uint32_t input_count,sampler_mask;
};
/* Call during exclusive initialization of a fresh backend, before COM create.
 * Identical payloads are shared; distinct original pairs may select different
 * linked payloads for the same original stage. An existing pair is immutable.
 * Missing membership fails before registration. A later failure
 * can leave partial registrations: destroy the backend, do not retry on it.
 * Success copies all consumed storage, creates no shader objects or draws. */
uint32_t fnv_vk_prepare_program(struct fnv_vk_backend *,const struct fnv_vk_prepared_program *,const struct fnv_d3d9_shader_code *,size_t catalog_count);
/* Preflight every program's membership and basic shape before any registration.
 * Subsequent backend failure can be partial. Failed index is SIZE_MAX on
 * success/invalid batch arguments, otherwise the first failing program index. */
uint32_t fnv_vk_prepare_programs(struct fnv_vk_backend *,const struct fnv_vk_prepared_program *const *,size_t programs_count,const struct fnv_d3d9_shader_code *,size_t catalog_count,size_t *failed_index);
#endif
