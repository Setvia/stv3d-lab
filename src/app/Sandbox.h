#ifndef APP_SANDBOX_H
#define APP_SANDBOX_H

#include "core/platform/FrameInput.h"
#include "game/Camera.h"
#include "game/Character.h"
#include "game/GameLoop.h"
#include "game/Model.h"
#include "render/rhi/RenderDevice.h"

#include <cstdint>
#include <string>
#include <vector>

// The demo scene - what used to be GLWidget, minus the window and minus the graphics API.
//
// It talks to IRenderDevice only: buffers, shaders, a pipeline and a command list. Which API is
// behind that interface (OpenGL today, Vulkan or D3D11 later) is not visible here, and the CPU-side
// geometry it uploads comes from core (MeshGen) rather than from a backend-specific mesh class.
class Sandbox
{
public:
    Sandbox();
    ~Sandbox();

    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;

    // Build the pipeline, the geometry and the constant buffer; requires a device with a swapchain.
    // `assetDirectory` is where shaders/ lives (the app passes the executable directory).
    bool createResources(IRenderDevice &device, const std::string &assetDirectory);

    // Release everything the device owns again; call it while the device is still alive
    void releaseResources();

    // Camera aspect only - the viewport belongs to the device's swapchain
    void resize(int width, int height);

    // One frame of input: view switch, reset, wheel zoom, drag look
    void handleInput(const FrameInput &input);

    void render(IRenderDevice &device);

    GameLoop &getGameLoop() { return game_loop; }

    const Character &getCharacter() const { return character; }
    CameraView getCameraView() const { return camera_view; }
    const std::vector<Model> &getModels() const { return models; }

private:
    // Geometry as the renderer sees it: a pair of GPU buffers plus how to draw them
    struct GpuMesh
    {
        BufferHandle vertex_buffer = kInvalidHandle;
        BufferHandle index_buffer = kInvalidHandle;
        std::uint32_t index_count = 0;
        IndexFormat index_format = IndexFormat::UInt32;
    };

    void onTick();
    void updateCharacter(float dt);
    void syncCharacterModel();
    void logCameraPositionIfMoved();
    void drawModel(IRenderDevice &device, ICommandList &commands, const mat4 &view_projection,
                   const Model &model, std::uint32_t block_index);

    void resetCamera();
    void setCameraView(CameraView view);

    // ---- rendering (owned by the device, referenced here) ----
    IRenderDevice *device = nullptr;
    PipelineHandle pipeline = kInvalidHandle;
    ShaderHandle vertex_shader = kInvalidHandle;
    ShaderHandle fragment_shader = kInvalidHandle;
    BufferHandle uniform_buffer = kInvalidHandle;
    std::vector<GpuMesh> meshes;

    // Constant blocks: one per draw, and one set of them per frame in flight, laid out at
    // device.uniformBufferAlignment() boundaries. A single block would be overwritten while the GPU
    // may still be reading it (Vulkan/D3D12 run 2 frames in flight); OpenGL gets the same layout and
    // simply ignores the extra slots.
    std::uint32_t uniform_stride = 0;   // bytes per block
    std::uint32_t max_draws = 0;        // blocks per frame
    std::uint32_t frame_index = 0;      // which set of blocks this frame uses

    // ---- scene state ----
    std::vector<Model> models;  // each element is an independent spinning cube
    Model character_model;      // placeholder model for the character

    Character character;  // the camera is attached to the character; FPV and TPV share it
    CameraView camera_view = CameraView::TPV;
    GameLoop game_loop;
    FrameInput frame_input;  // the input of the current frame, for the logic tick

    // ---- tuning ----
    float orbit_speed = 0.4f;  // degrees per pixel while dragging
    float wheel_step = 0.5f;   // metres per wheel notch (TPV dolly)

    // ---- camera pose log (a line only when position or orientation changed past a threshold) ----
    vec3 last_logged_camera_position;
    vec3 last_logged_camera_forward;
    bool has_logged_camera = false;
    float log_move_threshold = 0.5f;
};

#endif  // APP_SANDBOX_H
