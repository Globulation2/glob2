// Standalone platform probe; compile against the repository's pinned SDL prefix.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    const bool visible = argc > 1 && std::strcmp(argv[1], "--visible") == 0;
    const int version = SDL_GetVersion();
    std::printf("SDL runtime=%d.%d.%d headers=%d.%d.%d\n",
                SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    if (version != SDL_VERSIONNUM(3, 4, 16) || SDL_VERSION != SDL_VERSIONNUM(3, 4, 16)) {
        std::fprintf(stderr, "Probe requires repository-pinned SDL 3.4.16\n");
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "video initialization failed: %s\n", SDL_GetError());
        return 3;
    }
    std::printf("video=%s GPU drivers=%d\n", SDL_GetCurrentVideoDriver(), SDL_GetNumGPUDrivers());
    bool success = false;
    for (int i = 0; i < SDL_GetNumGPUDrivers(); ++i) {
        const char *driver = SDL_GetGPUDriver(i);
        const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
        std::printf("driver=%s supported=%d\n", driver, SDL_GPUSupportsShaderFormats(formats, driver));
        SDL_GPUDevice *device = SDL_CreateGPUDevice(formats, false, driver);
        if (!device) {
            std::printf("create failed: %s\n", SDL_GetError());
            continue;
        }
        std::printf("created=%s shader_formats=0x%x\n", SDL_GetGPUDeviceDriver(device), SDL_GetGPUShaderFormats(device));
        const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(device);
        std::printf("device=%s driver=%s version=%s\n",
                    SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
                    SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown"),
                    SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "unknown"));
        // Hidden windows may omit swapchains. --visible checks actual clear presentation.
        SDL_Window *window = SDL_CreateWindow("GPU capability probe", 64, 64, SDL_WINDOW_HIDDEN);
        if (window && visible) {
            SDL_ShowWindow(window);
            for (int frame = 0; frame < 10; ++frame) { SDL_PumpEvents(); SDL_Delay(20); }
        }
        if (window && SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
            if (commands) {
                SDL_GPUTexture *texture = nullptr;
                if (SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &texture, nullptr, nullptr)) {
                    // Hidden windows may legitimately have no swapchain texture.
                    bool cleared = false;
                    if (texture) {
                        SDL_GPUColorTargetInfo target{};
                        target.texture = texture;
                        target.clear_color = {0.25f, 0.5f, 0.75f, 1.0f};
                        target.load_op = SDL_GPU_LOADOP_CLEAR;
                        target.store_op = SDL_GPU_STOREOP_STORE;
                        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                        if (pass) { SDL_EndGPURenderPass(pass); cleared = true; }
                    }
                    const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
                    success |= submitted && (!visible || cleared);
                    std::printf("submitted=%d swapchain_texture=%d clear_pass=%d\n", submitted, texture != nullptr, cleared);
                } else {
                    std::printf("acquire failed: %s\n", SDL_GetError());
                    SDL_CancelGPUCommandBuffer(commands);
                }
            }
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseWindowFromGPUDevice(device, window);
        } else {
            std::printf("claim failed: %s\n", SDL_GetError());
        }
        if (window) SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
    }
    SDL_Quit();
    return success ? 0 : 4;
}
