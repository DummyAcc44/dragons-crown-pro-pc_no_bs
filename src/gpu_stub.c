#include "dcpgpu.h"
#include <stdio.h>

int dcpgpu_available(void) { return 0; }
void dcpgpu_register_kernel(void) {}
int dcpgpu_init(const DcpGpuConfig *config) {
    (void)config;
    puts("GPU: video core not built; Gnm and VideoOut imports stay unresolved");
    return 0;
}
uintptr_t dcpgpu_resolve(const char *scoped_nid) { (void)scoped_nid; return 0; }
int dcpgpu_handle_fault(void *context, void *address) { (void)context; (void)address; return 0; }
void dcpgpu_dump_guest_writes(void *context) { (void)context; }
unsigned dcpgpu_symbol_count(void) { return 0; }
int dcpgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8) { (void)initial_utf8; (void)prompt_utf8; return 0; }
int dcpgpu_text_input_poll(char *out_utf8, uint64_t size) { (void)out_utf8; (void)size; return 2; }
int dcpgpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus) {
    (void)title; (void)items; (void)images; (void)count; (void)focus;
    return 0;
}
int dcpgpu_choice_poll(void) { return -2; }
void dcpgpu_choice_set_input(uint32_t (*buttons)(void)) { (void)buttons; }
int dcpgpu_overlay_captures_input(void) { return 0; }
