#include "dcp_resolution.h"

#include <stdio.h>
#include <string.h>

const DcpResolution dcp_resolutions[DCP_RESOLUTION_COUNT] = {
    {"native", "Native (1080p)", 1920, 1080, 0}, {"480p", "480p", 854, 480, 0},
    {"720p", "720p", 1280, 720, 0},              {"1440p", "1440p", 2560, 1440, 1},
    {"1800p", "1800p", 3200, 1800, 1},           {"4K", "4K", 3840, 2160, 1},
};

#define W 0
#define H 1
/* The base PS4 and PS4 Pro sizes the game picks between with sceKernelIsNeoMode, in the order they appear. */
static const DcpResolutionSite sites[] = {
    {0x5997a8, 1, {0xb8}, 1920, W},       {0x5997ad, 1, {0xb9}, 3840, W},       {0x5997c3, 1, {0xb8}, 1080, H},
    {0x5997c8, 1, {0xb9}, 2160, H},       {0x599921, 1, {0xb8}, 1920, W},       {0x599926, 1, {0xb9}, 3840, W},
    {0x59993c, 1, {0xb8}, 1080, H},       {0x599941, 1, {0xb9}, 2160, H},       {0x599b2f, 1, {0xb8}, 3840, W},
    {0x599b34, 1, {0xbb}, 1920, W},       {0x599b39, 2, {0x41, 0xbe}, 1920, W}, {0x599b4a, 2, {0x41, 0xbd}, 2160, H},
    {0x599b50, 1, {0xba}, 1080, H},       {0x599b5a, 2, {0x41, 0xbf}, 1080, H}, {0x599b79, 1, {0xb8}, 3840, W},
    {0x599ddc, 1, {0xb9}, 3840, W},       {0x599de1, 1, {0xb8}, 1920, W},       {0x599dfc, 1, {0xb9}, 2160, H},
    {0x599e01, 1, {0xb8}, 1080, H},       {0x59a004, 1, {0xb8}, 3840, W},       {0x59a009, 2, {0x41, 0xbc}, 1920, W},
    {0x59a023, 1, {0xb8}, 2160, H},       {0x59a028, 2, {0x41, 0xb8}, 1080, H},
};
#undef W
#undef H

int dcp_resolution_index(const char *key) {
    for (int i = 0; key && i < DCP_RESOLUTION_COUNT; ++i)
        if (!strcmp(key, dcp_resolutions[i].key)) return i;
    return 0;
}

const DcpResolutionSite *dcp_resolution_sites(size_t *count) {
    *count = sizeof(sites) / sizeof(sites[0]);
    return sites;
}

static int fail(char *error, size_t size, const char *what, uint32_t offset) {
    snprintf(error, size, "render resolution patch: %s at image offset 0x%x", what, (unsigned)offset);
    return 0;
}

int dcp_apply_resolution(unsigned char *image, uint64_t image_size, const DcpResolution *resolution, char *error,
                         size_t error_size) {
    const size_t count = sizeof(sites) / sizeof(sites[0]);
    for (size_t i = 0; i < count; ++i) {
        const DcpResolutionSite *site = &sites[i];
        const uint64_t end = (uint64_t)site->offset + site->opcode_length + 4;
        if (end > image_size) return fail(error, error_size, "site outside the image", site->offset);
        const unsigned char *at = image + site->offset;
        uint32_t current;
        memcpy(&current, at + site->opcode_length, 4);
        if (memcmp(at, site->opcode, site->opcode_length) || current != site->value)
            return fail(error, error_size, "unexpected instruction", site->offset);
    }
    for (size_t i = 0; i < count; ++i) {
        const uint32_t value = (uint32_t)(sites[i].height ? resolution->height : resolution->width);
        memcpy(image + sites[i].offset + sites[i].opcode_length, &value, 4);
    }
    return 1;
}
