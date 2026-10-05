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
│   │   └─ log/                LogManager（目前仍用 Qt，下一步搬到 platform/qt）
│   ├─ engine → 见下（物理位置仍在 src/game/）
│   ├─ game/                   场景与交互
│   │   ├─ Camera.h            Camera：位置 + 四元数朝向 + 投影（★ 已用 core 数学）
│   │   ├─ Model.h             Model：网格引用 + 变换 + 自转（★ 已用 core 数学）
│   │   ├─ Character.h/.cpp    Character(Controller)：位置 + 控制器 + FPV/TPV（★ 已用 core 数学）
│   │   ├─ 3d.h/.cpp           GLWidget：GL 资源装配 + 输入 + 场景 + 渲染队列（Qt 边界）
│   │   └─ GameLoop.h/.cpp     GameLoop：固定步长逻辑刻 + 每帧回调 + 任务调度（Qt 边界）
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
stv3d-lab (exe)   src/main.cpp、src/game/{3d,GameLoop}.*、src/core/log/*   ← Qt + OpenGL + cpr
   ├─ stv3d_engine     src/game/{Camera.h, Model.h, Character.*}            ← 只链 core（无 Qt / 无 GL）
   ├─ stv3d_render_gl  src/render/*                                         ← OpenGL 后端
   └─ stv3d_core       src/core/*                                           ← 零依赖（无 Qt、无 GL）
stv3d_core_tests   tests/core_math_test.cpp      ← 只链 stv3d_core
stv3d_engine_tests tests/engine_test.cpp         ← 只链 stv3d_engine
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
- 单测目标只链 core/engine → 不需要窗口和显卡，`ctest` 0.2 秒跑完两个套件

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

### `GameLoop` / `TaskScheduler`（src/game/GameLoop.h/.cpp）
- `init()/start()/pause()/exit()`、`isActive()`、`getState()`（位标志 `GameLoopFlags`）、`getTickCount()`
- 固定步长：`getFixedTickSeconds()`（默认 1/60）、`setFixedTickSeconds()`；帧间隔 `setFrameInterval(ms)` / `getFrameInterval()`（默认 16）
- 信号：`ticked(tickCount)`（每次逻辑刻）、`frameStepped(dt)`（每帧）
- 累加器最多补 5 个逻辑刻（防"死亡螺旋"）；`enqueue(task)` 的待办在当前帧开头执行
- **不使用 `while` 阻塞循环**：Qt 事件循环必须持续运行，阻塞会把界面冻死
- `mainLoop()` 是私有单帧推进，由内部 `QTimer` 驱动

### `LogManager`（src/core/log/LogManager.h/.cpp）
> 位置在 `core/` 但实现用了 Qt（`QFile/QString/QDateTime/QMutex`）——它是唯一"名不副实"的文件。
> 下一步把它拆成 `core` 的 `ILogSink` 接口 + `platform/qt` 的实现（或直接用 `std::ofstream` 实现，连 Qt 都不需要）。

- `init()` / `shutdown()` / `logFilePath()` / `isReady()`；日志格式 `时间戳 [级别] 内容`
- 文件对象藏在 `.cpp` 内、`QMutex` 保护（多线程安全）；`QtFatalMsg` 触发 `abort()`

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
GameLoop（QTimer 16ms）
 │
 ├─ 累加器按 1/60 推进 N 次 ── emit ticked ──► GLWidget::onGameTick()
 │                                              ├─ 每个 Model::updateSpin(fixedDt)
 │                                              └─ updateCharacter(fixedDt)
 │                                                   键盘 → CharacterController::InputState
 │                                                        → Character::update(fixedDt)
 │                                                        → 位移 + syncCamera()（按 FPV/TPV 摆放）
 │
 └─ emit frameStepped ──► onGameFrame() ──► update() ──► paintGL()
                                                          ├─ camera = character.getCamera()
                                                          ├─ VP = projectionMatrix() × viewMatrix()
                                                          └─ 对每个模型：
                                                               program.setMat4("uMvp", VP × modelMatrix())
                                                               mesh->draw()   // 显式重放绑定 + glDrawElements
```

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

## 10. 测试

### core / engine 层（随项目构建，无 Qt、无 GPU）

```
cmake --build --preset mingw      # 会一并构建两个测试目标
ctest --test-dir build            # 或直接跑 build\stv3d_core_tests.exe / stv3d_engine_tests.exe
```

| 套件 | 覆盖 | 检查项 |
|---|---|---|
| `tests/core_math_test.cpp` | 列主序布局与 `at/column/translation`、乘法与结合、`fromTRS` 的 S→R→T、`lookAt`（含视线与 up 平行退化）、透视投影 **GL[-1,1] 与 Vulkan[0,1]+flipY 两套**、`ortho` 两套、四元数（轴角/复合顺序/共轭/归一化/fromTo/slerp/`fromMat3`↔`toMat3` 一致性）、mat3 逆与行列式、mat4 行列式与逆（含奇异→单位阵） | 67 |
| `tests/engine_test.cpp` | 摄像机（默认机位、viewMatrix 映射、lookAt、世界/局部旋转、俯仰限位、500 次随机旋转后仍无滚转且正交、moveLocal、**GL/Vulkan 两套投影 + flipY**、fov/aspect 钳制）、模型（S→R→T、`fromTRS` 等价、四元数累积、自转积分、负 dt 不推进）、角色控制器（方向/归一化/疾跑/俯视不出水平面）、角色（TPV 摆放与注视、FPV 眼睛高度与朝向不被覆盖、切模式、轨道限位 ±89°、距离钳制 0.5/100） | 67 |

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

**迁移路线（每一步都能独立编译通过）**

| # | 步骤 | 状态 |
|---|---|---|
| 1 | **修树 + 分层目标**：根目录文件归位到 `src/`；`stv3d_core` / `stv3d_render_gl` / exe / core 单测 立起来（core 零依赖由编译期强制） | ✅ 已完成 |
| 2 | **补数学**：新增 `conventions.h`、`quat.h`；`mat3/mat4` 修 bug 并补齐 `lookAt / perspective(两套) / ortho / fromTRS / inverse / determinant`；core 单测 67 项 | ✅ 已完成 |
| 3 | **换类型**：`Camera.h`、`Model.h`、`Character.*` 从 Qt 数学类型换成 `core` 的 `vec3/quat/mat4`，并独立出 `stv3d_engine` 目标（零 Qt）；engine 单测 67 项 | ✅ 已完成 |
| 4 | **拆网格**：`render/Mesh` 的 GPU 句柄与 `core/geometry/MeshData` 彻底分离，engine 只持有句柄（`Model` 现在仍以 `shared_ptr<Mesh>` 前置声明引用几何） | ⏭ 下一步 |
| 5 | **抽 RHI**：定义 `IRenderDevice`（`createMesh / beginFrame / submit / endFrame`），GL 调用收进 `render/gl/` | ⏭ |
| 6 | **抽 engine**：`3d.h` 拆成场景层（模型列表、角色、输入映射）+ 渲染队列 | ⏭ |
| 7 | **Qt 下沉**：`platform/qt/{QtWindow, QtTimer, QtFileLogSink}` + `app/main` 组装；`core/log/LogManager` 目前仍用 Qt，是唯一"名不副实"的文件 | ⏭ |
| 8 | **Vulkan 后端**：`render/vk/` 实现同一个 `IRenderDevice`（`ClipDepth::ZeroToOne` + `flipY`、SPIR-V、`NativeWindowHandle`） | ⏭ |

**其他已知边界**

- 角色只有**水平移动**：没有重力、跳跃、碰撞（`InputState.jump` 已接但控制器未实现）
- `GameLoop` 的 `pause()` / `exit()` 与状态位尚未被应用使用（可接暂停键）
- `nlohmann-json` 已安装但未使用；目前**所有参数仍是代码内常量**，可统一抽成 `config.json`
- 自由飞行的调试相机已移除（只剩 FPV / TPV）；如需"上帝视角"可加第三个 `CameraView`
- TPV 俯仰限位为 `[-89°, 89°]`：相机会绕到角色**下方**（当前没有地面，所以不会穿地）
- 网格只有立方体一种；`MeshFactory` 可继续加球/平面，或写 OBJ 读取器
- `src/core/geometry/generator/VertexGen.h` 里的 `void VertexGen()` 仍是空壳
- **两处待清理的重复**：`src/core/geometry/Mesh.h` 是重构前的死代码（把 GPU 句柄留在了 core 层，
  没人 include），应删掉或改成纯数据；另外 `Vertex` 这个名字同时被 `core/geometry/Vertex.h`
  （`pos/norm/uv`）与 `render/Mesh.h`（`position/color`）使用，等第 4 步拆网格时统一掉
- 阴影、光照、纹理、实例化（`glDrawElementsInstanced`）都还没做
- **注释语言**：源码注释与日志文案已统一为英文；本 README 按你的要求保持中文
