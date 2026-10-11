#ifndef APP_SANDBOX_H
#define APP_SANDBOX_H

#include "core/geometry/MeshData.h"
#include "core/platform/FrameInput.h"
#include "game/Camera.h"
#include "game/Character.h"
#include "game/GameLoop.h"
#include "game/Model.h"
#include "render/rhi/RenderDevice.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The demo scene: scene assembly, input handling and one frame of drawing.
//
// It talks to IRenderDevice only - buffers, shaders, a pipeline and a command list - so the graphics
// API behind the interface is not visible here, and the geometry it uploads comes from core (MeshGen).
//
// Scene assembly happens in two waves (see createResources):
//   * the preload wave runs before the loop starts and holds whatever the first logic tick reads;
//   * the streaming wave is queued before the loop starts but runs inside it, a bounded number of
//     items per turn, so nothing waits for content that no tick depends on.
class Sandbox
{
public:
    Sandbox();
    ~Sandbox();

    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;

    // Build the pipeline, load the scene and start the loop; requires a device with a swapchain.
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

    // Upload staged_meshes[slot] and remember it as meshes[slot]. Runs from a load queue apply step,
    // i.e. on the main thread between frames - never inside beginFrame/endFrame.
    bool uploadMesh(std::size_t slot);

    void resetCamera();
    void setCameraView(CameraView view);

    // ---- rendering (owned by the device, referenced here) ----
    IRenderDevice *device = nullptr;
    PipelineHandle pipeline = kInvalidHandle;
    ShaderHandle vertex_shader = kInvalidHandle;
    ShaderHandle fragment_shader = kInvalidHandle;
    BufferHandle uniform_buffer = kInvalidHandle;
    std::vector<GpuMesh> meshes;  // one per entry in staged_meshes, filled as the loads run

    // CPU payloads waiting for (or already past) their upload: slot 0 is the environment geometry,
    // the following slots are the streamed props. A load's produce() fills a slot and its apply()
    // consumes it, which is the seam a worker thread would use later on.
    std::vector<MeshData> staged_meshes;

    // Constant blocks: one per draw, and one set of them per frame in flight, laid out at
    // device.uniformBufferAlignment() boundaries. A single block would be overwritten while the GPU
    // may still be reading it (Vulkan/D3D12 run 2 frames in flight); OpenGL gets the same layout and
    // simply ignores the extra slots.
    std::uint32_t uniform_stride = 0;   // bytes per block
    std::uint32_t max_draws = 0;        // blocks per frame
    std::uint32_t frame_index = 0;      // which set of blocks this frame uses

    // ---- scene state ----
    std::vector<Model> models;  // the streamed props, appended as their loads complete
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
