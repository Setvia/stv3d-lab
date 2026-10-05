# stv3d-lab

Qt6 + OpenGL 4.3 Core 的「全显式」渲染原型 / 迷你引擎雏形。

设计取向：**不隐藏任何东西**——

- 不用 Qt 的 GL 封装类（`QOpenGLShaderProgram` / `QOpenGLBuffer` / `QOpenGLVertexArrayObject`），直接调原生 OpenGL 4.3
- 顶点属性关联显式三步：`glVertexAttribFormat` + `glVertexAttribBinding` + `glBindVertexBuffer`
- 摄像机朝向用**四元数**，不保存欧拉角，也不保存 `target`/`up`
- 每个第三方库都在 CMake 里显式 `find_package` + `target_link_libraries`（不靠"顺带带上的 include 路径"）
- 着色器是真实文件（`shaders/*.vert|frag`），编译期经 `stv3d-lab.qrc` 嵌进 exe

---

## 1. 环境与依赖

| 项 | 位置 / 版本 |
|---|---|
| 编译器 | MinGW-w64 g++ **13.2.0**（`D:\MinGW`），CMake 4.1.1 |
| Qt | 官方预编译 **6.9.3 mingw_64**（`D:\Qt\6.9.3\mingw_64`） |
| vcpkg | `D:\git\repos\vcpkg`，triplet **`x64-mingw-dynamic`** |
| vcpkg 包 | `cpr`（HTTP）、`nlohmann-json`（JSON，待用） |
| 显卡要求 | **OpenGL 4.3**（显式顶点属性绑定 API 的最低版本）；开发机为 Intel UHD 630 / 驱动 4.3.0 |

> vcpkg 在这台机器上没有 MSVC，所以安装任何包都要带 `--host-triplet x64-mingw-dynamic`，
> 否则会去构建 `x64-windows` 的宿主工具并报 `Unable to find a valid Visual Studio instance`。

## 2. 构建 / 运行 / 调试

```powershell
cmake --preset mingw            # 配置（preset 内含编译器、vcpkg toolchain、Qt 路径、triplet）
cmake --build --preset mingw    # 构建 → build\stv3d-lab.exe
.\build\stv3d-lab.exe                # 运行
```

- VS Code：**F5** = 配置 + 构建 + 调试（`.vscode/tasks.json`、`launch.json` 已接好）；**Ctrl+Shift+B** 只构建
- `build\` 是**自足目录**：构建后自动把 vcpkg 运行时 DLL（POST_BUILD 复制）与 Qt 运行时
  （`windeployqt`：Qt6*.dll、`platforms/qwindows.dll`、MinGW 运行时）放到 exe 旁边，双击即可跑
- **日志**：`build\stv3d-lab.log`（Qt 全部日志重定向到文件，命令行不输出任何内容）
- 若链接报 `cannot open output file stv3d-lab.exe: Permission denied`，是上一次的 `stv3d-lab.exe` 还在运行：
  `Get-Process stv3d-lab | Stop-Process -Force`

### 这台机器上的 Windows / MinGW 陷阱（都实际踩过）

| 现象 | 真正原因 / 处理 |
|---|---|
| vcpkg 报 `Unable to find a valid Visual Studio instance` | 机器上没有 MSVC，任何 `vcpkg install` 都要带 `--host-triplet x64-mingw-dynamic` |
| 链接报 `multiple definition of pthread_mutex_lock` | posix 线程模型下 g++ 隐式传 `-lpthread`，在 MinGW 上解析到**静态** `libpthread.a`；vcpkg 的 `libcpr.dll.a` 又导出同一批 winpthread 符号。**不要** `find_package(Threads)`、不要加 `-lpthread`、更不要用 `std::mutex`（日志改用原子自旋锁就是为了绕开它；强行 `-lwinpthread` 能过，但会把第二份 pthread 实现塞进一个有 Qt DLL 的进程） |
| 结构体成员名 `near` / `far` 编译报奇怪的错 | `<windows.h>`（经 Qt 带进来）把它们定义成**宏**，只能叫 `near_plane` / `far_plane` |
| 用 PowerShell 截图验证渲染时"右半边被切掉" | 本机 DPI 缩放 125%，非 DPI-aware 的 PowerShell 会把 `GetWindowRect` 的虚拟像素当物理像素（×1.25 才对齐）；`PrintWindow` 抓不到 GL 区域（黑），要抓就抓全屏再按物理坐标裁剪 |
| `Select-Object -First N` 之后构建/gdb 莫名其妙 exit 1 | PowerShell 提前关管道会**掐死上游进程**，别看被截断的输出，改用 `*> build\build.log` 落盘再读 |

## 3. 目录结构

```
stv3d-lab/
├─ CMakeLists.txt              分层目标（见 §4）+ 运行时部署
├─ CMakePresets.json           mingw preset（编译器 / vcpkg toolchain / triplet / Qt 路径 / Debug）
├─ stv3d-lab.qrc                    资源清单：把 shaders/ 嵌进 exe（AUTORCC）
├─ shaders/
│   ├─ basic.vert              顶点着色器（属性号与 Mesh 的常量一致）
│   └─ basic.frag              片元着色器
├─ src/
│   ├─ main.cpp                入口：QSurfaceFormat(4.3 Core) → 日志 → 窗口 → 事件循环
│   ├─ core/                   ★ 零 Qt / 零 OpenGL（编译期强制，见 §4）
│   │   ├─ core_smoke.cpp      守卫 TU：core 里一旦出现 Qt/GL include 就编译失败
│   │   ├─ math/               vec2/vec3/vec4、mat3/mat4、quat、conventions.h
│   │   ├─ geometry/           Vertex、Triangle、MeshData（纯 CPU 数据）、generator/
│   │   └─ log/                LogManager：★ 已去 Qt（std::ofstream + 原子自旋锁，格式与原来一致）
│   ├─ engine → 见下（物理位置仍在 src/game/）
│   ├─ game/                   场景与交互
│   │   ├─ Camera.h            Camera：位置 + 四元数朝向 + 投影（★ 已用 core 数学）
│   │   ├─ Model.h             Model：网格引用 + 变换 + 自转（★ 已用 core 数学）
│   │   ├─ Character.h/.cpp    Character(Controller)：位置 + 控制器 + FPV/TPV（★ 已用 core 数学）
│   │   ├─ 3d.h/.cpp           GLWidget：GL 资源装配 + 输入 + 场景 + 渲染队列（Qt 边界）
│   │   └─ GameLoop.h/.cpp     GameLoop：固定步长逻辑刻 + 每帧回调 + 任务调度（★ 已去 Qt，引擎层）
│   ├─ render/                 GPU 侧（当前是 OpenGL 后端）
│   │   ├─ Mesh.h/.cpp         Mesh：VAO/VBO/EBO + 显式属性绑定
│   │   ├─ ShaderProgram.h/.cpp ShaderProgram：编译/链接/uniform 位置缓存（uniform 收 core 类型）
│   │   └─ resources/          （占位）BufferObject / MeshResource / TextureResource
│   ├─ physics/                （占位）Collider / RigidBody / PhysicsWorld
│   ├─ loader/                 （占位）FBXLoader / GLTFLoader
│   └─ ecs/                    （占位）Entity.h
├─ tests/
│   ├─ core_math_test.cpp      core 数学单测（67 项，纯 g++，无 Qt 无 GPU）
│   └─ engine_test.cpp         engine 单测：摄像机/模型/角色（67 项，纯 g++，无 Qt 无 GPU）
├─ .vscode/                    tasks.json（CMake 构建）/ launch.json（gdb）/ c_cpp_properties.json
├─ .gitignore                  build/、*.exe、*.dll、*.log 等
└─ README.md                   本文件
```

## 4. 分层与 CMake 目标

依赖方向严格向下，**Qt 只允许出现在最上面**：

```
stv3d-lab (exe)   src/main.cpp、src/game/3d.*                                      ← Qt + OpenGL + cpr
   ├─ stv3d_engine     src/game/{Camera.h, Model.h, Character.*, GameLoop.*}  ← 只链 core（无 Qt / 无 GL）
   ├─ stv3d_render_gl  src/render/*                                         ← OpenGL 后端
   └─ stv3d_core       src/core/*                                           ← 零依赖（无 Qt、无 GL）
stv3d_core_tests   tests/core_math_test.cpp      ← 只链 stv3d_core
stv3d_engine_tests tests/engine_test.cpp         ← 只链 stv3d_engine
stv3d_core_log_tests tests/core_log_test.cpp     ← 只链 stv3d_core
```

```
stv3d-lab (exe)
 ├─ LogManager               基础设施：日志（静态工具类）
 └─ GLWidget                 视图 + 输入 + 场景装配；每帧遍历模型绘制
      ├─ GameLoop / TaskScheduler   时间：固定步长逻辑刻 + 每帧回调
      └─ Character                  角色：位置、控制器、摄像机（FPV/TPV 摆放）   ← stv3d_engine
           ├─ CharacterController   输入意图 → 世界位移
           └─ Camera                位置 + 四元数朝向 + 投影矩阵
      └─ Model → Mesh               场景对象：变换 / 几何（下游是 GPU 资源）
      └─ ShaderProgram              着色器：编译链接 + uniform 缓存（源码来自 qrc）
```

### 分层是被"编译期"强制的，不靠自觉

`stv3d_core` 与 `stv3d_engine` 目标里**没有** `find_package(Qt6)`、**也没有** GL 的 include 目录，因此：

- 任何 core/engine 头文件里写 `#include <QVector3D>` / `#include <GL/gl.h>` → **直接编译失败**
- `src/core/core_smoke.cpp` 是守卫 TU，能被裸编译器单独编过就是解耦的证明：
  `g++ -std=c++17 -c src/core/core_smoke.cpp -Isrc`
- engine 层同样可以用裸编译器验证：`g++ -std=c++17 -c src/game/Character.cpp -Isrc`
  （`GameLoop.cpp` 也一样，它已经不带 Qt 了）
- 单测目标只链 core/engine → 不需要窗口和显卡，`ctest` 0.25 秒跑完三个套件
- 现在还剩 Qt 的地方只有两处：**app 层**（`main.cpp`、`3d.h/.cpp` 的窗口与输入）与 **render 层**
  （`Mesh`/`ShaderProgram` 借用 `QOpenGLFunctions_4_3_Core` 的函数表）；这两处在 A3/A4 一并去掉

### 新增一个模块时放哪里

| 放哪 | 判据 |
|---|---|
| `src/core/` | 纯算法/数据：数学、几何生成、内存、句柄、输入**值类型**。**不允许**任何第三方依赖 |
| `src/render/` | 只跟 GPU 打交道：缓冲、纹理、着色器、管线 |
| `src/game/` | 场景与规则：模型实例、摄像机、角色、主循环、输入**映射** |
| 将来的 `src/platform/` | 窗口、事件泵、文件、定时器——**唯一**可以使用 Qt 的底层实现 |
| `src/main.cpp` | 组装根：把上面几层接起来，Qt 与引擎在这里第一次相遇 |


## 5. 各类职责与关键接口

### `GLWidget`（src/game/3d.h/.cpp）
视图层。持有网格库、模型列表、角色、着色器程序与主循环；把输入翻译成对角色/摄像机的操作。

- 人称：`enum class CameraView { FPV, TPV }`（定义在 src/game/Camera.h），`setCameraView()` / `getCameraView()`
- 摄像机访问：`getCamera()`（就是角色身上那台）、`setCamera(eye, target, up)`、`resetCamera()`
- 角色 / 循环 / 模型：`getCharacter()`、`getGameLoop()`（可 `getGameLoop().enqueue(task)` 或
  `getGameLoop().getScheduler().addTickTask(...)`）、`getModels()`、`meshCount()`
- 输入注入（便于脚本/测试）：`setCameraInput(CameraInput)` / `getCameraInput()`、
  移动速度 `setMoveSpeed()` / `getMoveSpeed()`
- 生命周期：`initializeGL()` 建资源、`resizeGL()` 更新纵横比、`paintGL()` 遍历模型绘制；析构里
  `makeCurrent() → releaseGlResources() → doneCurrent()`

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

### `Mesh` / `MeshFactory`（src/render/Mesh.h/.cpp）
- `struct Vertex { float position[3]; float color[3]; }`（POD，用 `offsetof` 给出属性偏移）
- `Mesh`：RAII、**禁拷贝、可移动**；`create(vertices, indices)` / `destroy()` / `draw()` /
  `isValid()` / `getIndexCount()` / `vertexStride()`
- 属性号常量：`kAttribPos = 0`、`kAttribColor = 1`、`kBindingInterleaved = 0`（与 shader 的 `layout(location=N)` 对应）
- `MeshFactory::makeCube(vertices, indices, size)`：边长可配的立方体（8 顶点 / 36 索引）
- 兼容性保护：没有当前 GL 上下文时 `create()` 直接返回并告警（Qt 内部会在空上下文崩溃）

### `Model`（src/game/Model.h → `stv3d_engine`，无 Qt）
- 引用几何：`std::shared_ptr<Mesh>`（多个模型共享一份 VBO/VAO；`Mesh` 只在 render 层，这里只用前置声明）
- 变换：`setPosition` / `getPosition()` / `translate`（`vec3`）、`setRotation(quat)` / `getRotation()` /
  `setRotationDegrees(deg, axis)` / `rotateBy`、`setScale` / `setUniformScale` / `getScale()`
- 自转：`setSpin(degPerSec, axis)` + `updateSpin(dt)`，读回用 `getSpinDegreesPerSecond()` / `getSpinAxis()`
- 网格引用：`getMesh()`（可写指针）/ `getMeshPtr()`（共享所有权）/ `hasMesh()` / `setMesh()`
- `modelMatrix()`：`mat4::fromTRS(...)`，顺序固定 **缩放 → 旋转 → 平移**

### `ShaderProgram`（src/render/ShaderProgram.h/.cpp）
- `createFromFiles(vertPath, fragPath, attributeBindings)`（支持 `:/...` 资源路径）、`createFromSource(...)`
- `bind()/release()`、`uniformLocation(name)`（查一次后缓存）、`setMat4(const mat4&)/setVec3(const vec3&)/setFloat`
  —— uniform 接口收 **core 类型**，所以上层（engine）不需要知道背后是哪个图形 API
- 编译/链接失败把 GL info log 写进日志；`readTextFile()` 为静态工具方法
- 同样：禁拷贝可移动、无上下文时优雅失败

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
`Vertex{pos,norm,uv}`、`Triangle`、**`MeshData{vertices, indices}`**（纯 CPU 数据，不含任何 GPU 句柄——GPU 句柄属于 `src/render/`），以及 `generator/` 下的几何生成器占位。

## 6. 每帧数据流

```
平台泵（现在：QTimer 16ms 调 game_loop.advance()；A3 起：Win32 消息循环）
 │
 ├─ 累加器按 1/60 推进 N 次 ── tick 回调 ──► GLWidget::onGameTick()
 │                                              ├─ 每个 Model::updateSpin(fixedDt)
 │                                              └─ updateCharacter(fixedDt)
 │                                                   键盘 → CharacterController::InputState
 │                                                        → Character::update(fixedDt)
 │                                                        → 位移 + syncCamera()（按 FPV/TPV 摆放）
 │
 └─ frame 回调 ──► onGameFrame() ──► update() ──► paintGL()
                                                          ├─ camera = character.getCamera()
                                                          ├─ VP = projectionMatrix() × viewMatrix()
                                                          └─ 对每个模型：
                                                               program.setMat4("uMvp", VP × modelMatrix())
                                                               mesh->draw()   // 显式重放绑定 + glDrawElements
```

> `GameLoop` 不再自己起定时器：平台层每转一圈泵就调一次 `advance()`，逻辑刻与帧的节奏由累加器决定，
> 窗口层只负责"什么时候给它时间"。（A2 之后 `core` 与 `engine` 已经完全没有 Qt，剩下的 Qt 只在 app 与 render 层。）

## 7. 输入映射

| 输入 | 作用 |
|---|---|
| `W/A/S/D`、方向键 | 角色前后左右（沿摄像机视线的水平投影） |
| `Shift` | 加速（`sprint` 9 m/s，否则 `walk` 5 m/s） |
| `Space` / `E` | 上（已接入 `InputState.jump`，等待物理实现） |
| `C` / `Q` | 下 |
| 鼠标左键拖拽 | FPV：自由转头（`yawPitch`，俯仰 ±85°）；TPV：绕角色轨道（`orbitCamera`） |
| 滚轮 | FPV：视场角变焦；TPV：轨道拉近/拉远 |
| `F5` | **人称切换 FPV ⇄ TPV**（不用 Tab：Qt 会把它吃掉用于焦点切换） |
| `R` | 机位复位（角色回原点、朝向与轨道偏移复位） |
| `Esc` | 关闭窗口（正常退出 → 日志收尾） |

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
2. **core / engine 不得依赖 Qt/GL**：`src/core/**` 与 `src/game/{Camera.h,Model.h,Character.*}` 里出现 `<Q...>` / `<GL...>` 就是分层破坏（编译期分别由 `stv3d_core` / `stv3d_engine` 目标挡住）
3. **数学约定只认 `core/math/conventions.h`**：列主序、弧度、右手系、`T*R*S`、四元数 `{w,x,y,z}`；GL 与 Vulkan 的裁剪空间差异用 `ClipDepth` 参数表达，不要烤进 core
4. **GL 资源生命周期**：只在上下文有效时创建/销毁（`initializeGL()` / `makeCurrent()` 之后）
5. **GL 句柄类**：RAII、禁拷贝、可移动（`Mesh` / `ShaderProgram`）
6. **顶点属性显式三步**：`glVertexAttribFormat` → `glVertexAttribBinding` → `glBindVertexBuffer`
7. **朝向用四元数**：不引入欧拉角状态；需要限位就"钳制目标角、只转差值"
8. **着色器是真实文件**：改完要重新构建（qrc 是编译期嵌入）；临时想热改可把 `createFromFiles` 换成磁盘路径
9. **逻辑按固定步长、渲染按帧**：任何随时间变化的量都用 `dt`，不要绑帧率
10. **纯数学类不碰 GL**：`Camera` / `Model` / core 数学可以在没有窗口的进程里单测
11. **命名**：成员变量一律**裸名**（不加 `m_` 前缀）；读一个已存成员的访问器写 `getXxx()`（`getPosition()`、
    `getCamera()`、`getMeshPtr()`、`getIndexCount()`），现场算出来的派生量不带 `get`（`forward()`、
    `viewMatrix()`、`cameraDistance()`、`meshCount()`），判定用 `isXxx()` / `hasXxx()`。
    写成员的 setter 用 `setXxx()`，与成员同名时内部显式写 `this->x = x`
12. **类名不带 `My` 前缀**：`Camera` / `Model` / `Character` / `Mesh` / `ShaderProgram` / `GLWidget` / `LogManager`
13. **日志一律走 `LOG_*()`**：不要 `printf`/`std::cout`，一条消息就是一行（`\n` 会被折成 `|`），级别用 `DEBUG/INFO/WARN/ERROR/FATAL`；core/engine 里也不许自己和文件、控制台打交道

## 10. 测试

### core / engine 层（随项目构建，无 Qt、无 GPU）

```
cmake --build --preset mingw      # 会一并构建三个测试目标
ctest --test-dir build            # 或直接跑 build\stv3d_*_tests.exe
```

| 套件 | 覆盖 | 检查项 |
|---|---|---|
| `tests/core_math_test.cpp` | 列主序布局与 `at/column/translation`、乘法与结合、`fromTRS` 的 S→R→T、`lookAt`（含视线与 up 平行退化）、透视投影 **GL[-1,1] 与 Vulkan[0,1]+flipY 两套**、`ortho` 两套、四元数（轴角/复合顺序/共轭/归一化/fromTo/slerp/`fromMat3`↔`toMat3` 一致性）、mat3 逆与行列式、mat4 行列式与逆（含奇异→单位阵） | 67 |
| `tests/engine_test.cpp` | 摄像机（默认机位、viewMatrix 映射、lookAt、世界/局部旋转、俯仰限位、500 次随机旋转后仍无滚转且正交、moveLocal、**GL/Vulkan 两套投影 + flipY**、fov/aspect 钳制）、模型（S→R→T、`fromTRS` 等价、四元数累积、自转积分、负 dt 不推进）、角色控制器（方向/归一化/疾跑/俯视不出水平面）、角色（TPV 摆放与注视、FPV 眼睛高度与朝向不被覆盖、切模式、轨道限位 ±89°、距离钳制 0.5/100）、**GameLoop**（固定步长整除、累加器余数、10 秒卡顿只补 5 刻且丢弃积压、pause 停逻辑刻但帧照跑、start 恢复、`enqueue` 只跑一次、三种 scheduler 任务、零/负 dt 不推进、非法参数回退） | 100 |
| `tests/core_log_test.cpp` | 行格式（`时间戳 [级别] 内容`、逐位校验时间戳）、四个级别的标签、流式拼接（int/`std::string`/bool）、`logFixed`/`logHex` 的精度与状态还原、**4 线程 × 50 行不丢行不串行**、init/shutdown/再 init 的幂等与追加语义、init 之前写 stderr 不丢消息 | 26 |

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

**下一阶段：去 Qt + 可切换三后端（每一步都能独立编译、测试、运行）**

| # | 步骤 | 状态 |
|---|---|---|
| A0 | **回退点**：打 tag `qt-final`（指向最后一个带 Qt 的提交 `145af81`） | ✅ 已完成 |
| A1 | **自建日志层**：`core/log` 改成 std-only（`LOG_*()` 流式宏 + 原子自旋锁，格式与 Qt 版逐字一致），30+ 调用点全部改完，core 彻底 Qt-free；新增 `core_log` 单测 26 项 | ✅ 已完成 |
| A2 | **去 Qt 的时间与主循环**：`GameLoop` 改用 `std::chrono::steady_clock` + 回调接口（`setTickCallback/setFrameCallback`、`advance()/advanceBy()`），删掉 `QObject/QTimer/signals/slots`，累加器逻辑不变；GameLoop 移入 `stv3d_engine`，新增 33 项单测（engine 套件 67 → 100） | ✅ 已完成 |
| A3 | **Win32 窗口与输入**：`src/platform/win32/Win32Window`（`CreateWindowEx` + `WndProc` + 键盘/鼠标/滚轮事件）+ `src/app/main.cpp` 裸消息泵（每圈调 `game_loop.advance()`）；`HWND` 同时是 GL/WGL、`VkSurfaceKHR`、DXGI swapchain 的 native handle | ⏭ 下一步 |
| A4 | **GL 函数加载表 + 去 qrc**：`src/render/gl/GLFunctions`（X-macro 表 + `wglGetProcAddress`，只加载用到的函数），着色器改磁盘/嵌入头；CMake 删掉 Qt/AUTOMOC/qrc/windeployqt → **Qt 归零**，届时做一次画面回归（机位日志 + 像素聚类） | ⏭ |
| A5 | **抽 `IRenderDevice`**：按"显式帧模型"设计（`BeginFrame/EndFrame`、CommandList、Pipeline、Buffer、Swapchain、`NativeWindowHandle`、`ClipDepth`），GL 后端先实现；顺带把 `render/Mesh` 的 GPU 句柄与 `core/geometry/MeshData` 彻底分离 | ⏭ |
| A6 | **Vulkan 后端**：`vcpkg install vulkan-headers vulkan-loader glslang`（本机只有运行时 `vulkan-1.dll`，没有头/导入库），构建期用 `glslangValidator` 把 GLSL 编成 SPIR-V | ⏭ |
| A7 | **D3D11 后端**：MinGW 自带 `d3d11/dxgi/d3dcompiler` 头与导入库，HLSL 运行时编译（系统自带 `D3DCompiler_47.dll`）；RHI 保留将来加 D3D12 的位置 | ⏭ |

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
- 网格只有立方体一种；`MeshFactory` 可继续加球/平面，或写 OBJ 读取器
- `src/core/geometry/generator/VertexGen.h` 里的 `void VertexGen()` 仍是空壳
- **两处待清理的重复**：`src/core/geometry/Mesh.h` 是重构前的死代码（把 GPU 句柄留在了 core 层，
  没人 include），应删掉或改成纯数据；另外 `Vertex` 这个名字同时被 `core/geometry/Vertex.h`
  （`pos/norm/uv`）与 `render/Mesh.h`（`position/color`）使用，等 A5 抽 RHI 时统一掉
- 阴影、光照、纹理、实例化（`glDrawElementsInstanced`）都还没做
- **注释语言**：源码注释与日志文案已统一为英文；本 README 按你的要求保持中文
