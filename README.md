# stv3d-lab

Win32 + OpenGL 4.3 Core 的「全显式」渲染原型 / 迷你引擎雏形。**架构上已完全没有 Qt**——
窗口是手写 Win32，GL 上下文是 WGL，函数表是手写的 X-macro 加载器，日志/主循环/着色器资源全部自建。

设计取向：**不隐藏任何东西**——

- 不用任何图形封装库（既不用 `QOpenGLShaderProgram`/`QOpenGLBuffer`，也不用 GLAD/GLFW），直接调原生 OpenGL 4.3
- 顶点属性关联显式三步：`glVertexAttribFormat` + `glVertexAttribBinding` + `glBindVertexBuffer`
- GL 入口点是一张手写表（`render/gl/GLFunctions.inc`，43 个函数），一屏能看完
- 摄像机朝向用**四元数**，不保存欧拉角，也不保存 `target`/`up`
- 每个第三方库都在 CMake 里显式 `find_package` + `target_link_libraries`（不靠"顺带带上的 include 路径"）
- 着色器是真实文件（`shaders/*.vert|frag`），运行期从 exe 旁边的 `shaders/` 读取

---

## 1. 环境与依赖

| 项 | 位置 / 版本 |
|---|---|
| 编译器 | MinGW-w64 g++ **13.2.0**（`D:\MinGW`），CMake 4.1.1 |
| vcpkg | `D:\git\repos\vcpkg`，triplet **`x64-mingw-dynamic`** |
| vcpkg 包 | `cpr`（HTTP）、`nlohmann-json`（JSON，待用）、`vulkan-headers`、`vulkan-loader`、`glslang[tools]`（GLSL→SPIR-V） |
| 系统库 | OpenGL：`opengl32` + `GL/glcorearb.h`、`GL/wglext.h`（MinGW 自带）；Vulkan：`vulkan-1.dll`（显卡驱动自带）；D3D11：`d3d11` `dxgi` `d3dcompiler`（MinGW 导入库 + 系统 `D3DCompiler_47.dll`） |
| 显卡 | 本机两块：OpenGL 与 D3D11 走 Intel UHD 630，Vulkan 后端优先选**独显** GeForce GTX 1050 Ti（Vulkan 1.2） |

> Qt 已经**不是**依赖了：`find_package(Qt6)`、AUTOMOC/AUTORCC、`.qrc`、`windeployqt` 全部移除。
> 现在 exe 只依赖 `libcpr.dll`、三个图形 API 的系统 DLL 与系统库——`objdump -p` 可直接验证。

> Vulkan 的三件依赖这样装（本机没有 Vulkan SDK）：
> ```powershell
> vcpkg install vulkan-headers vulkan-loader "glslang[tools]" `
>       --triplet x64-mingw-dynamic --host-triplet x64-mingw-dynamic --recurse
> ```
> `glslang` 的 `tools` feature 才带 `glslangValidator`，缺了它 SPIR-V 就不会生成（CMake 会给警告）。

> vcpkg 在这台机器上没有 MSVC，所以安装任何包都要带 `--host-triplet x64-mingw-dynamic`，
> 否则会去构建 `x64-windows` 的宿主工具并报 `Unable to find a valid Visual Studio instance`。

## 2. 构建 / 运行 / 调试

```powershell
cmake --preset mingw            # 配置（preset 内含编译器、vcpkg toolchain、triplet）
cmake --build --preset mingw    # 构建 → build\stv3d-lab.exe（顺便把 GLSL 编成 SPIR-V）
.\build\stv3d-lab.exe                # 运行（默认 OpenGL 后端）
.\build\stv3d-lab.exe --api vk       # 运行（Vulkan 后端）
.\build\stv3d-lab.exe --api d3d11    # 运行（Direct3D 11 后端）
```

- **后端由命令行选**：`--api gl`（默认）/ `vk` / `d3d11`。选择只发生在 `src/main.cpp`，其余代码只认 `IRenderDevice`
- 着色器一份源码三用：GLSL 由 GL 直接编译、构建期用 `glslangValidator -V` 出 SPIR-V 给 Vulkan、
  HLSL 由 D3D11 在**运行期**用 `D3DCompile` 编译（系统自带 `D3DCompiler_47.dll`，无需构建步骤）
- 资源（`shaders/` 与 SPIR-V）由 `stv3d-assets` 目标**每次构建都同步**，所以改 shader 不必等 exe 重新链接

- VS Code：**F5** = 配置 + 构建 + 调试（`.vscode/tasks.json`、`launch.json` 已接好）；**Ctrl+Shift+B** 只构建
- `build\` 是**自足目录**：构建后自动把 vcpkg 运行时 DLL 与 `shaders/` 放到 exe 旁边，双击即可跑
- **日志**：`build\stv3d-lab.log`（程序不往控制台输出任何内容）
- 若链接报 `cannot open output file stv3d-lab.exe: Permission denied`，是上一次的 `stv3d-lab.exe` 还在运行：
  `Get-Process stv3d-lab | Stop-Process -Force`

### 这台机器上的 Windows / MinGW 陷阱（都实际踩过）

| 现象 | 真正原因 / 处理 |
|---|---|
| vcpkg 报 `Unable to find a valid Visual Studio instance` | 机器上没有 MSVC，任何 `vcpkg install` 都要带 `--host-triplet x64-mingw-dynamic` |
| 链接报 `multiple definition of pthread_mutex_lock` | posix 线程模型下 g++ 隐式传 `-lpthread`，在 MinGW 上解析到**静态** `libpthread.a`；vcpkg 的 `libcpr.dll.a` 又导出同一批 winpthread 符号。**不要** `find_package(Threads)`、不要加 `-lpthread`、更不要用 `std::mutex`（日志改用原子自旋锁就是为了绕开它；强行 `-lwinpthread` 能过，但会把第二份 pthread 实现塞进进程里） |
| `wglGetProcAddress` 返回空，明明驱动支持这个函数 | 它**只**回答 OpenGL 1.1 以上的入口点；`glClear`/`glDrawElements` 这类 1.1 函数必须回退到 `GetProcAddress(opengl32.dll, ...)`（见 `GLFunctions.cpp`） |
| `SetPixelFormat` 第二次调用失败 | 一个窗口的 DC **只能**设置一次像素格式——所以先用临时窗口的旧式上下文去取 `wglChoosePixelFormatARB`，再在真窗口上设格式（见 `GLContext.cpp`） |
| 客户区尺寸和渲染像素不一致 / 画面发虚 | 进程没声明 DPI 感知，Windows 会做虚拟化。启动时 `SetProcessDPIAware()`（`Win32Window::create` 里做的） |
| 切到别的窗口后角色一直往前跑 | 失去焦点时收不到配对的 `WM_KEYUP`，键会"卡住"；`WM_KILLFOCUS` 里把所有键清掉 |
| 结构体成员名 `near` / `far` 编译报奇怪的错 | `<windows.h>` 把它们定义成**宏**，只能叫 `near_plane` / `far_plane`（也正因如此，`Win32Window.h` 刻意不 include `<windows.h>`） |
| 用 PowerShell 截图验证渲染时画面全黑/被切 | 三件事一起做才对：① 先 `SetProcessDPIAware()`（否则拿到的是虚拟像素）；② 把窗口 `SetWindowPos(HWND_TOPMOST)` 置顶（否则抓到的是压在上面的别的窗口）；③ 用 `ClientToScreen` 算出客户区物理坐标再 `CopyFromScreen`。`PrintWindow` 抓不到 GL 区域 |
| `Select-Object -First N` 之后构建/gdb 莫名其妙 exit 1 | PowerShell 提前关管道会**掐死上游进程**，别看被截断的输出，改用 `*> build\build.log` 落盘再读 |
| D3D11：清屏正常、几何完全不出现，且没有任何 API 报错 | 两件事叠在一起：① `D3D11_MAP_WRITE_NO_OVERWRITE` **只对 vertex/index buffer 有效**，常量缓冲必须用 `WRITE_DISCARD`（用它覆写会静默丢掉写入）；② 退一步用 `ID3D11DeviceContext1::*SetConstantBuffers1` 做范围绑定时，本机 Intel 驱动**静默给错数据**。最终方案：每个常量块一个小缓冲 + 经典 `VSSetConstantBuffers` |
| 改了 shader 但运行结果没变 | `POST_BUILD` 只在目标重新链接时才跑；改成 always-run 的 `stv3d-assets` 目标后，每次构建都会同步 `shaders/` 与 SPIR-V |
| D3D11 调试层用不了 | 本机没装 `d3d11sdklayers.dll`（Windows 可选功能 Graphics Tools），`D3D11_CREATE_DEVICE_DEBUG` 会失败；这种时候用"二分诊断"（绕过常量缓冲 / 临时整块绑定）比等工具更快 |

## 3. 目录结构

```
stv3d-lab/
├─ CMakeLists.txt              分层目标（见 §4）+ 着色器/vcpkg 运行时部署
├─ CMakePresets.json           mingw preset（编译器 / vcpkg toolchain / triplet / Debug）
├─ shaders/
│   ├─ basic.vert              顶点着色器（GLSL，Vulkan 用的 SPIR-V 由它生成）
│   ├─ basic.frag              片元着色器
│   └─ basic.hlsl              D3D11 用（一份文件两个入口：VSMain / PSMain）
├─ src/
│   ├─ main.cpp                入口：日志 → Win32 窗口 → WGL 上下文 → 函数表 → 场景 → 裸消息泵
│   ├─ app/
│   │   └─ Sandbox.h/.cpp      场景装配 + 输入处理 + 单帧绘制（原 GLWidget 去掉窗口后的部分）
│   ├─ core/                   ★ 零依赖（无 Qt / 无 GL / 无 OS 头文件，编译期强制，见 §4）
│   │   ├─ core_smoke.cpp      守卫 TU：core 里一旦出现 Qt/GL/OS include 就编译失败
│   │   ├─ math/               vec2/vec3/vec4、mat3/mat4、quat、conventions.h
│   │   ├─ geometry/           Vertex（唯一顶点格式）、Triangle、MeshData、generator/MeshGen
│   │   ├─ platform/           Key、FrameInput、NativeWindowHandle、File（纯 std）
│   │   └─ log/                LogManager：std::ofstream + 原子自旋锁
│   ├─ game/                   → 目标名 stv3d_engine（物理位置仍在 src/game/）
│   │   ├─ Camera.h            Camera：位置 + 四元数朝向 + 投影
│   │   ├─ Model.h             Model：网格引用 + 变换 + 自转
│   │   ├─ Character.h/.cpp    Character(Controller)：位置 + 控制器 + FPV/TPV
│   │   ├─ GameLoop.h/.cpp     GameLoop：固定步长逻辑刻 + 每帧回调 + 任务调度
│   │   └─ InputMapping.h/.cpp 按键 → 意图（纯函数，可单测）
│   ├─ platform/win32/         窗口层
│   │   ├─ Win32Window.h/.cpp  窗口类 + WndProc + 消息泵 + 输入采集
│   │   └─ Win32Module.h/.cpp  exe 所在目录/路径（原来靠 QCoreApplication）
│   ├─ render/                 GPU 侧
│   │   ├─ rhi/RenderTypes.h   句柄 / 枚举 / 描述结构（backend 无关）
│   │   ├─ rhi/RenderDevice.h  IRenderDevice + ICommandList（显式帧模型）
│   │   ├─ gl/GLContext.h/.cpp WGL 上下文：像素格式 + 4.3 core + vsync + SwapBuffers
│   │   ├─ gl/GLFunctions.h/.cpp + .inc  手写 X-macro 函数表
│   │   ├─ gl/GLRenderDevice.h/.cpp      IRenderDevice 的 OpenGL 实现
│   │   ├─ vk/VulkanRenderDevice.h/.cpp  IRenderDevice 的 Vulkan 实现
│   │   ├─ d3d11/D3D11RenderDevice.h/.cpp IRenderDevice 的 D3D11 实现
│   │   └─ resources/          （占位）BufferObject / MeshResource / TextureResource
│   ├─ physics/                （占位）Collider / RigidBody / PhysicsWorld
│   ├─ loader/                 （占位）FBXLoader / GLTFLoader
│   └─ ecs/                    （占位）Entity.h
├─ tests/
│   ├─ core_math_test.cpp      core 数学单测（67 项，纯 g++，无窗口无 GPU）
│   ├─ core_log_test.cpp       日志单测（26 项，含 4 线程并发写）
│   ├─ core_geometry_test.cpp  几何生成单测（17 项：索引范围/绕序/尺寸/法线）
│   └─ engine_test.cpp         engine 单测：摄像机/模型/角色/主循环/输入映射（117 项）
├─ .vscode/                    tasks.json（CMake 构建）/ launch.json（gdb）/ c_cpp_properties.json
├─ .gitignore                  build/、*.exe、*.dll、*.log 等
└─ README.md                   本文件
```

## 4. 分层与 CMake 目标

依赖方向严格向下，**core 与 engine 不许碰 Qt / OpenGL / OS 头文件**：

```
stv3d-lab (exe)   src/main.cpp、src/app/Sandbox.*                     ← 组装根
   ├─ stv3d_platform_win32  src/platform/win32/*                  ← 窗口 / 消息泵 / 输入（user32、gdi32）
   ├─ stv3d_render_gl       src/render/gl/*                       ← OpenGL 后端（opengl32、gdi32）
   ├─ stv3d_render_vk       src/render/vk/*                       ← Vulkan 后端（vulkan-1）
   ├─ stv3d_render_d3d11    src/render/d3d11/*                    ← D3D11 后端（d3d11、dxgi、d3dcompiler）
   ├─ stv3d_render          src/render/rhi/*                      ← RHI 接口（header-only INTERFACE 目标）
   ├─ stv3d_engine          src/game/{Camera.h,Model.h,Character.*,GameLoop.*,InputMapping.*}
   │                                                              ← 只链 core（无 Qt / 无 GL / 无 OS）
   └─ stv3d_core            src/core/*                            ← 零依赖（无 Qt、无 GL、无 OS 头文件）
stv3d_core_tests     tests/core_math_test.cpp      ← 只链 stv3d_core
stv3d_core_log_tests tests/core_log_test.cpp       ← 只链 stv3d_core
stv3d_core_geometry_tests tests/core_geometry_test.cpp ← 只链 stv3d_core
stv3d_engine_tests   tests/engine_test.cpp         ← 只链 stv3d_engine
```

```
stv3d-lab (exe)  main.cpp：裸消息泵 + 游戏循环
 ├─ Win32Window           窗口类 / WndProc / 消息泵 / 输入采集 → FrameInput（HWND 交给渲染后端）
 ├─ IRenderDevice         RHI：swapchain / buffer / shader / pipeline / 帧（A5）
 │    ├─ GLRenderDevice   OpenGL 实现：GLContext + GLFunctions + 一个 command list
 │    ├─ VulkanRenderDevice  Vulkan 实现：instance/device/swapchain/render pass + 命令缓冲（A6）
 │    └─ D3D11RenderDevice   D3D11 实现：device/swapchain/input layout + 运行期 HLSL（A7）
 ├─ Sandbox               场景装配 + 输入处理 + 单帧绘制（只认 RHI）
 │    ├─ GameLoop / TaskScheduler   时间：固定步长逻辑刻 + 每帧回调
 │    ├─ InputMapping               按键 → 意图（纯函数，可单测）
 │    ├─ Character                  角色：位置、控制器、摄像机（FPV/TPV 摆放）
 │    │    ├─ CharacterController   输入意图 → 世界位移
 │    │    └─ Camera                位置 + 四元数朝向 + 投影矩阵
 │    ├─ Model（按 MeshId 引用几何） 场景对象：变换 / 自转
 │    └─ MeshGen（core）→ RHI buffer CPU 几何上传（GPU 句柄只存在于 render 层）
 └─ LogManager            基础设施：日志（静态工具类）
```

### 分层是被"编译期"强制的，不靠自觉

`stv3d_core` 与 `stv3d_engine` 目标里**没有**任何 `find_package`、**也没有** GL/OS 的 include 目录，因此：

- core/engine 头文件里写 `#include <windows.h>` / `#include <GL/gl.h>` / 任何 Qt → **直接编译失败**
- 每个 TU 都能被裸编译器单独编过，这就是解耦的证明（不需要 CMake、不需要 Qt、不需要 vcpkg）：
  ```powershell
  $g='D:\MinGW\bin\g++.exe'
  $vcpkgInc='D:\git\repos\vcpkg\installed\x64-mingw-dynamic\include'
  foreach ($f in 'src/core/core_smoke.cpp','src/core/log/LogManager.cpp','src/core/platform/File.cpp',
                 'src/core/geometry/generator/MeshGen.cpp','src/game/Character.cpp',
                 'src/game/GameLoop.cpp','src/game/InputMapping.cpp','src/render/gl/GLFunctions.cpp',
                 'src/render/gl/GLContext.cpp','src/render/gl/GLRenderDevice.cpp',
                 'src/render/vk/VulkanRenderDevice.cpp','src/render/d3d11/D3D11RenderDevice.cpp',
                 'src/app/Sandbox.cpp',
                 'src/platform/win32/Win32Window.cpp','src/platform/win32/Win32Module.cpp') {
    & $g -std=c++17 -c $f -Isrc "-I$vcpkgInc" -o "$env:TEMP\proof.o"
  }
  ```
  这 15 个文件全部通过 → 全项目没有任何 Qt 残留（`main.cpp` 只多一个 cpr 依赖；
  Vulkan 后端需要 vcpkg 的 `vulkan/vulkan.h`，所以带上 `-I$vcpkgInc`；D3D11 用 MinGW 自带头，不需要额外路径）
- 单测目标只链 core/engine → 不需要窗口和显卡，`ctest` 0.3 秒跑完三个套件
- 依赖方向上也做了保护：`Win32Window.h` 刻意**不** include `<windows.h>`（消息处理器用普通整数声明），
  这样 `near`/`far`/`min`/`max` 这类宏不会泄漏进 engine 和 app

### 新增一个模块时放哪里

| 放哪 | 判据 |
|---|---|
| `src/core/` | 纯算法/数据：数学、几何生成、日志、平台无关的**值类型**（Key/FrameInput/句柄）。**不允许**任何第三方或 OS 依赖 |
| `src/game/` | 场景与规则：模型实例、摄像机、角色、主循环、输入**映射**（可脱离窗口单测） |
| `src/platform/<os>/` | 窗口、消息泵、输入采集、文件路径——**唯一**允许 include OS 头文件的地方 |
| `src/render/<api>/` | 只跟 GPU 打交道：上下文、函数表、缓冲、纹理、着色器、管线（将来 vk/ 与 d3d11/ 与 gl/ 并列） |
| `src/app/` + `src/main.cpp` | 组装：把上面几层接起来，只有这里知道"当前用的是哪个后端" |


## 5. 各类职责与关键接口

### `Sandbox`（src/app/Sandbox.h/.cpp）
场景层（原 `GLWidget` 去掉窗口与图形 API 后的部分）。持有 RHI 资源、模型列表、角色与主循环。

- 生命周期：`createResources(device, exeDir)` 建片元/顶点着色器、pipeline、几何与常量缓冲 →
  `resize(w,h)` 只改摄像机纵横比 → `handleInput(input)` → `render(device)` → `releaseResources()`
- 人称：`enum class CameraView { FPV, TPV }`（定义在 src/game/Camera.h），内部 `setCameraView()`
- 输入：移动意图在逻辑刻里由 `InputMapping::characterInputFromKeys()` 现算；
  `handleInput()` 处理切人称、复位、滚轮（TPV 拉距离 / FPV 变焦）与左键拖拽（FPV 转头 / TPV 轨道）
- 几何：`MeshGen::makeCube` 产出 `MeshData`，经 `device.createBuffer` 变成一对 GPU buffer；
  Sandbox 自己维护 `GpuMesh{ vertex_buffer, index_buffer, index_count, index_format }` 列表，
  模型只记 `MeshId`
- 绘制：`render()` 里 `clear` → `bindPipeline/bindVertexBuffer/bindIndexBuffer/bindUniformBuffer` →
  逐模型 `updateBuffer(常量, VP × modelMatrix())` + `drawIndexed()`
- 机位日志：位置移动 ≥0.5 m 或朝向变化时写一行 `camera[TPV]: eye(...) forward(...)`

### `Win32Window`（src/platform/win32/Win32Window.h/.cpp）
窗口层。`create(config)` 注册窗口类、建窗、显示并把进程声明为 DPI 感知；`pumpMessages()` 非阻塞地
处理整队消息并返回"窗口是否还活着"；输入全部收集进 `FrameInput`（`input()` 读，`endFrame()` 清每帧量）。

- 键：`WM_KEYDOWN/UP` → `Key`（首次按下才算 edge），失焦时清空所有键避免"卡键"
- 鼠标：位置 + 每帧增量、左键（含 `SetCapture`）、滚轮（按 120 一档累加）
- 尺寸：`WM_SIZE` 更新客户区并把 `resized` 置位；`WM_ERASEBKGND` 返回 1（背景由 GL 画）
- 句柄：`nativeHandle()` 返回 `NativeWindowHandle{ HWND, HINSTANCE }` —— 同一份数据将来直接喂给
  `VkWin32SurfaceCreateInfoKHR` 与 DXGI swapchain

### `GLContext` / `GLFunctions`（src/render/gl/）
- `GLContext::create(nativeHandle, config{4.3, core, depth 24, MSAA 4, vsync})`：
  临时窗口取 WGL 扩展 → `wglChoosePixelFormatARB` → `SetPixelFormat`（一窗一次）→
  `wglCreateContextAttribsARB`（4.3 core）→ `makeCurrent` → `wglSwapIntervalEXT`
- `makeCurrent()` / `swapBuffers()` / `destroy()`；`lastError()` 给日志用
- `GLFunctions::load()`：把 `GLFunctions.inc` 里列的 43 个入口点全部解析出来；
  `wglGetProcAddress` 只管 1.1 以上，1.1 的（`glClear`/`glDrawElements`/…）回退到 `GetProcAddress(opengl32.dll, …)`；
  失败时 `missingFunction()` 给出第一个缺的名字

### `InputMapping`（src/game/InputMapping.h/.cpp → `stv3d_engine`，纯函数）
| 输入 | 作用 |
|---|---|
| `W/A/S/D`、方向键 | 角色前后左右（沿摄像机视线的水平投影） |
| `Shift` | 加速（`sprint` 9 m/s，否则 `walk` 5 m/s） |
| `Space` / `E` | 上（已接入 `InputState.jump`，等待物理实现） |
| `C` / `Q` | 下（平台层认识这两个键，但 `InputState` 还没有对应字段） |
| `F5` | 人称切换 FPV ⇄ TPV（用 edge，不是 level） |
| `R` | 机位复位（角色回原点、朝向与轨道偏移复位） |
| `Esc` / 关闭按钮 | 退出 |

### `Camera`（src/game/Camera.h → 属于 `stv3d_engine`，无 Qt）
状态只有 **位置 + 单位四元数 + 透视参数**，全部用 core 类型（`vec3` / `quat` / `mat4`）。

| 分类 | 接口 |
|---|---|
| 位置 | `setPosition` / `getPosition()` / `move(worldDelta)` / `moveLocal(f,r,u)` |
| 朝向 | `setOrientation` / `getOrientation()` / `lookAt(target, worldUp)`（内部换算成四元数，不保存 target/up） |
| 旋转 | `rotateWorld(deg, axis)`（前乘，世界系）、`rotateLocal(deg, axis)`（后乘，局部系）、`yawPitch(yaw, pitch, min, max)` |
| 派生轴 | `forward()` / `right()` / `up()`（由四元数现算，永远正交）、`pitchDegrees()` / `yawDegrees()` |
| 矩阵 | `viewMatrix()`（四元数共轭 + 反向平移，现算）、`projectionMatrix()` |
| 透视 | `setPerspective(fovY, near, far)`（fov 钳制 1°~179°）、`setViewportAspect(aspect)`（非法值钳制为 1）、`getFovYDegrees()` / `getAspect()` / `getNearPlane()` / `getFarPlane()` |
| 裁剪空间 | `setClipDepth(ClipDepth::NegativeOneToOne \| ZeroToOne)`、`setFlipY(bool)` —— **GL 与 Vulkan 的差异是参数**，不是硬编码 |

设计要点：偏航只绕世界 Y、俯仰只绕自身 X ⇒ **滚转恒为 0、无万向锁**；俯仰限位靠"钳制目标俯仰角、
只转差值"实现，**全程不保存欧拉角**；`moveLocal` 因此也不需要任何退化保护。

> `near` / `far` 是 `<windows.h>`（经 Qt 带进来）里的**宏**，所以成员变量叫
> `near_plane` / `far_plane`，不能用裸名 `near` / `far`。

### `Character` / `CharacterController`（src/game/Character.h/.cpp → `stv3d_engine`，无 Qt）
- 控制器：`InputState{forward,backward,left,right,jump,sprint}` + `Speeds{walk=5, sprint=9}`（m/s）；
  `movementDirection(camera)` 取视线**水平投影**（抬头低头不会让角色飞起来）并归一化；
  `update(vec3& position, camera, dt)` 显式传入位置引用（控制器自身不藏位置状态）；
  访问器 `getInput()` / `setInput()`、`getSpeeds()` / `setSpeeds()`、`currentSpeed()`
- 角色：`update(dt)`（推进 + 摆摄像机）、`syncCamera()`、`getPosition()` / `setPosition()`、
  `getCamera()`、`getController()`
- 人称：`setView(CameraView)` / `getView()`；FPV 相机在 `position + (0, eyeHeight=1.6, 0)`，
  **朝向由玩家控制、syncCamera 不覆盖**；TPV 相机在 `position + offset(0,2,5)`，
  `lookAt` 角色头部（`camera_target_height = 1.6`）
- TPV 轨道：`orbitCamera(yaw, pitch)`（四元数旋转偏移向量，俯仰限位 `[-89°, 89°]`，见 `Character.cpp` 常量）、
  `setCameraDistance()`（钳制 0.5~100 m）、`getCameraOffset()` / `cameraDistance()`

### `IRenderDevice` / `ICommandList`（src/render/rhi/ → `stv3d_render`，backend 无关）
应用只跟这一层打交道；具体图形 API 藏在实现里（当前 `GLRenderDevice`）。

| 关注点 | 接口 |
|---|---|
| 帧 | `beginFrame()`（取下一张 back buffer + 让设备 current）/ `endFrame()`（提交 + present）/ `waitIdle()` |
| 命令 | `getCommandList()` → `begin()/end()`、`setViewport`、`clear`、`bindPipeline`、`bindVertexBuffer`、`bindIndexBuffer`、`bindUniformBuffer`、`drawIndexed` |
| 交换链 | `createSwapchain(SwapchainDesc{ NativeWindowHandle, w, h, vsync })`、`resizeSwapchain`、`destroySwapchain`、`swapchainWidth/Height` |
| 资源 | `createBuffer/destroyBuffer/updateBuffer`、`createShader/destroyShader`、`createPipeline/destroyPipeline` |
| 自述 | `backendName()`（写日志用）、`shaderLanguage()`（GLSL / SPIR-V / HLSL，决定应用去读哪个着色器文件）、`lastError()` |

设计要点（这几条决定了它能不能真的换后端）：

- **形状跟最"重"的后端走**：帧是显式获取/提交的，命令只能写在 `begin()/end()` 之间。OpenGL 没有命令缓冲
  （它本身就是状态机），所以 GL 后端的 command list 是"立刻调用"；但接口按 Vulkan/D3D12 的要求定，
  否则以后换过去要推翻重来。
- **资源一律是不透明句柄**（`BufferHandle`/`ShaderHandle`/`PipelineHandle`，0 = 非法），应用拿不到任何 API 对象。
- **着色器是字节块**：`ShaderDesc{ stage, code, size }`。设备用 `shaderLanguage()` 告诉应用它要哪种方言，
  所以换后端 = 换着色器文件，而不是改应用代码。
- **常量走 uniform buffer**（`bindUniformBuffer(buffer, slot)`），不是逐个 uniform：GLSL 用
  `layout(std140, binding = 0) uniform Scene { mat4 uMvp; };`，同一份块结构 Vulkan 与 D3D11 直接可用。
- **v1 的已知取舍**：每帧只用一个 command list、一个 VAO、逐 draw 更新同一个常量缓冲。
  Vulkan/D3D12 下"帧在飞"时不能这样覆写常量，A6/A7 需要换成小环形缓冲或 dynamic offset（代码里有注释标出）。

### `GLRenderDevice`（src/render/gl/GLRenderDevice.h/.cpp）
RHI 的 OpenGL 4.3 实现：持有一个 `GLContext` + `GLFunctions`，把 RHI 调用翻译成 GL 调用。

| RHI | OpenGL |
|---|---|
| `beginFrame` | `makeCurrent` + 按交换链尺寸 `glViewport` + command list begin |
| `begin()/end()` | 只切一个 recoding 标志（GL 调用立即生效） |
| `clear` | `glClearColor` + `glDepthMask(true)` + `glClear` |
| `bindPipeline` | `glUseProgram` + 深度测试/写掩码 + 把顶点布局重新写进 VAO |
| `bindVertexBuffer` | `glBindVertexBuffer(0, id, 0, stride)`（stride 来自 pipeline） |
| `bindIndexBuffer` | `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id)` |
| `bindUniformBuffer` | `glBindBufferBase(GL_UNIFORM_BUFFER, slot, id)` |
| `drawIndexed` | `glDrawElements` |
| `endFrame` | `SwapBuffers`（vsync 由 `wglSwapIntervalEXT(1)` 限帧） |
| `waitIdle` | `glFinish` |

另外提供 `functions()`（GL 函数表）与 `reportErrors(stage)`（读一次 `glGetError` 并写日志）两个后门，
只给组装根和诊断用；应用逻辑不该碰。

### `VulkanRenderDevice`（src/render/vk/VulkanRenderDevice.h/.cpp）
RHI 的 Vulkan 1.0 实现。**它是 RHI 形状的来源**——接口就是按这一层的需求定的，换过来几乎只是"填空"：

| RHI | Vulkan |
|---|---|
| 创建 | instance（+ Win32 surface 扩展，有验证层就开）→ surface（HWND）→ 物理设备（挑有 graphics+present 队列且支持 swapchain 的，独显优先）→ 逻辑设备+队列 |
| `createSwapchain` | swapchain（FIFO、B8G8R8A8_UNORM、minImageCount+1）+ image view + 深度图 + render pass（颜色 CLEAR/STORE → PRESENT_SRC，深度 CLEAR/DONT_CARE）+ framebuffer |
| `beginFrame` | 等本帧 fence → `vkAcquireNextImageKHR` → 复位 fence/命令缓冲 → `vkBeginCommandBuffer` |
| `clear` | `vkCmdBeginRenderPass`（Vulkan 的清屏是"开始 render pass"的一部分，所以它必须是本帧第一条命令）+ 动态 viewport/scissor |
| `bindPipeline` | `vkCmdBindPipeline`（深度测试/写掩码固化在 pipeline 里，viewport/scissor 用动态状态，所以 resize 不用重建 pipeline） |
| `bindVertexBuffer` / `bindIndexBuffer` | `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer`（stride 来自 pipeline 的顶点输入描述） |
| `bindUniformBuffer` | 一组 `UNIFORM_BUFFER_DYNAMIC` 描述符（每帧在飞一个 set）+ `vkCmdBindDescriptorSets` 的**动态偏移** |
| `drawIndexed` | `vkCmdDrawIndexed` |
| `endFrame` | `vkQueueSubmit`（等 image_available、给 render_finished 发信号、fence 收尾）+ `vkQueuePresentKHR` |
| `waitIdle` | `vkDeviceWaitIdle` |

**为什么常量一定要按帧分槽**：Vulkan 允许 2 帧在飞，同一个缓冲如果被下一帧覆写，GPU 可能还在读它。
所以 `Sandbox` 按 `framesInFlight() × 每帧 draw 数` 分配常量块，块间距用 `uniformBufferAlignment()`
（本机 256 字节），绑定范围/动态偏移各取所需 —— Vulkan 与 GL 用的是同一套调用。

clamp 空间也是 RHI 告诉应用的：`clipDepth() = ZeroToOne`、`flipY() = true`，应用把它们交给摄像机
（`Camera::setClipDepth/setFlipY`），所以投影矩阵是**按后端算对的**，而不是在 shader 里打补丁。

v1 有意的简化（都在代码注释里标明）：单队列族（graphics 与 present 同一个家族）、无 MSAA、
所有 buffer 都是 host-visible coherent 常驻映射（不搞 staging/allocator）、render pass 只在第一次建
swapchain 时按格式创建一次、present mode 固定 FIFO、resize 时整体重建 swapchain。

### `D3D11RenderDevice`（src/render/d3d11/D3D11RenderDevice.h/.cpp）
RHI 的 Direct3D 11 实现，也是三个后端里最"短"的一个（D3D11 本身就是立即上下文，很多概念天然对应）。

| RHI | D3D11 |
|---|---|
| 创建 | `D3D11CreateDevice`（feature level 11_1，失败回退 11_0）→ 从 `IDXGIDevice` 拿到 `IDXGIFactory2` → 适配器名写进日志 |
| `createSwapchain` | `CreateSwapChainForHwnd`（`FLIP_DISCARD`、BGRA8、2 个 buffer）+ `MakeWindowAssociation(NO_ALT_ENTER)` → RTV + D32 深度纹理/DSV |
| `beginFrame` | `OMSetRenderTargets` + `RSSetViewports` + command list begin |
| `clear` | `ClearRenderTargetView` + `ClearDepthStencilView`（深度格式无模板，所以只清深度） |
| `bindPipeline` | `IASetInputLayout` + `IASetPrimitiveTopology` + `VSSetShader`/`PSSetShader` + `OMSetDepthStencilState` + `RSSetState` |
| `bindVertexBuffer` / `bindIndexBuffer` | `IASetVertexBuffers`（stride 来自 pipeline）/ `IASetIndexBuffer` |
| `bindUniformBuffer` | 每个 (uniform buffer, 偏移) 槽惰性建一个 64 字节常量缓冲，`Map(WRITE_DISCARD)` 后整块绑定到 b0 |
| `drawIndexed` | `DrawIndexed` |
| `endFrame` | `Present(vsync ? 1 : 0, 0)` |
| `waitIdle` | `Flush()` |
| 着色器 | HLSL 运行期 `D3DCompile`（入口名约定 `VSMain`/`PSMain`）；输入布局的语义按 location 映射：0 → `POSITION`、1 → `COLOR`、其余 → `TEXCOORD<n>` |

**为什么常量不是"一个缓冲 + 偏移"**：D3D11 的 `ID3D11DeviceContext1::*SetConstantBuffers1` 允许
FirstConstant/NumConstants 的范围绑定，但本机 Intel 驱动上它会**静默给出错误数据**（实测：同一 draw
改成整块绑定就正常渲染）。所以后端改成"每个块一个小常量缓冲"，用最经典的
`VSSetConstantBuffers` 绑定 —— 代价是几十字节一个块，好处是**不再依赖 D3D11.1**，也不受驱动差异影响。
应用侧完全不用知道这件事：它依旧只说"第 X 个块"。

### `Model`（src/game/Model.h → `stv3d_engine`，无 Qt / 无 GPU）
- 变换：`setPosition` / `getPosition()` / `translate`（`vec3`）、`setRotation(quat)` / `getRotation()` /
  `setRotationDegrees(deg, axis)` / `rotateBy`、`setScale` / `setUniformScale` / `getScale()`
- 自转：`setSpin(degPerSec, axis)` + `updateSpin(dt)`，读回用 `getSpinDegreesPerSecond()` / `getSpinAxis()`
- 几何引用：**`MeshId`（下标）+ `kNoMesh`**，`setMesh(id)` / `getMesh()` / `hasMesh()`。
  引擎只记"第几份几何"，具体是 CPU 数据还是 GPU buffer 由应用/render 层决定——这正是它能脱离设备单测的原因
- `modelMatrix()`：`mat4::fromTRS(...)`，顺序固定 **缩放 → 旋转 → 平移**

### `Vertex` / `MeshData` / `MeshGen`（src/core/geometry/）
- **`Vertex` 全项目唯一**：`{ vec3 position; vec3 color; vec3 normal; vec2 uv; }`（44 字节，`offsetof` 给出属性偏移）。
  以前 core 与 render 各有一个同名结构，A5 合并掉了
- `MeshData{ std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; }`：纯 CPU 数据，
  带 `empty()` / `triangleCount()` / `clear()`，**不含任何 GPU 句柄**
- `MeshGen::makeCube(size)`：8 顶点 / 36 索引（12 三角形），六个面统一按"从外面看逆时针"绕序
  （原来 back/right/top 三个面绕反了，是新的几何单测抓出来的），角点为彩色、法线取角点方向
- 生成器放 core 的理由：产出物是 `MeshData`，不知道谁会把它传上 GPU，因此可以脱离窗口单测

### `GameLoop` / `TaskScheduler`（src/game/GameLoop.h/.cpp → `stv3d_engine`，**无 Qt**）
- `init()/start()/pause()/exit()`、`isActive()`、`getState()`（位标志 `GameLoopFlags`）、`getTickCount()`
- 固定步长：`getFixedTickSeconds()`（默认 1/60）、`setFixedTickSeconds()`；帧间隔 `setFrameInterval(ms)` / `getFrameInterval()`（默认 16）
- **回调替代了原来的 Qt 信号**：`setTickCallback(cb(tickCount))`、`setFrameCallback(cb(dt))`
- **循环自己不持有时钟/事件循环**：由平台层每个泵循环调一次 `advance()`；
  `advanceBy(elapsedSeconds)` 是确定性的内核（`advance()` 只负责从 `steady_clock` 采一个数给它），
  所以累加器可以用精确数字测，不用在测试里 sleep
- 累加器最多补 5 个逻辑刻，超过就丢弃积压（防"死亡螺旋"）；`enqueue(task)` 的待办在当前步开头执行
- `pause()` 只停逻辑刻，帧与回调照跑；`start()` 兼作"恢复"并重置时钟（暂停期间的时间不会一次性涌进来）
- 时间不活跃时（`pause()` 之后）只跑帧回调；未 `start()` 时除待办队列外什么都不跑
- **不使用 `while` 阻塞循环**：事件泵必须持续运行，阻塞会让窗口失去响应
- `mainLoop(elapsedSeconds)` 是私有单步推进，由 `advance()/advanceBy()` 调用

### `LogManager`（src/core/log/LogManager.h/.cpp → `stv3d_core`，**零依赖，std only**）

一行日志的格式（与旧的 Qt 版本逐字一致）：

```
2026-10-05 22:43:36.797 [INFO] shader program ready: id = 3 | vertex: :/shaders/basic.vert
```

| 接口 | 说明 |
|---|---|
| `init(path)` / `shutdown()` / `isReady()` / `logFilePath()` | 打开（追加模式，写 start 横幅）/ 关闭；`init` 之后重复调用是幂等的 |
| `write(level, message)` | 写一整行；`LOG_*()` 宏内部就用它（`main.cpp` 的 Qt 桥接也用它） |
| `LOG_DEBUG/INFO/WARNING/ERROR/FATAL()` | 返回一个临时 `LogStream`，**整行拼完才落盘**（析构时写入并 flush），所以多线程下也不会把两行搅在一起 |
| `logHex(v, width)` / `logFixed(v, decimals)` | `0x0500` / `-0.080`，替代 Qt 的 `Qt::hex` 与 `QString::arg(...,'f',n)` |
| `LogStream::operator<<(bool)` | 打印 `true` / `false` 而不是 `1` / `0` |

设计要点：

- **路径由调用方给**：`init(path)` 收一个 `std::string`。解析"exe 所在目录"是平台问题（`GetModuleFileNameW`），属于 app/platform 层，core 不该碰。
- **init 之前不静默丢消息**：还没打开文件时写 stderr，比"什么都不打印"好。
- **锁是 `std::atomic_flag` 自旋锁，不是 `std::mutex`**：`std::mutex` 会把 `pthread_mutex_*` 拖进链接，而本机 MinGW 的 `-lpthread` 解析到**静态** `libpthread.a`，vcpkg 的 `libcpr.dll.a` 又导出了同一批 winpthread 符号 → `multiple definition of pthread_mutex_lock`。日志的临界区极短（拼一行、写一行），自旋锁足够，且完全不引用 pthread 符号（见 §2 的 Windows 陷阱）。
- **`<mutex>` 只用来拿 `std::lock_guard`**，`std::mutex` 本身刻意不用。

### `core` 数学层（src/core/math/，零依赖）

| 文件 | 内容 |
|---|---|
| `conventions.h` | **约定唯一出处**：右手系、弧度、列主序 `m[col*4+row]`、`v'=M*v`、`T*R*S`、四元数 `{w,x,y,z}`、正方向右手定则；以及 `ClipDepth{NegativeOneToOne, ZeroToOne}`（GL vs Vulkan 的裁剪空间差异是**参数**而不是硬编码） |
| `vec2/vec3/vec4.h` | 基础向量（点积/叉积/长度/归一化/复合赋值） |
| `mat3.h` | 3x3：`at/set/column/row`、乘法、转置、`determinant`、`inverse`、`scale`、`rotateX/Y/Z`、`fromColumns` |
| `mat4.h` | 4x4：同上的元素访问 + `translation`、`transformPoint/Direction`、`determinant`、`inverse`（奇异→单位阵）、`translate/scale/fromQuat/fromMat3/fromTRS`、`rotateX/Y/Z`、`lookAt`（含退化处理）、`perspective/ortho`（两套裁剪空间 + Y 翻转） |
| `quat.h` | `{w,x,y,z}` 单位四元数：`fromAxisAngle/fromTo`、复合（`(a*b)` 先作用 b）、`conjugate/inverse`、`rotate`、`toMat3`、`slerp`、`dot/normalized` |

### `core` 几何层（src/core/geometry/，零依赖）
`Vertex`（唯一顶点格式，见 §5）、`Triangle`、**`MeshData`**（纯 CPU 数据，不含任何 GPU 句柄），
以及 `generator/MeshGen`（立方体生成器，有单测）。GPU 句柄只存在于 `src/render/`。

## 6. 每帧数据流

```
平台泵（main.cpp 里的裸循环：Win32 消息泵 → advance() → render → SwapBuffers）
 │
 ├─ 累加器按 1/60 推进 N 次（最多补 5 刻）── tick 回调 ──► Sandbox::onTick()
 │                                              ├─ 每个 Model::updateSpin(fixedDt)
 │                                              └─ updateCharacter(fixedDt)
 │                                                   键盘 → CharacterController::InputState
 │                                                        → Character::update(fixedDt)
 │                                                        → 位移 + syncCamera()（按 FPV/TPV 摆放）
 │
 └─ 主循环里直接画：device.beginFrame() → sandbox.render(device) → device.endFrame()
                                                          ├─ ICommandList：clear / bindPipeline /
                                                          │   bindVertexBuffer / bindIndexBuffer /
                                                          │   bindUniformBuffer
                                                          ├─ camera = character.getCamera()
                                                          ├─ VP = projectionMatrix() × viewMatrix()
                                                          └─ 对每个模型：
                                                               device.updateBuffer(ubo, VP × modelMatrix())
                                                               commands.drawIndexed(index_count)
```

> `GameLoop` 不持有时钟也不持有定时器：主循环每转一圈就调一次 `advance()`，逻辑刻与帧的节奏由累加器决定，
> 平台层只负责"什么时候给它时间"。绘制在主循环里同步完成（vsync 由 `wglSwapIntervalEXT(1)` 限帧）。

## 7. 输入映射

完整映射表在 §5 的 `InputMapping`（纯函数、有单测）；这里补充与平台相关的几条：

| 输入 | 作用 |
|---|---|
| 鼠标左键拖拽 | FPV：自由转头（`yawPitch`，俯仰 ±85°）；TPV：绕角色轨道（`orbitCamera`） |
| 滚轮 | FPV：视场角变焦；TPV：轨道拉近/拉远 |
| `F5` | **人称切换 FPV ⇄ TPV**（edge 触发；不用 Tab——系统常拿它做焦点切换） |
| `Esc` / 窗口关闭按钮 | 关闭窗口（正常退出 → 日志收尾 → `exit 0`） |
| 窗口失焦 | 清空所有按下的键（避免收不到对应的 `WM_KEYUP` 而"角色一直走"） |

## 8. 当前场景内容（demo）

| 对象 | 位置 | 缩放 | 自转 |
|---|---|---|---|
| 立方体 1 | (-4, 0, -2) | 1.0 | 30 °/s 绕 Y |
| 立方体 2 | (4, 0, -2) | 1.4 | 60 °/s 绕 X |
| 立方体 3 | (0, 0, -7) | 0.7 | 90 °/s 绕 (1,1,0) |
| 角色占位 | 跟随角色 + (0, 0.9, 0) | 0.6 | 不转 |

几何只上传一份（`1 mesh`），由 4 个模型共享。

## 9. 写代码时遵守的约定

1. **依赖对称**：一个库 = 一次 `find_package` + 一次 `target_link_libraries`
2. **core / engine 不许碰 Qt / GL / OS 头文件**：`src/core/**` 与 `src/game/**` 里出现 `<Q...>` / `<GL...>` / `<windows.h>` 就是分层破坏（编译期由 `stv3d_core` / `stv3d_engine` 目标挡住）
3. **数学约定只认 `core/math/conventions.h`**：列主序、弧度、右手系、`T*R*S`、四元数 `{w,x,y,z}`；GL 与 Vulkan 的裁剪空间差异用 `ClipDepth` 参数表达，不要烤进 core
4. **GPU 资源只通过 `IRenderDevice` 创建/销毁**：拿到的是不透明句柄，销毁前 `waitIdle()`；GL 后端额外要求"上下文 current"（`beginFrame` 之后、`endFrame` 之前最安全）
5. **应用不许 include 具体后端**：`Sandbox` 只认 `render/rhi/`，唯一写出 `GLRenderDevice`/`VulkanRenderDevice` 的地方是 `main.cpp`（加后端时也只改这里 + CMake）
15. **新后端的检查清单**：实现 `IRenderDevice` 全部纯虚函数 → 在 `main.cpp` 加一个开关 → CMake 加一个 `stv3d_render_*` 目标 → 用同一套画面回归验证（见 §10）；RHI 里缺什么就补接口，**不要**在应用里特判后端
6. **顶点属性显式三步**：`glVertexAttribFormat` → `glVertexAttribBinding` → `glBindVertexBuffer`
7. **朝向用四元数**：不引入欧拉角状态；需要限位就"钳制目标角、只转差值"
8. **着色器是磁盘上的真实文件**（`shaders/*.vert|frag`，CMake 构建后复制到 exe 旁边）；改 shader 不用重新编译，重启程序即可
9. **逻辑按固定步长、渲染按帧**：任何随时间变化的量都用 `dt`，不要绑帧率
10. **纯数学类不碰 GL**：`Camera` / `Model` / `Character` / `GameLoop` / `InputMapping` 都能在没有窗口的进程里单测
11. **命名**：成员变量一律**裸名**（不加 `m_` 前缀）；读一个已存成员的访问器写 `getXxx()`（`getPosition()`、
    `getCamera()`、`getMeshPtr()`、`getIndexCount()`），现场算出来的派生量不带 `get`（`forward()`、
    `viewMatrix()`、`cameraDistance()`、`meshCount()`），判定用 `isXxx()` / `hasXxx()`。
    写成员的 setter 用 `setXxx()`，与成员同名时内部显式写 `this->x = x`
12. **类名不带 `My` 前缀**：`Camera` / `Model` / `Character` / `Sandbox` / `GLRenderDevice` / `LogManager`（RHI 接口另用 `I` 前缀：`IRenderDevice` / `ICommandList`）
13. **日志一律走 `LOG_*()`**：不要 `printf`/`std::cout`，一条消息就是一行（`\n` 会被折成 `|`），级别用 `DEBUG/INFO/WARN/ERROR/FATAL`；core/engine 里也不许自己和文件、控制台打交道
14. **平台的脏东西只留在 `src/platform/<os>/`**：`Win32Window.h` 连 `<windows.h>` 都不 include（消息处理器用普通整数声明 + `static_assert` 校验宽度），这样 `near`/`far` 之类宏永远进不了上层

## 10. 测试

### core / engine 层（随项目构建，无 Qt、无窗口、无 GPU）

```
cmake --build --preset mingw      # 会一并构建三个测试目标
ctest --test-dir build            # 或直接跑 build\stv3d_*_tests.exe
```

| 套件 | 覆盖 | 检查项 |
|---|---|---|
| `tests/core_math_test.cpp` | 列主序布局与 `at/column/translation`、乘法与结合、`fromTRS` 的 S→R→T、`lookAt`（含视线与 up 平行退化）、透视投影 **GL[-1,1] 与 Vulkan[0,1]+flipY 两套**、`ortho` 两套、四元数（轴角/复合顺序/共轭/归一化/fromTo/slerp/`fromMat3`↔`toMat3` 一致性）、mat3 逆与行列式、mat4 行列式与逆（含奇异→单位阵） | 67 |
| `tests/engine_test.cpp` | 摄像机（默认机位、viewMatrix 映射、lookAt、世界/局部旋转、俯仰限位、500 次随机旋转后仍无滚转且正交、moveLocal、**GL/Vulkan 两套投影 + flipY**、fov/aspect 钳制）、模型（S→R→T、`fromTRS` 等价、四元数累积、自转积分、负 dt 不推进）、角色控制器（方向/归一化/疾跑/俯视不出水平面）、角色（TPV 摆放与注视、FPV 眼睛高度与朝向不被覆盖、切模式、轨道限位 ±89°、距离钳制 0.5/100）、**GameLoop**（固定步长整除、累加器余数、10 秒卡顿只补 5 刻且丢弃积压、pause 停逻辑刻但帧照跑、start 恢复、`enqueue` 只跑一次、三种 scheduler 任务、零/负 dt 不推进、非法参数回退）、**InputMapping**（空帧无意图、W/↑ 都是前进、SAD+方向键、Space/E 跳跃意图、Shift 疾跑、F5/R/Esc 都是 edge 不粘滞、关闭按钮也算退出、`clearPerFrame` 清每帧量但保留按键与光标） | 117 |
| `tests/core_log_test.cpp` | 行格式（`时间戳 [级别] 内容`、逐位校验时间戳）、四个级别的标签、流式拼接（int/`std::string`/bool）、`logFixed`/`logHex` 的精度与状态还原、**4 线程 × 50 行不丢行不串行**、init/shutdown/再 init 的幂等与追加语义、init 之前写 stderr 不丢消息 | 26 |
| `tests/core_geometry_test.cpp` | 顶点格式尺寸（44 字节，即 pipeline 被告知的 stride）、`MeshData` 的 `empty`/`triangleCount`/`clear`、立方体的顶点/索引数、**索引全在范围内**、**无退化三角形**、**12 个面全部朝外（绕序一致）**、尺寸只改位置不改颜色、法线单位长度且朝外 | 17 |

### 后端怎么验证（每加一个后端都做一次）

单测覆盖不到"画面对不对"，所以后端用**同一套画面回归**检查：启动程序 → 把窗口置顶（DPI-aware +
`ClientToScreen`）→ 抓客户区 → 统计 clear 色之外的像素按列分组，看三组物体的相对位置；三个后端的结果
应落在同一区间。最近一次实测（每次加/改后端都重跑）：

| 后端 | 客户区 | clear 色 | 左立方体 | 中（立方体3+角色） | 右立方体 | 两帧变化 | 日志错误 | 退出码 |
|---|---|---|---|---|---|---|---|---|
| `--api gl`（Intel UHD 630, 4×MSAA, GLSL） | 800×600 | (25,31,38) | 0.242–0.330 | 0.478–0.522 | 0.655–0.778 | 2481 | 0 | 0 |
| `--api vk`（GTX 1050 Ti, 1 sample, SPIR-V） | 800×600 | (25,31,38) | 0.255–0.325 | 0.478–0.522 | 0.658–0.772 | 2435 | 0 | 0 |
| `--api d3d11`（Intel UHD 630, 1 sample, HLSL） | 800×600 | (25,31,38) | 0.245–0.330 | 0.478–0.522 | 0.652–0.785 | 2611 | 0 | 0 |

差别来自 MSAA 与两块 GPU 的光栅化细节；位置一致、都在动、都无错误即视为通过。
"两帧变化"是相隔 1.2 秒两次抓图的像素差——它同时证明了固定步长逻辑刻在跑（模型自转只可能来自 tick）。

### 迁移前的旧测试（已删除，覆盖面对照）

> `camera_test.cpp` / `model_test.cpp` / `shader_test.cpp` 曾放在相邻目录 `../qtcheck/`，它们 `#include`
> 的是**旧头文件名**（`myCamera.h` / `myCharacter.h` / `myMesh.h` / `myModel.h` / `myShader.h`），
> 而这些文件早已不存在（现在叫 `src/game/Camera.h` 等），`shader_test.cpp` 里还硬编码着旧项目路径
> `D:/SetviaHarness/CProj/qtds` —— 也就是说它们早就编不过了，属于死文件，**已删除**（连同 `.exe`）。
> 它们当年的覆盖面现在是这样的：

| 旧文件（已删） | 旧覆盖（当时 61 / 24 / 15 项） | 现在在哪 |
|---|---|---|
| `qtcheck/camera_test.cpp` | 四元数朝向性质（单位长度/无滚转/正交/限位）、lookAt、moveLocal、FPV/TPV 摆放与切换、轨道限位与距离钳制 | `tests/engine_test.cpp` |
| `qtcheck/model_test.cpp` | 模型矩阵顺序 S→R→T、四元数累积、自转积分、网格共享与引用计数 | `tests/engine_test.cpp` |
| `qtcheck/shader_test.cpp` | 着色器文件读取、缺失文件错误路径、无上下文时优雅失败 | 暂无（需要 GL 上下文，属于 render 层） |

> `qtcheck/` 里剩下的都是更早的探针程序（`main.cpp`、`glprobe.cpp`、`jsoncheck.cpp`、`math_debug.cpp`、
> `jsonprobe*/`）与它们的 `build-*` 目录，属于历史调试产物，不参与现在的构建。

## 11. 已知边界 / TODO

**下一阶段：可切换三后端（Qt 已经彻底移除，A0–A7 全部完成）**

| # | 步骤 | 状态 |
|---|---|---|
| A0 | **回退点**：打 tag `qt-final`（指向最后一个带 Qt 的提交 `145af81`） | ✅ 已完成 |
| A1 | **自建日志层**：`core/log` 改成 std-only（`LOG_*()` 流式宏 + 原子自旋锁，格式与 Qt 版逐字一致），30+ 调用点全部改完，core 彻底 Qt-free；新增 `core_log` 单测 26 项 | ✅ 已完成 |
| A2 | **去 Qt 的时间与主循环**：`GameLoop` 改用 `std::chrono::steady_clock` + 回调接口（`setTickCallback/setFrameCallback`、`advance()/advanceBy()`），删掉 `QObject/QTimer/signals/slots`；GameLoop 移入 `stv3d_engine`，新增 33 项单测 | ✅ 已完成 |
| A3 | **Win32 窗口与输入**：`src/platform/win32/{Win32Window,Win32Module}`（`CreateWindowEx` + `WndProc` + 键盘/鼠标/滚轮 → `FrameInput`）+ `core/platform/{Key,FrameInput,NativeWindowHandle}` + 引擎侧 `InputMapping`（可单测） | ✅ 已完成 |
| A4 | **GL 上下文 + 函数表 + 去 qrc**：`render/gl/{GLContext,GLFunctions}`（WGL 建 4.3 core、手写 X-macro 表、`wglGetProcAddress` + 1.1 回退）、`Mesh/ShaderProgram` 改为注入函数表、着色器改磁盘文件、CMake 删掉 Qt/AUTOMOC/qrc/windeployqt → **Qt 归零** | ✅ 已完成 |
| A5 | **抽 `IRenderDevice`**：`render/rhi/{RenderTypes,RenderDevice}` 定义显式帧模型（`beginFrame/endFrame`、`ICommandList`、Pipeline/Buffer/Shader 句柄、Swapchain、`NativeWindowHandle`、`ShaderLanguage`），`render/gl/GLRenderDevice` 先实现；`Sandbox` 只认 RHI；`Model` 改为按 `MeshId` 引用几何；core 合并成唯一 `Vertex` 并新增 `MeshGen`（立方体生成器 + 17 项单测）；删掉 `render/Mesh`、`render/ShaderProgram`、死代码 `core/geometry/Mesh.h` 与三个空占位生成器 | ✅ 已完成 |
| A6 | **Vulkan 后端**：`render/vk/VulkanRenderDevice` 实现同一套 RHI（instance/surface/device/swapchain/render pass/命令缓冲/信号量/fence/动态偏移常量），`main.cpp` 加 `--api gl|vk`；着色器一份源码两用（构建期 `glslangValidator -V` 出 SPIR-V，SPIR-V 要求显式 location，已补）；RHI 补了 `uniformBufferAlignment()`、`framesInFlight()`、`clipDepth()/flipY()`、带 offset/size 的 `bindUniformBuffer`。**实测两后端画面一致**（见 §10 回归表） | ✅ 已完成 |
| A7 | **D3D11 后端**：`render/d3d11/D3D11RenderDevice`（device + `CreateSwapChainForHwnd`(FLIP_DISCARD) + input layout + 运行期 HLSL 编译 + 每块一个小常量缓冲），`shaders/basic.hlsl`（VSMain/PSMain），`main.cpp` 加 `--api d3d11`；三后端画面一致（见 §10） | ✅ 已完成 |

**这一步之后的状态**：目标里的两半都完成了 —— Qt 全清、三个后端可切换。剩下的都是可选深化，按需再做：

| 可选方向 | 说明 |
|---|---|
| 后端细节对齐 | 给 Vulkan/D3D11 补 MSAA、剔除模式、混合、多渲染目标；`PipelineDesc` 已经是加这些的地方 |
| 资源管理 | 纹理/采样器（`IRenderDevice` 加 `createTexture`）、设备内存分配（Vulkan 用 VMA，D3D11 用 staging 上传） |
| 场景层 | `3d.h` 时代的"渲染队列"没做：目前 Sandbox 每帧线性遍历模型，可以换成按 pipeline/材质分组的队列 |
| 工具 | `--api` 之外再加 `--shader-dir`、`--frames N`（跑固定帧数后退出，便于 CI 做画面回归） |

**已完成的 Qt 阶段（历史）**

| # | 步骤 | 状态 |
|---|---|---|
| 1 | **修树 + 分层目标**：根目录文件归位到 `src/`；`stv3d_core` / `stv3d_render_gl` / exe / core 单测 立起来（core 零依赖由编译期强制） | ✅ |
| 2 | **补数学**：新增 `conventions.h`、`quat.h`；`mat3/mat4` 修 bug 并补齐 `lookAt / perspective(两套) / ortho / fromTRS / inverse / determinant`；core 单测 67 项 | ✅ |
| 3 | **换类型**：`Camera.h`、`Model.h`、`Character.*` 从 Qt 数学类型换成 `core` 的 `vec3/quat/mat4`，并独立出 `stv3d_engine` 目标（零 Qt）；engine 单测 67 项 | ✅ |
| 4 | **拆网格**：并入 A5（engine 目前仍以 `shared_ptr<Mesh>` 前置声明引用几何） | ⏳ 并入 |

**其他已知边界**

- 角色只有**水平移动**：没有重力、跳跃、碰撞（`InputState.jump` 已接但控制器未实现）
- `GameLoop` 的 `pause()` / `exit()` 与状态位已由单测覆盖，但应用还没接（可以绑到暂停键上）
- `nlohmann-json` 已安装但未使用；目前**所有参数仍是代码内常量**，可统一抽成 `config.json`
- 自由飞行的调试相机已移除（只剩 FPV / TPV）；如需"上帝视角"可加第三个 `CameraView`
- TPV 俯仰限位为 `[-89°, 89°]`：相机会绕到角色**下方**（当前没有地面，所以不会穿地）
- 网格只有立方体一种；`MeshGen` 可继续加球/平面，或写 OBJ 读取器
- **立方体绕序是 A5 才修好的**：原来 back/right/top 三个面的三角形绕反了（没开背面剔除所以看不出来），
  新的几何单测把它们抓了出来；现在六个面统一 CCW 朝外，将来开 `GL_CULL_FACE` 可以直接用
- **RHI v1 的取舍**：常量按"每 draw × 每帧在飞"分槽（Vulkan 必须如此），但每帧仍只用一个 command list、
  一个顶点/索引缓冲、一条 pipeline；`PipelineDesc` 目前只有深度测试/写掩码，没有剔除模式、混合、多渲染目标——按需再加
- **Vulkan 后端的已知简化**：无 MSAA（GL 侧是 4×）、所有 buffer 走 host-visible coherent 常驻映射、
  单队列族、present mode 固定 FIFO、整个 swapchain 在 resize 时重建、render pass 只按第一次的交换链格式建一次
- **Vulkan 验证层**：装了 `VK_LAYER_KHRONOS_validation` 就会自动开启（日志里会写一行），没装则静默跳过
- `src/core/geometry/generator/VertexGen.h` 里的 `void VertexGen()` 仍是空壳
- **注释语言**：源码注释与日志文案已统一为英文；本 README 按你的要求保持中文
- 阴影、光照、纹理、实例化（`glDrawElementsInstanced`）都还没做
- **拖拽时的光标反馈没有了**：Qt 版拖拽会切 `ClosedHandCursor`，现在只做了 `SetCapture`（要补就是 `Win32Window::setDraggingCursor()`）
- **`main.cpp` 里那个 httpbin 请求**是 Qt 时代留下的 cpr 依赖自检，会阻塞启动约 1 秒直到超时/返回；不需要的话可以删
- **注释语言**：源码注释与日志文案已统一为英文；本 README 按你的要求保持中文
