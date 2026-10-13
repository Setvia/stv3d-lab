# stv3d-lab 分层目标推进计划

> 本文档由四份讨论总结合并去重而成，并对每一条与代码有关的说法做了核实。
> **基线**：commit `ef72d21`，全部事实取自当前代码，不是讨论时的记忆。
>
> **与现有文档的分工**（一份事实一个家）：
> - `README.md`：怎么构建、怎么运行、为什么这么设计
> - `ARCHITECTURE.md`：结构、方法定义、资源链路、常量表
> - 本文档：目标分层、阶段推进、待决断台账、风险台账
>
> 结构图与常量表不在本文档重复，只引用。

**标记约定**

| 标记 | 含义 |
|---|---|
| 【已达成】 | 代码里已经成立，有验证手段 |
| 【进行中】 | 当前阶段的活 |
| 【待你决断】 | 决策权在人，未填结论前不进入实现 |
| 【已决断】 | 已按外部规范或既有约束裁定，结论写在该条目内 |
| 【缓做】/【不做】 | 明确排在后面，或明确排除 |

---

## 1. 定位：三层，不要混着谈

| 层 | 内容 | 状态 | 能否作为当前决策依据 |
|---|---|---|---|
| **L0 个人实验平台** | 亲手重建底层到应用层，让骨架裸露，不被功能淹没 | 【已达成】 | 是，这是当下唯一的事实 |
| **L1 可嵌入的薄抽象渲染内核** | 别人能拿公共头文件编出一个最小可运行程序；内核精简、可裁剪、多后端可插拔 | 未开始 | 是，近期目标的验收对象 |
| **L2 中间件 / 社区事实标准** | 图形领域的"能力该被怎么调用"的定义权 | 愿景 | **否** |

**一条硬约束**：L2 的任何诉求不得反向驱动 L1 的接口决策。理由：L2 是结果不是输入，用它指导接口会得到一堆为想象中的用户预留的抽象。L2 唯一被允许影响设计的地方是第 4 节那条"内核保持开放与稳定"。

**生态位**（保留但压缩，不作为技术输入）：抽象的是"帧怎么画"而不是"模型怎么写"，因此容错空间比上层框架小得多——接口定错，用户是照着错的方式写代码，而不是写不出代码。

**商业模式不进入技术路线**。只保留一条设计约束：内核完全开放、接口稳定，单次定制需求可以接但不修改核心规范。

---

## 2. 现状基线

### 2.1 已达成

- Qt 彻底移除，三个后端可切换（`--api gl|vk|d3d11`），git 有 A1–A7 的对应提交
- `IRenderDevice` / `ICommandList` 抽象成形，接口形状按最重的后端（Vulkan/D3D12 的显式帧模型）定
- `core` 零依赖，由 `core_smoke.cpp` 守卫 TU 编译期强制；`engine` 只链 core
- 常量按「每 draw × 每帧在飞」分槽，布局在启动时一次算定
- 裁剪空间差异（`ClipDepth` / `flipY`）是参数，不烤进 core
- 单测 4 套共 **277 项**（core_math 67 / core_log 26 / core_geometry 17 / engine 167），`ctest` 实测 0.70 s 跑完，不需要窗口和 GPU
- 三后端画面回归验证机制已建立，几何位置与动效一致

### 2.2 尚未具备

- 没有任何资源来自磁盘：唯一几何是 `MeshGen::makeCube`（8 顶点 / 36 索引）
- 没有内存策略：`BufferDesc` 只有 `{size, usage, initial_data}`，不含"创建后是否还会被 CPU 改"
- Vulkan 后端所有 buffer 走 `HOST_VISIBLE | HOST_COHERENT` 常驻映射（`VulkanRenderDevice.cpp:1036`），没有 staging → device-local 路径
- 没有非索引绘制入口：`ICommandList` 只有 `drawIndexed`（`RenderDevice.h:58`）
- 没有纹理、采样器、compute、多渲染目标
- 画面回归靠人工截图统计，不是一条能自动跑的命令
- 没有公共头文件安装规则（`CMakeLists.txt` 无 `install()` / `export()`），唯一消费者是 exe
- 没有项目级命名空间：`Camera` `Model` `Vertex` `IRenderDevice` `BufferHandle` 都在全局命名空间里
- 三后端采样数不一致：GL 4×MSAA（`GLContext.h:23` `samples = 4`），Vulkan 硬编码 `VK_SAMPLE_COUNT_1_BIT`（`VulkanRenderDevice.cpp:598/651/661/1222`），D3D11 无任何采样设置
- `GameLoop::pause/exit` 已测但应用未接
- `nlohmann_json` 已链接未使用

### 2.3 需要更正的说法（四份总结与代码冲突处）

| 原说法 | 代码事实 | 处理 |
|---|---|---|
| 网格"采用 Face 独立顶点布局（未索引）"，并作为**已定决策** | 现状是**索引式**：`MeshData{vertices, indices}`，`makeCube` 8 顶点 / 36 索引，`Sandbox` 固定走 `bindIndexBuffer` + `drawIndexed`；且 RHI **没有**非索引绘制入口 | **已按 glTF 2.0 规范裁决**：索引式正确，撤回每面独立顶点方案（见 D4） |
| 预留的 Vertex：`pos(12) + uv(8) + norm(8) = 28` 字节 | 现状 44 字节：`position(12) + color(12) + normal(12) + uv(8)`；着色器只读位置与颜色（`basic.vert` 属性 0/1），`normal` 与 `uv` 目前**没有消费者**，`uv` 还是全 0 占位 | **已裁决：撤回 28 字节方案**——规范里 `NORMAL` 是 VEC3 / float32，没有 2 分量法线；`COLOR_0` 是规范属性不能删（见 D4） |
| "三后端画面回归结果一致" | 是**列区间近似一致**，不是逐像素一致：GL 用 4×MSAA，VK/D3D11 单采样。一致性缺口进入 Phase 3 | 结论降级为"几何位置与动效一致"；Phase 3 前不得用"三后端一致"当回归结论 |
| engine 单测 117 项 | 167 项（总数 277） | 采用代码数字 |
| "中间件 / 事实标准"作为当前目标 | 无 `install()`、无 `export()`、无项目级命名空间、唯一消费者是 exe | 降到 L2 愿景；对外化列为 Phase 4，是通往 L1 的必经关口 |

---

## 3. 为什么不建在现有平台上（合并后的单一论证）

四份总结里"为什么自己搭"（5 条）和"UE/Unity 的痛点"（5 条）是同一件事的两面，合并为一张表。

| 平台侧事实 | 对我们的含义 |
|---|---|
| 插件绑死在目标引擎的 RHI、资产管线、生命周期上 | 换个引擎就要重写，"作品"与"平台"的边界被焊死 |
| 底层假设是"给游戏用的"：实时、帧驱动、场景树 | 生成类工具多是离线批处理，要吞吐量不要 60fps，硬塞进去一直跟框架假设打架 |
| 资源系统为编辑器离线导入设计 | 对运行时高频动态创建/销毁的资产支持差（GC 卡顿、内存碎片）——这正是我们要面向的场景 |
| 抽象厚重，原生 API 被深度封装 | 很难插入自定义底层代码，版本升级还会让 hack 失效 |
| 对象模型（Actor / GameObject）自带反射、序列化、元数据开销 | 批量加载静态资产时有大量不必要的开销 |
| 体量大、深度耦合、难裁剪 | 不适合网页、嵌入式、短生命周期服务端渲染 |
| 在别家平台上无法从底层切进去验证 | 这正是"同一份输出在三后端下是否一致"必须自己搭才能回答的原因 |
| 文档只讲"怎么用"，不讲"为什么这么设计" | 亲手重建才能回答：draw call 怎么走到 GPU、矩阵在哪个坐标系翻转、资源在帧在飞时被谁覆写 |

**能力边界自知**：资产一致性、虚拟世界长期状态自洽这类终局问题超出当前技术栈进度，采用渐进迭代，先把稳定的底层渲染骨架落地。

---

## 4. 工作方式约束（先于技术路线，贯穿全程）

这一节是四份总结里唯一不该被"阶段化"的部分——它是所有阶段的前提。

1. **一次只消化一个模块**。暂停新增大量功能。吃透一个、完成测试验证之后，再开发下一块。
2. **AI 只出风险清单，不静默修架构隐患**。流程：AI 列风险 → 你独立构思修复方案 → 再对照 AI 方案 → 你定。理由：AI 静默修复会让你看不见缺陷，也丢掉踩坑的学习机会。
3. **接管验收标准**（四条全过才算吃透）：
   - 能画出这个模块的数据流图
   - 能说清它有哪些隐式约束
   - 能在**保持接口不变**的前提下重构它的内部实现
   - 能写出覆盖边界场景的测试
4. **文档随消化进度用自己的语言重写**。摒弃宣传式文本，文档是维护笔记。
5. **反 AI 黑盒检查**：如果一个设计决策你没法解释"为什么不选另一条路"，那它可能不是你做的。
6. **拒绝"好看但底层不可控"的演示**。底层内核稳定优先于短期效果。

---

## 5. 阶段推进

### 阶段依赖

```
Phase 0 骨架完善（句柄 / 内存 / 资源链路）
   └─> Phase 1 加载层
          └─> Phase 2 验证闭环自动化
                 └─> Phase 3 后端能力对齐
                        └─> Phase 4 内核对外化（L0 -> L1 的关口）
                               └─> Phase 5+ 缓做项
```

Phase 2 与 Phase 1 可以部分并行（`--frames N` 不依赖加载器），但顺序上先有内容再从磁盘来，回归才测得到真实路径。

---

### Phase 0 · 骨架完善【进行中】

**优先级**：数据结构、内存管理、资源链路。三者互相咬合，但**顺序不能按名字排**。

**顺序论证**：

- **资源链路是需求来源**——它规定句柄和分配器必须支持什么（几个缓冲、索引不索引、创建后还改不改、什么时候归还）
- **内存管理是约束**——它限制数据结构能怎么长
- **数据结构是承载**——它是前两者的落点

所以实际执行顺序是：**先把链路写成规格（需求清单）→ 据此定句柄与生命周期 → 据此定分配器接口 → 实现最小链路做验证**。链路先"写"后"做"，是因为它的作用是提出需求，不是提供实现。

**第一步选 mesh 而不是 texture 的理由**：RHI 已有完整的 buffer 通路（`createBuffer` / `updateBuffer` / `bindVertexBuffer` / `bindIndexBuffer`），而 texture 需要新增 `createTexture` / `createSampler` / `bindTexture`，Vulkan 侧还要改描述符布局（现在只有一个 dynamic UBO binding）。用最小改动换最大验证。

| 子项 | 产出 | 依赖 |
|---|---|---|
| 0.1 资源链路规格 | 一页纸：每一跳的输入、输出、失败模式、职责归属 | 无 |
| 0.2 句柄与生命周期 | D1 已决断（见第 6 节）；D2 / D3 的结论待定 | 0.1 |
| 0.3 内存分配器接口 | D5 / D6 的结论；`BufferDesc` 的最终字段 | 0.1、0.2 |
| 0.4 最小链路闭环 | 一条从磁盘到渲染、不带 EBO 开关分支的 mesh 通路 | 0.2、0.3 |

**资源链路的职责划分（保留原结论）**：

```
磁盘文件 → 解析器（CPU 字节 → MeshData）→ 上传器（MeshData → staging）
        → 拷贝器（staging → device-local）→ 绑定器 → 帧渲染 → 回收
```

- `loader` 负责"文件 → CPU 数据"
- `resources` 负责"CPU 数据 → GPU 资源 + 生命周期"
- **上传是 resources 的职责**，不是 loader 的

**Phase 0 验收**：能画出 mesh 从磁盘到 GPU 的数据流图；能说清每个句柄的所有权与失效条件；能在不改 RHI 接口的前提下换掉分配器实现；有覆盖边界（分配失败、超预算、句柄失效、重复销毁）的测试。

---

### Phase 1 · 加载层

**入口条件**：Phase 0 的句柄与分配器定案。否则加载器会把错误的资源语义（比如"所有资源都是 host-visible"）固化成事实。

**为什么 glTF 而不是 FBX**：glTF 是 Khronos 开放标准，规范公开，纯标准库可解析；FBX 闭源、平台绑定，与"可迁移、可审计"的设计取向冲突。可行的分离是格式的定义权与所有权分离——但 glTF 从一开始就把"成为标准"和"保持开放"当同一件事。

**落地顺序**：

1. `MeshAsset` 与 `IMeshLoader` 接口（D7）。约束：**必须能塞进 `LoadQueue::produce()`**，即纯 CPU、不碰设备——现有的 `staged_meshes` + `uploadMesh(slot)` 两段式就是那条缝
2. `.gltf` 最小解析：单 mesh、`POSITION` + `NORMAL` + `TEXCOORD_0`（+ `COLOR_0`）、索引、无材质。规范对齐的三条硬要求见 D4 结果 5：索引 `componentType` 接受 uint8 / uint16 / uint32（uint8 加宽到 uint16）；`mode != 4` 明确报错拒绝；保留 `POSITION` 的 `min` / `max`
3. 接进 `Sandbox`，与 `makeCube` 并存（先并列，不替换，保住回归基线）
4. 材质：先只读 `baseColorFactor`。注意它会改变每 draw 常量块的大小，因此 `uniform_stride` 与着色器的 cbuffer 一起动
5. `.glb` 单二进制容器
6. 节点层级与变换（D8）
7. 纹理：需先给 `IRenderDevice` 加 `createTexture` / `createSampler` / `bindTexture`

**glTF 已知的坑（保留）**：

- 两种容器：`.gltf`（JSON + 外部 bin + 外部纹理）与 `.glb`（单二进制）
- `bufferViews`（哪段字节是什么）与 `accessors`（怎么解释）分离，两层要分别对照
- 坐标系：右手系、Y 向上、相机朝 -Z，与 `conventions.h` 一致；但节点变换可能给 `matrix`，也可能给 TRS，两条都要处理
- `Vertex` 可能因切线 / 多套 UV 而需要扩展，而改 `Vertex` 会牵动三个后端——这是第 2.3 节第 2 条的具体兑现

**Phase 1 验收**：一个真实 `.gltf` 文件在三个后端上都能加载并正确显示；解析失败路径有测试；加载走的是 `LoadQueue` 而不是启动阻塞。

---

### Phase 2 · 验证闭环自动化

**现状**：画面回归靠人工截图 + 按列统计 clear 色之外的像素分组。

**产出**：`--frames N`（限制运行帧数后正常退出）+ 固定相机位姿 + 抓帧落盘 + 三后端结果比较，一条命令给出差异报告。

**为什么它排在这里**：这是唯一能自动回答"我改 RHI 有没有破坏后端一致性"的机制。Phase 3（后端能力对齐）和 Phase 4（对外化）都要靠它，没有它就只能靠肉眼。

**入口条件**：可以早于 Phase 1 完成，但 Phase 1 之后才测得到"资源从磁盘来"的真实路径。

---

### Phase 3 · 后端能力对齐

| 项 | 现状 | 备注 |
|---|---|---|
| MSAA | GL 4×，VK/D3D11 单采样 | `GLContext.h:23` vs `VulkanRenderDevice.cpp:598/651/661/1222`；D3D11 无采样设置 |
| 剔除 / 混合 | `PipelineDesc` 只有 `depth_test` / `depth_write` | 需要扩字段，是 RHI 接口变化 |
| 非索引绘制 | `ICommandList` 只有 `drawIndexed` | 规范允许 `indices` 缺省（非索引几何），加载器必须画得出来；当前缺这条通路 |
| device-local 内存 | Vulkan 全走 host-visible coherent | 依赖 D6 |
| 多渲染目标 / 离屏 | 无 | 更靠后 |

**为什么这算"骨架"而不是"上层功能"**：它不是加效果，是消掉三个后端之间的语义差。语义差存在时，"同一份输出在三后端一致"这个核心验证命题就不成立。

---

### Phase 4 · 内核对外化（L0 → L1 的关口）

**现状缺口（已核实）**：无 `install()` / `export()`；无项目级命名空间（`Camera` `Model` `Vertex` `IRenderDevice` 都在全局）；无版本策略；无后端插件注册入口；唯一消费者是 exe。

**决策点**：D9 原生穿透契约。

**验收**：一个不是你的人，只靠公共头文件 + 一份 README，能编出一个最小可运行程序。

**可提前的最小切片**（建议，属于判断而非规定）：只加项目级命名空间 + 一个 `examples/` 下的最小消费者，不追求完整对外化。理由：**自产自销会系统性地掩盖接口问题**——所有别扭的地方都被"我知道它什么意思"抹平了。这一步成本低，暴露问题的效率高。

---

### Phase 5+ · 缓做 / 不做

| 项 | 处置 | 理由 |
|---|---|---|
| 迁移 / 引擎插件包格式（UE Plugin、UPM 那类注册与加载机制） | 【缓做】 | 骨架稳定后再验证"可迁移"链路 |
| 阴影、光照、后处理、实例化 | 【缓做】 | 骨架稳了再加 |
| 场景树 / ECS（`ecs/Entity.h` 占位） | 【缓做】 | 引入层级会渗进 `Model`、`Sandbox` 和所有遍历，需要先有真实需求 |
| 物理（`physics/` 占位） | 【缓做】 | 同上 |
| 交互式动态叙事（超梦类） | 【不做】 | 不纳入规划 |
| 商业模式的路线化 | 【不做】 | 只保留"内核开放稳定"这一条设计约束 |
| 迁就一次性风口需求的补丁 | 【不做】 | 见第 7 节"需求绑架" |

---

## 6. 决策台账

**使用方式**：标【待你决断】的条目不填结论就不进入实现；标【已决断】的条目已给出结论与依据。每条给出选项、判据、已核实的事实、一个倾向、以及**可证伪条件**（什么事实出现时这个倾向就是错的）。倾向本身不是结论。

### D1 · 句柄是否需要 generation 字段【已决断：需要】

**核实方法**：逐个平台拉真实源码与规范原文，不是看文档描述、也不是凭印象。UE 的 RHI 源码需要授权，因此走 Epic 公开的按符号 API 文档（由该源码生成），下面明确标注。

| 平台 | 句柄类型 | generation | 失效检查的作用 |
|---|---|---|---|
| Godot | `RID` = `uint64_t`：低 32 index，高 32 validator | 有 | **硬闸门**：`get_or_null()` 校验失败返回 `nullptr` |
| wgpu | `Id` = `RawId(NonZeroU64)`：低 32 index，高 32 epoch | 有 | **硬闸门**：`Element::Occupied(T, Epoch)` 查表比对 |
| Unity（6000.7 / master） | `EntityId` = 64 位：Version 24 位 / TypeId 12 位 / Index 28 位 | **有** | **硬闸门**：`EntityIdStore` 查找时比对 version，不匹配返回 null |
| Unity（2022.3–6000.3，历史） | `m_CachedPtr`（原生裸指针）+ 32 位 `InstanceID` | **无** | 原生把缓存指针置空 → fake null、`MissingReferenceException` |
| Filament | `HandleId` = `uint32_t`：低 27 index，bit 27–30 age（4 位），bit 31 heap 标志 | 有 | **仅诊断**：`FILAMENT_CHECK_POSTCONDITION` 报 use-after-free，可关闭 |
| Unreal | **不用句柄**：`TRefCountPtr<FRHIBuffer>` 一类引用计数指针（`FBufferRHIRef`…） | **不适用** | 无槽位表、无索引、无 generation；ABA 结构上不存在 |
| bgfx / bx | `uint16_t` 槽位号 | **无** | `isValid()` 只证明槽位活跃，**复用的旧句柄会通过校验** |
| D3D12 / Vulkan | COM 引用计数接口 / 不透明句柄 | **无** | 规范只保证"对象生命周期内唯一"，明确允许句柄值复用；有效性是调用方的义务 |

**源码事实（可复查）**

Godot `core/templates/rid.h` + `rid_owner.h`：

```cpp
_ALWAYS_INLINE_ uint32_t get_local_index() const { return _id & 0xFFFFFFFF; }

// 分配：高 32 位放一个全局递增的 validator
uint32_t validator = 1 + (uint32_t)(_gen_id() % 0x7FFFFFFF);
uint64_t id = validator; id <<= 32; id |= free_index;

// 查找：validator 不匹配即无效句柄
uint32_t validator = uint32_t(id >> 32);
if (unlikely(c.validator != validator)) { ...; return nullptr; }
```

wgpu `wgpu-core/src/id.rs` + `storage.rs`（v24.0.0）：

```rust
/// There is one live id with this index, allocated at the given epoch.
Occupied(T, Epoch),

pub fn zip(index: Index, epoch: Epoch) -> RawId {
    let v = (index as u64) | ((epoch as u64) << 32);
    Self(NonZeroU64::new(v).unwrap())
}
```

Unity `Runtime/Export/Scripting/EntityIdStore.bindings.cs`（6000.7）：

```csharp
// 位布局与原生 EntityId 一致：Version:24 / TypeId:12 / Index:28

// 复用槽位时步进版本（+2：奇数表示活着）
uint newVersion = (version + 2) & (uint)k_SlotVersionMask;

// 查找时校验版本，不匹配即失效
internal static bool Exists(EntityId entity) {
    if (entity.Version == 0) return false;
    ref EntitySlot slot = ref GetSlot(entity.Index);
    return SlotGetVersion(slot.versionAndChunk) == entity.Version;
}
```

Filament `private/backend/HandleAllocator.h`：

```cpp
static constexpr uint32_t HANDLE_AGE_BIT_COUNT = 4;
static constexpr uint32_t HANDLE_AGE_SHIFT = 27;
static constexpr uint32_t HANDLE_HEAP_FLAG = 0x80000000u;
```

bgfx `bx/include/bx/inline/handlealloc.inl`：

```cpp
inline bool HandleAlloc::isValid(uint16_t _handle) const {
    uint16_t index = sparse[_handle];
    return index < m_numHandles && dense[index] == _handle;
}
```

**结论 0 · 先排除"不用句柄"那一族**

核实中最重要的发现是：**UE 的 RHI 根本没有句柄**。`FRHIResource` 是个 C++ 类，公开面里只有 `AddRef()` / `Release()` / `GetRefCount()` / `IsValid()`，资源引用一律是 `TRefCountPtr<T>`；没有槽位表、没有索引、没有 generation。它的安全来自"引用计数 + 延迟销毁"：`Release()` 归零不立即 delete，而是推进 pending-delete 队列（`MarkedForDelete` / `PendingDeletes` / `FlushPendingDeletes`、`FRenderCommandFence`、`FDeferredCleanupInterface`），等渲染命令队列排空再真正释放。

这一族里 **ABA 结构上不存在**——资源对象不会在别人还持有引用时被复用。所以严格说，UE 对"要不要 generation"的回答是"这个问题不成立"。

它对本项目**不可直接移植**，原因是架构形状不同：UE 把引用计数对象交给调用方持有；本项目 RHI 已经把"资源一律是不透明句柄、应用拿不到任何 API 对象"定为设计决策（见 `ARCHITECTURE.md` §6）。要采纳它就得改成智能指针语义，所有权模型、后端对象生命周期、显式帧模型都要跟着动。**本条按句柄那一族推进，但这条岔路必须被显式排除，而不是被忽略**——这正是"能解释为什么不选另一条路"的应用。

**结论 1 · 布局采用 Godot / wgpu：64 位，低 32 位 index + 高 32 位 generation**

Godot 与 wgpu 两个互不相关的引擎收敛到**逐位相同**的布局；Unity 走同一族（64 位、位域切分），只是把一部分位宽分给了 type。三个实现同族，比任何单一实现的权威性都强。

**结论 2 · 真正的决策不是"加不加字段"，而是"硬闸门还是诊断层"**

- **硬闸门**（Godot、wgpu、Unity 新版）：查表比对，不匹配即失效。代价是一次整数比较（可忽略）加句柄宽度 32→64
- **诊断断言**（Filament）：只在 debug 检查并报错，release 可关。它的 age 只有 **4 位**，每 16 次复用就回绕——因为它不需要证明，只需要大概率抓到并说清

取**硬闸门**。理由：位宽与比较成本都不是瓶颈，而"陈旧句柄静默指向另一个资源"属于不崩不报错的一类 bug；既然成本可以忽略，就不要留一条只有 debug 构建才看得见的路径。

**结论 3 · `kInvalidHandle = 0` 保留，generation 从 1 起**

Godot 的 `base_id{1}` 就是这个做法。Unity 是另一条等价路线：`if (entity.Version == 0) return false;`——版本 0 永不匹配任何活对象。两者都保住了"全 0 句柄天然无效"。

**Unity 的答案本身是分裂的，这一点必须说清**

- `UnityEngine.Object` 那条线（`Texture` / `Mesh` / `Material`）：带 version 的 64 位 `EntityId`，查找时校验
- `GraphicsBuffer` / `ComputeBuffer`：**不是** `UnityEngine.Object`（Scripting API 里没有 "Inherits from"）。它们持有裸 `IntPtr m_Ptr`，`IsValid()` 就是 `m_Ptr != IntPtr.Zero && IsValidBuffer(this)`，而 `GraphicsBufferHandle` 只是一个裸 `UInt32`（`GfxBufferID`）
- 也就是说：**最接近本项目"缓冲句柄"的那条路径，Unity 并没有公开可见的 generation**

所以不要把"Unity 用了 generation"过度推广到缓冲上。这**削弱**了 Unity 对本条的证明力，但结论不变——因为结论本来就不依赖 Unity：布局依据来自 Godot 与 wgpu 的逐位收敛，硬闸门取舍来自 Godot / wgpu / Unity 与 Filament 的对比。

**Unity 的迁移史仍然值得单独记**

Unity 不是一直这么做的：2022.3 到 6000.3 是 `m_CachedPtr` + 32 位 `InstanceID`、没有版本，销毁靠原生置空缓存指针（fake null）；到 6000.7 / master 才改成带 24 位 version 的 64 位 `EntityId` 并在查找时校验。

也就是说，**一个主流引擎在有十年积累之后，专门回头把 generation 加进对象标识**，而且是破坏性变更（旧的 `GetInstanceID()` 被标记 `Obsolete`，因为它只取低 32 位、丢掉 version，无法区分复用后的 id）。对我们的直接含义是：**现在加是改类型，以后加是破坏性变更**——正对应风险台账里的"对外化债"。

**为什么 bgfx 可以不加**：`HandleAlloc` 的 dense/sparse 结构解决的是 O(1) 分配回收，不是失效检测；它的 `isValid` 只回答"这个槽位现在活跃吗"，不回答"这是不是当初那个资源"。选它等于接受 ABA——这是取舍不是错，但不适合"别人持有句柄"的库。

**D3D12 / Vulkan 不提供任何帮助**：Vulkan 规范只保证句柄值"在对象生命周期内唯一"，并明确允许 non-dispatchable 句柄值不唯一、可复用；有效性（"未被删除过"）是调用方的义务。所以 generation 只能由我们这一层自己加。

**与 D2 的依赖**：generation 只在"槽位会被复用"的前提下才有意义。若 D2 最终定成"资源只增不回收、只在整设备销毁时统一释放"，generation 无收益；若 D2 改成池化复用，它从"有用"变成"必需"。UE 那一族等于把 D2 的答案前置了：不回收就没有 ABA。

**可证伪**：如果 D2 定为"句柄只在成对创建销毁的短生命周期内有效、只能用 RAII 包装持有"，则 generation 无收益，退回纯槽位下标。

**核实来源**（复查用）：

- Godot：`core/templates/rid.h`、`core/templates/rid_owner.h`（master）
- wgpu：`wgpu-core/src/id.rs`、`wgpu-core/src/storage.rs`、`wgpu-core/src/registry.rs`（v24.0.0）
- Unity：`Runtime/Export/Scripting/EntityIdStore.bindings.cs`（6000.7）、`Runtime/Export/Scripting/UnityEngineObject.bindings.cs`（2022.3 / 6000.0 / 6000.7 / master）、`Modules/Entities/Public/EntityId.bindings.cs`（master）、`Runtime/Export/Graphics/{GraphicsBuffer,Texture,Mesh}.bindings.cs`（master）
- Filament：`filament/backend/include/backend/Handle.h`、`filament/backend/src/HandleAllocator.cpp`、`filament/backend/include/private/backend/HandleAllocator.h`（main）
- Unreal：Epic 公开 API 文档 `Runtime/RHI/FRHIResource`、`FRHIBuffer`、`FRHITextureReference`、`Runtime/RenderCore/FRenderCommandFence`、`FDeferredCleanupInterface`、`BeginCleanup`（由 UE 源码生成；RHI 源码本身需授权）
- bgfx / bx：`include/bgfx/bgfx.h`、`include/bx/handlealloc.h`、`include/bx/inline/handlealloc.inl`（master）
- Vulkan：`Vulkan-Docs/chapters/fundamentals.adoc`，Object Model 与 Valid Usage for Object Handles
- D3D12：Microsoft Learn `ID3D12Resource`、`D3D12_CPU_DESCRIPTOR_HANDLE`

### D2 · 资源所有权在设备还是独立资源池

- **事实**：现状是设备的变体——创建/销毁都经设备，但**销毁时机由调用方决定**（`Sandbox::releaseResources()`）；设备内部有槽表；帧在飞的常量分槽由 `Sandbox` 自己按 `framesInFlight()` 记账
- **判据**：谁能回答"这个资源是否还在飞"；是否会出现多设备共存
- **倾向**：保留"设备是唯一能碰 GPU 的对象"，但把"谁在什么时候销毁"升级成**必须显式回答**的问题。理由：常量分槽这套设计就是被这件事逼出来的（`ARCHITECTURE.md` §8.3），说明生命周期不能靠默契
- **可证伪**：出现同一进程内多设备/多窗口 → 需要独立资源池

### D3 · 跨层转换点唯一化

- **问题**：CPU 的 `MeshData` / `MeshId` 与 GPU 的 `BufferHandle` 在哪一层发生唯一转换
- **事实**：现状转换点是 `Sandbox::uploadMesh(slot)`（`Sandbox.cpp:283`），它同时维护 `meshes[slot]` 表；`Model` 只记 `MeshId`
- **判据**：第二个消费者是否需要同一张表
- **倾向**：把"MeshId → GpuMesh"的表从 `Sandbox` 提到 resources 层，`Sandbox` 只调 resources。否则每个新应用（比如离线缩略图工具）都要重写一遍这张表
- **可证伪**：如果第二个消费者永远不会出现，留在 app 层更简单

### D4 · Vertex 格式与 MeshData 形状【已决断：以 glTF 2.0 规范为准】

**裁决依据**：glTF 2.0 规范（Khronos；`mesh.primitive` schema 与属性访问器表，要点见第 9 节）。选它作为唯一外部契约的理由：公开、可审计、纯标准库可解析，而且它本来就是 Phase 1 的输入格式——让上层数据结构与输入格式对齐，而不是另立一套私有约定。

**裁决结论**：**现状代码已经合规；讨论提出的 28 字节 / 每面独立顶点方案是偏离，撤回。**

**结果 1 · 索引式是规范的主路径，保持现状**

- `MeshData{vertices, indices}` 与 `Sandbox` 的 `bindIndexBuffer` + `drawIndexed` 通路都不动
- 规范要求同一 primitive 的所有属性访问器 `count` **MUST** 相等，即所有属性共享一个索引空间 ⇒ 去重单位是完整顶点，`uv` / `norm` 的计数永远等于 V
- 撤回"每面独立顶点作为默认布局"。理由不是"索引更省内存"（那取决于第 9 节的临界点），而是：所有导出器都产出索引几何、GPU 顶点后变换缓存复用依赖索引、非索引会丢掉规范里的共享语义

**结果 2 · 非索引仍然合法，但理由换成"规范允许"**

规范原文：`indices` 缺省时 the primitive uses non-indexed geometry。所以 `MeshData` 现在的形状（`indices` 为空 = 非索引）**正好就是规范语义**，不需要改。它保留的动机从"属性逐帧重整"改成"加载器要能吃规范允许的资产"。要把它画出来仍然需要 Phase 3 补的非索引 `draw`。

**结果 3 · Vertex 保持 44 字节，逐字段对照规范**

| 现状字段 | glTF 语义 | 规范类型 | 合规 |
|---|---|---|---|
| `position` vec3 | `POSITION` | VEC3 / float32 | 是 |
| `color` vec3 | `COLOR_0` | VEC3 或 VEC4 / float32、unorm8、unorm16 | 是（VEC3 合法） |
| `normal` vec3 | `NORMAL` | VEC3 / float32 | 是 |
| `uv` vec2 | `TEXCOORD_0` | VEC2 / float32、unorm8、unorm16 | 是 |

现布局的每个字段都有规范对应物，没有私有字段。被撤回的两条：

- **2 分量法线**：规范里 `NORMAL` 就是 VEC3 / float32，不存在 2 分量法线属性。要压缩只能上 octahedral 之类的编码，那属于存储优化，按第 9 节的优先级必须先有测量；而且编码后着色器要配解码器，是额外的正确性面
- **去掉 `color`**：`COLOR_0` 是规范属性，且当前着色器就读它（`basic.vert` 属性 1）

**结果 4 · 属性 location 不需要改，因为规范不定义 location**

glTF 不规定 location 编号。业界做法是"语义命名的属性 + 一张显式声明表"，项目现状正是这个：`VertexAttribute{location, format, offset}` + `basic.vert` 的 `layout(location = N)` + `basic.hlsl` 的语义名（D3D11 侧把 location 映射到 `POSITION` / `COLOR`）。**这一项无需改动**，也不要为了"统一编号"去动它——那是偏好，不是规范。

**结果 5 · 由规范推出的三个具体待办**

| # | 待办 | 现状 | 落到哪 |
|---|---|---|---|
| 1 | 索引宽度：顶点数 ≤ 65535 时用 uint16 | 所有网格硬编码 `IndexFormat::UInt32`（`Sandbox.cpp:305`），RHI 已声明 `UInt16` 却从未用过 | Phase 1（生成器与上传点） |
| 2 | 拓扑：`mode != 4`（非 TRIANGLES）**明确报错拒绝** | RHI 只有 `PrimitiveTopology::TriangleList`；规范有 7 种 `mode`，默认 4 | Phase 1 解析器 |
| 3 | 保留 `POSITION` 的 `min` / `max` | 规范 **MUST** 定义；`MeshData.h` 现写着"包围盒刻意留给几何工具" | Phase 1；是否成为 `MeshData` 字段随 D7 一并定 |

第 2 条按项目既有的 misconfiguration fails loud 取向走：静默画错比拒绝加载更糟。扩展 `PrimitiveTopology` 留到有真实需求。

**不加 `IndexFormat::UInt8`**：规范允许 uint8 索引，但 Vulkan 的 uint8 索引需要 `VK_EXT_index_type_uint8` 扩展，加进 RHI 会违反"接口按最重的后端定形状"。加载器遇到 uint8 时**加宽到 uint16**。

**可证伪**：第 1 条（uint16）只影响带宽不影响正确性，若实测网格都远小于 65535 顶点，可以推迟；第 2、3 条是正确性要求，不能推迟。

**顺带记录一个现状事实**：当前 `Vertex` 44 字节里有 20 字节（`normal` 12 + `uv` 8）**没有消费者**，`uv` 还是全 0 占位。这不是布局问题而是着色器与管线的问题——按规范看是"属性齐了但没接上"的状态，"Vertex 可能要扩展"的说法是反的。

### D5 · 分配器：按需分配还是池化复用

- **判据**：一次运行的资源创建/销毁次数、峰值同时存活数量、有没有帧内创建
- **事实**：现状是"每次 `createBuffer` 直接向后端要"，没有池、没有复用、没有上限。流式加载每个道具建 2 个缓冲，3 个道具共 6 个，之后不再创建
- **倾向**：**先按需 + 加测量**——把每次分配的 `size` / `usage` / 存活时长写进日志，拿到真实数字再决定池化。池化解决的是高频抖动，而现在没有任何抖动证据；加测量是几十行，池化是重写
- **可证伪**：一旦出现"每帧创建/销毁资源"的路径（AI 资产预览很可能就是），立刻回头做池化

### D6 · 内存驻留：staging → device-local 何时做

- **事实**：Vulkan 后端目前所有 buffer 都是 `HOST_VISIBLE | HOST_COHERENT` 且常驻映射（`VulkanRenderDevice.cpp:1036`），`DEVICE_LOCAL` 只用在深度图一类
- **讨论判断**：host-visible 不是万能的，device-local 才是 GPU 读取最快的地方
- **前置问题**：`BufferDesc` 只有 `{size, usage, initial_data}`，**不含"创建后是否还会被 CPU 改"**，分配器无法据此选内存类型
- **倾向**：先扩一个"更新模式"字段（`Static` / `Dynamic`），再决定映射策略。D3D11 的 DEFAULT / DYNAMIC / STAGING 与 Vulkan 的 memory type 都能由它推出来
- **顺序警告**：这条会改 `BufferDesc`，属于 RHI 接口变化，**必须在 Phase 1 之前定**，否则加载器会把"静态资源也用 host-visible"固化下来
- **连带影响**：常量缓冲是每帧改的 → `Dynamic`；顶点/索引来自磁盘 → `Static`。这条一落地，三后端的常量分槽逻辑要跟着复核一遍

### D7 · MeshAsset 放在哪一层

- **选项**：A `core`；B 新层 `stv3d_asset`（只链 core）；C `src/loader/` 里
- **判据**：`core` 是"编译期强制零依赖"的守卫层（`core_smoke.cpp`）。`MeshAsset` 带节点、材质、纹理引用，已经不是纯数学与几何值类型
- **事实**：`src/loader/` 现有 `GLTFLoader.h` / `FBXLoader.h` / `GLBLoader.h` 三个**空文件**（其中 `GLBLoader.h` 未跟踪），且**没有任何 CMake 目标引用它们**
- **倾向**：B。`core` 的零依赖守卫是项目最值钱的约束之一，不该为了资产类型松动；`asset` 层只依赖 core，仍然可脱离 GPU 单测
- **可证伪**：如果 `MeshAsset` 最终只剩 `vertices` / `indices`，那它就是 `MeshData` 的超集，应直接放 `core`

### D8 · 节点层级：先展平还是引入 Transform 层级

- **事实**：`Model` 只有自己的 position/rotation/scale，没有父子关系；场景是 `Sandbox` 里一个 `vector<Model>`
- **选项**：A 加载期把节点世界变换烘进 `Model`；B 引入 Transform 父子层级
- **判据**：加载的资产会不会在运行时改层级，或需要骨骼动画
- **倾向**：A。展平是可逆的（随时能加层级），而层级的代价会渗进 `Model`、`Sandbox` 以及之后所有遍历；在当前阶段引入层级等于提前引入场景树
- **可证伪**：出现骨骼动画或运行时改父子关系 → 选 B

### D9 · 原生穿透契约

- **目标**：熟悉 GL/VK/D3D 的用户能拿到后端原生句柄，插入自己的原生代码；一旦调用，状态同步与资源风险由开发者自负（unsafe 契约）
- **现状先例**：`GLRenderDevice` 已经开了 `functions()` 与 `reportErrors()` 两个后门，但它们是**具体类型上的方法**，不在 `IRenderDevice` 上
- **待设计**：句柄 → 原生对象的查询接口放哪（`IRenderDevice` 上的虚函数？每后端一个查询？）；"unsafe 之后框架状态视为失效"如何写进接口而不是只写在文档里
- **倾向**：在 `IRenderDevice` 上加一个**窄**接口，把"调用后框架状态视为失效"写成契约；不要为了通用抽象成一个万能 `void*`
- **风险**：这是薄抽象最容易泄漏成核心污染的地方（见第 7 节）。先设计边界，再写代码

### D10 · 句柄是否类型安全【待你决断】

- **起因**：核对 D1 时发现，现状 `BufferHandle` / `ShaderHandle` / `PipelineHandle` **都是 `using X = std::uint32_t`**，彼此可以自由互换。把 pipeline 句柄当 buffer 传，编译器不拦，而且不崩——只是画错或读到别的槽位
- **业界做法有两条路，机制不同**：
  - **Filament（编译期）**：`template<typename T> struct Handle : public HandleBase`，每个资源类型一个实例（`BufferObjectHandle`、`TextureHandle`、`VertexBufferHandle`…），并保留一个显式的基类转换用于上游类型；句柄本身仍是单个 32 位 id，所以类型安全是**零运行时成本**的
  - **Unity（运行期）**：`EntityId` 的位域里带 `TypeId:12`，源码注释原话是 one masked compare validates version and kind together——版本和类型用一次掩码比较同时校验。代价是句柄位宽被类型占掉 12 位
- **两条路的取舍**：编译期模板零成本，但只在 C++ 类型系统内有效；运行期 TypeId 能拦下跨语言、序列化、外部传入的句柄，但要吃位宽。本项目目前只有 C++ 一个面，所以**倾向 Filament 那条**
- **判据**：这个误用会不会真的发生？（D9 的原生穿透、以及将来交给外部用户时，概率上升）
- **倾向**：采纳模板化句柄。零运行时成本，且它把"句柄属于哪个资源类型"写进类型系统，与 D1 的 generation 互补——一个防陈旧，一个防串类型
- **代价**：RHI 接口、三个后端、`Sandbox` 的句柄成员都要跟着改类型；机械改动但面比较广，所以单独立项
- **可证伪**：如果最终定成"句柄只在框架内部流转、从不交给外部代码"，误用面很小，可以推迟
---

## 7. 风险台账

| 风险 | 具体表现 | 当前防线 / 该做什么 |
|---|---|---|
| **薄抽象泄漏** | 后端独有特性反向污染核心接口，最后 RHI 里长满"只有 Vulkan 用得上"的字段 | D9 是最前线；新字段一律先问"另外两个后端怎么映射" |
| **需求绑架** | 一次性定制需求改了核心规范，内核被单个客户绑死 | 判据：这个需求会不会被第二个用户要求？不会 → 不进核心 |
| **AI 黑盒** | 核心模块里有你说不清的代码 | 第 4 节的接管验收标准；核心模块必须能被你重写一遍 |
| **对外化债** | 无命名空间、无 `install()`、无版本策略。一旦真有人拿去用，改这些就是破坏性变更，越晚越贵 | Phase 4；可提前的最小切片是低成本的泄压阀 |
| **一致性只是近似** | GL 4×MSAA vs 单采样，却用"三后端一致"当回归结论 | Phase 3 前把结论限定为"几何位置与动效一致" |
| **线程交接未设计** | `LoadQueue` 的 `produce()` 未来搬工作线程时，"payload 交给主线程"与背压还没有设计 | 属于 Phase 0 之后；两段式本身已为此留好了缝（`ARCHITECTURE.md` §9.6） |
| **单线程设备假设** | 设备是单线程的，所有设备调用必须留在持有它的线程上 | 现有约束，改动加载路径时不能破坏 |
| **只看现象不做对照** | 没对照的观察容易变成印象 | 第 8 节 |

---

## 8. 带问题去用 UE / Unity

目的是**验证或修正自己的判断**，不是学怎么用它们。有对照的观察才是判断。

| 我已有的判断 | 对照问题 |
|---|---|
| RHI 按最重的后端定形状（显式获取/提交、命令只在 begin/end 之间） | UE 的 RHI 是不是也这样？它怎么给没有命令缓冲的后端（GL 这类）落地这个模型？代价是什么？ |
| 裁剪空间差异做成参数（`ClipDepth` / `flipY`） | Unity 的 SRP 怎么吞掉 GL / Vulkan / Metal 的差异？是在矩阵层、shader 层还是变体层？ |
| 分层靠编译期强制（`core_smoke.cpp`、只带一个 include 目录） | UE 的模块依赖怎么强制？`build.cs` 还是约定？我的做法代价是什么（灵活性、编译时间）？ |
| 常量按「每 draw × 每帧在飞」分槽 | 主流引擎怎么组织每 draw 常量？描述符、ring buffer 还是别的？ |
| 句柄是不透明 `uint32` | 主流引擎的句柄带 generation 吗？谁负责失效检测？ |
| 内核精简、非核心拆可选组件 | 它们的模块粒度是怎么切出来的？切错的代价是什么？ |

**不要问**"它们怎么做所以我们要怎么做"——它们面向离线预烘焙的成品游戏开发，前提不同。

---

## 9. 附：会影响决策的知识点（只保留会改变决策的）

**顶点去重**
- 去重的单位是**完整顶点**：`pos`、`uv`、`norm` 全部相等才能合并
- 位置相同但 UV 不同（纹理接缝）→ 不能合并；位置相同但法线不同（硬边）→ 不能合并
- 现代 GPU 只有单索引，所有属性共享同一个顶点索引，所以 `uv` 与 `norm` 的数目**永远等于 V**

**索引的收益临界点**（依赖顶点大小，这是讨论里那个 `2.57F` 的适用条件）

```
索引式内存   = V × S + F × 12      （每面 3 个 uint32 索引）
非索引式内存 = 3F × S
临界点       = 3F×S = V×S + F×12  →  V = F × (3 − 12/S)

S = 28 字节（讨论提出的布局）：V = 2.57F
S = 44 字节（当前布局）：        V = 2.73F
```

当前立方体 `V = 8, F = 12`，远低于临界点，所以**当前用索引式是对的**。

**变换计算量**：非索引每个面变换 3 个顶点 → `3F`；索引每个顶点只变换一次 → `V`。当 `V ≈ F/2` 时降到约 `1/6`。

**索引真正的软肋**（这是"每面独立顶点"方案的论据；方案已按规范撤回，但它描述的边界条件仍然成立）
- 属性高频变化 → 去重关系崩塌 → 重建成本吃掉收益
- 属性分裂（UV 接缝、硬边）→ `V` 涨到接近 `2F ~ 3F` → 收益趋零

**优化优先级**
1. 算法与数据结构——决定数量级，收益最大
2. 内存访问模式——决定常数因子，现代 CPU 的主战场
3. 指令级微优化——交给编译器

**三条会改变决策的纠错**
- `vector<mat3>` 是 **AoS 不是 SoA**；三角形是三顶点一起用，AoS 才对
- 索引数组（`3F × 4` 字节）与未去重顶点数组（`3F × S` 字节）不是一回事，不能混着比较
- "法线 / UV 不能去重"的准确说法是"**不同时**不能合并"

**工程原则**（保留原话，因为它们直接约束第 5 节的顺序）
- 通用性是复利，优化是利息。先保通用，再谈优化
- 代码少一行，bug 就少一个来源
- 能判断出"什么时候不优化"，比会优化更值钱

**glTF 2.0 规范要点（D4 的裁决依据）**

来源：Khronos glTF 2.0 规范的属性访问器表（`specification/2.0/Specification.adoc`）与 `schema/mesh.primitive.schema.json`。

属性语义与访问器类型：

| 语义 | 访问器类型 | 分量类型 | 说明 |
|---|---|---|---|
| `POSITION` | VEC3 | float32 | **MUST** 定义 `min` / `max` |
| `NORMAL` | VEC3 | float32 | 单位向量 |
| `TANGENT` | VEC4 | float32 | XYZ 单位化，W 为 ±1 表示切线基手性 |
| `TEXCOORD_n` | VEC2 | float32 / unorm8 / unorm16 | ST 纹理坐标 |
| `COLOR_n` | VEC3 或 VEC4 | float32 / unorm8 / unorm16 | 各分量 **MUST** 在 [0, 1] |
| `JOINTS_n` | VEC4 | uint8 / uint16 | 蒙皮 |
| `WEIGHTS_n` | VEC4 | float32 | 蒙皮 |

`mesh.primitive` 的关键规则：

- `attributes` 必填（至少一个属性）；`indices` **可选**
- `indices` 访问器 **MUST** 为 `SCALAR` 类型，分量类型为 uint8 / uint16 / uint32 之一
- `indices` 缺省时，primitive 使用非索引几何，顶点按属性访问器的顺序取用
- `mode` 默认 4：0 POINTS、1 LINES、2 LINE_LOOP、3 LINE_STRIP、**4 TRIANGLES**、5 TRIANGLE_STRIP、6 TRIANGLE_FAN
- 同一 primitive 的所有属性访问器 **MUST** 有相同的 element 数量（单索引空间）

---

## 10. 参考架构方向（只在需要时查，不作为模仿对象）

| 参考 | 可借鉴的点 |
|---|---|
| O3DE Atom | RHI / RPI 分层；数据驱动的 `PipelineDesc` |
| Unity Sentis | 推理后端抽象为 `BackendType`，应用层不感知底层执行细节 |
| Houdini PDG | Service 池 + Work Item 粒度的调度，适合离线批量生成 |
| UE Plugin / UPM | 作品注册与加载机制（迁移暂不管，架构上可预留） |

---

## 附：文档维护约定

- 定了 D1–D10 中的任何一条 → 更新第 6 节，并把结论同步到 `ARCHITECTURE.md`（D4 已按 glTF 2.0 规范决断；D1 已按 Godot / wgpu / Unity 的句柄布局决断，并显式排除 UE 的引用计数路线）
- 完成一个 Phase → 更新第 2 节基线与第 5 节状态
- 发现代码与本文档冲突 → **以代码为准**，回来改这里
- 本文档不重复 `README.md` 与 `ARCHITECTURE.md` 已有的结构图与常量表
