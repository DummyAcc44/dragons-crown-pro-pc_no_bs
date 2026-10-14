// SPDX-License-Identifier: GPL-2.0-or-later
// The launcher's list of Vulkan devices the renderer can use (the Graphics card row).

#include <cstdio>
#include <cstring>

#include "common/types.h"
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"

extern "C" int dcp_gpu_names(char (*names)[256], int max) {
    using namespace Vulkan;
    int count = 0;
    {
        auto instance = CreateInstance(Frontend::WindowSystemType::Headless, false, false);
        if (!instance) {
            return 0;
        }
        const auto [result, devices] = instance->enumeratePhysicalDevices();
        if (result != vk::Result::eSuccess) {
            return 0;
        }
        for (const auto& device : devices) {
            if (count == max || !IsDeviceUsable(device)) {
                continue;
            }
            const auto properties = device.getProperties();
            const char* name = properties.deviceName.data();
            bool known = false;
            for (int i = 0; i < count; ++i) {
                known |= !std::strcmp(names[i], name);
            }
            if (!known) {
                std::snprintf(names[count++], 256, "%s", name);
            }
        }
    }
    return count;
}
