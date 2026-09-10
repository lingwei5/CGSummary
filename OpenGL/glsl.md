# GLSL 数据系统详解

> 基于 OpenGL 4.6 / GLSL 4.60 规范整理
>
> 规范文档：https://registry.khronos.org/OpenGL/specs/gl/GLSL/glslangspec.4.60.pdf

本文系统讲解 GLSL 中与"数据"相关的一切：数据类型、类型限定符、数据在 application 与各 shader 阶段间的传递方式、buffer/texture/image 资源、interface block、内置变量。

## 目录

- [1. 数据类型与变量作用域](#1-数据类型与变量作用域)
- [2. 类型限定符](#2-类型限定符)
- [3. 数据的传递方式与限制](#3-数据的传递方式与限制)
- [4. Buffer、Texture 与 Image](#4-buffer-texture-与-image)
- [5. Interface Block](#5-interface-block)
- [6. 内置变量](#6-内置变量)
- [7. Shader 编译与 Program 生命周期](#7-shader-编译与-program-生命周期)
- [附录A：常见错误与排查](#附录a常见错误与排查)
- [附录B：速查表](#附录b-速查表)

## 全局视图：数据的完整生命周期

```
[Application / CPU 端]
      │
      │ ① 写入：glBufferSubData / glMapBuffer / glTexSubImage2D / glClearTexImage...
      ▼
┌────────────────────────────────────────────────┐
│ GPU 内存中的各种资源                            │
│   VBO      ─→ vertex attribute (in)            │
│   UBO      ─→ uniform block（只读）             │
│   SSBO     ─→ buffer block（可读写）            │
│   Texture  ─→ sampler（读，有硬件过滤）/         │
│               image（读写，无过滤）              │
│   Atomic Counter ─→ 原子计数器                  │
└────────────────────────────────────────────────┘
      │
      ▼
┌────────────────────────────────────────────────┐
│ VS → TCS → TES → GS → FS / CS                │
│   阶段之间用 in/out 接口变量传递                │
│   （光栅化时按 smooth/flat/noperspective 插值）  │
└────────────────────────────────────────────────┘
      │
      │ ② 输出：gl_Position→光栅化 / gl_FragDepth→深度
      │         transform feedback / imageStore / SSBO 写
      ▼
[帧缓冲 / 回读 / 下一帧输入]
```

---

# 1. 数据类型与变量作用域

## 1.1 基本类型总览

| 类别 | 类型 | 说明 |
|------|------|------|
| 标量 | `void`, `bool`, `int`, `uint`, `float`, `double` | 基础类型。int 默认 32 位有符号 |
| 浮点向量 | `vec2`, `vec3`, `vec4` | 最常用 |
| 整数向量 | `ivec2`, `ivec3`, `ivec4` | 有符号整数向量 |
| 无符号向量 | `uvec2`, `uvec3`, `uvec4` | 无符号整数向量 |
| 布尔向量 | `bvec2`, `bvec3`, `bvec4` | 布尔向量，常做条件选择 |
| 双精度向量 | `dvec2`, `dvec3`, `dvec4` | 需要 `GL_ARB_gpu_shader_fp64` |
| 浮点矩阵 | `mat2` ~ `mat4`，`mat2x3`, `mat3x2` 等 | **列主序**（column-major） |
| 整数/布尔矩阵 | 无 | GLSL 不支持 |
| 采样器 | `sampler2D`, `samplerCube`, ... | 不透明类型（opaque type） |
| 图像 | `image2D`, `uimageBuffer`, ... | 不透明类型，可读写 |
| 原子计数器 | `atomic_uint` | 不透明类型 |
| 数组 | `float a[4];` | 大小必须是常量表达式（SSBO 末尾除外） |
| 结构体 | `struct S { ... };` | 可嵌套，可含数组和结构体 |
| 不透明类型数组 | `sampler2D tex[8];` | 需要 binding 或用索引常量访问 |

**字面量后缀：**

```glsl
int   a = 1;      // 十进制 int
uint  b = 1u;     // 无符号后缀 u
float c = 1.0;    // 浮点（1 会退化为 int）
float d = 0x1p4;  // 十六进制浮点 = 16.0
1.0e5              // 科学计数法
true / false       // bool
```

## 1.2 向量与 Swizzle

**分量名（可以混用同一组内）：**

| 用途 | 分量名 |
|------|--------|
| 颜色 | `.r .g .b .a` |
| 位置 | `.x .y .z .w` |
| 纹理 | `.s .t .p .q` |

```glsl
vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
vec2 a = v.xy;    // (1.0, 2.0)
vec3 b = v.rgb;   // (1.0, 2.0, 3.0)
v.xz = vec2(5.0, 6.0);   // 写入 x 和 z
vec4 c = v.wzyx;  // 分量重排（swizzle）
float s = v.x;    // 取单分量
v.xyz += v.www;   // 复制分量后运算
```

**规则：**
- swizzle 只能用**同一组**的分量名（`.xry` 非法）
- 写入 swizzle 不能重复分量（`.xx = ...` 非法）
- l-value swizzle（如 `v.xy = ...`）在 uniform/attribute 上不可用

**构造函数技巧：**

```glsl
vec4 a = vec4(1.0);           // 全部填充 1.0
vec4 b = vec4(vec2(1.0), vec2(2.0));  // 拼接
vec4 c = vec4(a.xyz, 5.0);    // 混合
vec2 d = vec2(a);             // 丢弃多余分量（截断）
```

## 1.3 矩阵：列主序

**GLSL 矩阵按列存储**，`mat4` 在内存中和 `float m[16]` 布局一致（4 列，每列一个 vec4）。

```glsl
mat4 m;            // 4x4
mat3x2 m32;        // 3 列 2 行（注意：先列数后行数）
vec4 col = m[0];   // 取第 0 列，得到 vec4
float x = m[1][2]; // 第 1 列第 2 个元素（列、行）
float y = m[1].z;  // 等价写法
m[2] = vec4(1.0);  // 整列赋值
```

**构造：**

```glsl
// 逐列构造
mat3 M = mat3(vec3(1,0,0),   // 第0列
              vec3(0,1,0),   // 第1列
              vec3(0,0,1));  // 第2列
// 逐元素构造（按列优先顺序）
mat3 N = mat3(1,0,0,  0,1,0,  0,0,1);
// 对角线构造
mat2 D = mat2(2.0);          // 对角线全 2
```

**乘法语义：**

```glsl
vec4 v2 = M * v;      // 标准数学：矩阵左乘列向量
mat4 C = A * B;        // 矩阵乘矩阵
vec4 r = v * M;        // 行向量右乘，等价 transpose(M) * v
```

**与 C/C++ 传参的对应关系：**

OpenGL 的 `glUniformMatrix4fv` 默认 `transpose = GL_FALSE`，即按列主序直接上传，与 GLSL 内存布局一致。C++ 端若用行主序存储（如 glm 默认），需传 `GL_TRUE` 或使用 `glm::value_ptr` 前转置。

## 1.4 数组与结构体

```glsl
// 数组：大小必须是编译期常量
uniform vec3 lightPos[8];
const int N = 4;
uniform float weights[N];   // ✓ 常量表达式

// SSBO 中允许末尾 unsized 数组（变长）
buffer Particles {
    Particle data[];        // ✓ 最后一个成员可以不定长
};

// 结构体
struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};
uniform Light lights[8];

Light l = Light(vec3(0), vec3(1), 2.0);  // 按成员顺序构造
float i = lights[0].intensity;
```

**限制：**
- 结构体成员不能是 `in`/`out` 限定（限定符加在结构体整体上）
- 结构体不能递归
- 不透明类型（sampler 等）可以放在 uniform 结构体中，但**不能**放在 in/out 接口结构体或 SSBO 中

## 1.5 变量作用域

GLSL 采用 C 风格的块级作用域：

| 作用域 | 声明位置 | 生命周期 |
|--------|---------|---------|
| 全局 | 任何函数外 | 整个 shader 编译单元 |
| 局部 | 函数/块 `{}` 内 | 声明点 → 块结束 |
| 函数参数 | 参数列表 | 一次调用 |
| 接口变量 | 全局 + in/out 等 | 跨阶段（区别于普通全局） |

```glsl
uniform float g;     // 全局
int x = 1;           // 全局（非接口）

void foo() {
    int x = 2;       // 局部 x 遮蔽(shadow)全局 x —— GLSL 4.20起允许
    {
        float x = 0.0;  // 错误！局部内不允许再被遮蔽？不——
        // 实际上 GLSL 允许嵌套块内遮蔽，同层不允许重名
    }
}
```

**遮蔽（shadowing）规则：**
- 内层作用域可以遮蔽外层同名变量（GLSL 4.20 前，全局变量不可被局部遮蔽，4.20 起允许）
- 同一作用域内不允许重名声明
- `for` 循环变量作用域仅限循环内（4.20 起像 C++ 一样）

**变量初始化时机：**
- 全局变量可用**常量表达式**初始化（不能用 uniform 初始化另一个全局）
- 局部变量可用任意表达式初始化
- 未初始化的局部变量值未定义（undefined），务必显式初始化——这是许多随机错误的根源

## 1.6 const 与 readonly 数据

```glsl
const float PI = 3.14159265;        // 编译期常量
const vec4 ORIGIN = vec4(0.0);      // 必须是常量表达式
const int N = 8;
uniform vec3 a[N];                  // 数组大小用 const
```

`const` 变量是编译期常量，可用于数组大小、loop 展开等需要编译期已知值的场合。

---

# 2. 类型限定符

GLSL 的限定符分五大家族，可叠加使用（按顺序）：

```
[layout(...)] [interpolation] [memory] [precision] storage
```

```glsl
layout(std140, binding = 2) uniform Matrices { ... };
layout(location = 0) out vec4 fragColor;
flat in int materialID;
coherent volatile layout(binding=0, rgba8) readonly image2D img;
```

## 2.1 存储限定符

### const — 编译期常量

```glsl
const float PI = 3.14159;
```
- 必须在声明时用常量表达式初始化
- 编译期内联，不占用 uniform 空间

### in / out — 输入与输出

**跨阶段传递的核心机制**，详见第 3 节。

```glsl
// 顶点着色器
layout(location = 0) in vec3 aPos;     // 从顶点属性读取
out vec3 vColor;                        // 传给下一阶段

// 片元着色器
in vec3 vColor;                         // 接收（名字类型须与VS的out完全一致）
layout(location = 0) out vec4 fragColor;// 写入帧缓冲
```

**不同阶段中 in/out 的含义：**

| 阶段 | `in` | `out` |
|------|------|-------|
| VS | 顶点属性（来自 VAO） | 传给 TCS/GS/FS 的逐顶点数据 |
| TCS | VS 输出（可读取任意顶点） | 控制 patch 的顶点；**每个 invocation 写各自的一份**，最后全体 invocation 的输出合并成一个 patch |
| TES | patch 全部控制点 + per-patch 数据 | 传给 GS/FS |
| GS | 前一阶段输出（**自动成为数组**，长度=输入图元顶点数） | 每次 EmitVertex 输出一组 |
| FS | 光栅化插值后的数据 | 片元颜色（到 framebuffer） |
| CS | **不可用** | **不可用**（数据交换靠 SSBO/image/shared） |

### uniform — 全局只读

```glsl
uniform mat4 uModel;
uniform vec3 lightColor;
uniform sampler2D uTexture;   // sampler 也是 uniform 的一种
```

- 着色器内只读；值由 application 用 `glUniform*` / `glProgramUniform*` 设置
- 在**所有 invocation 之间共享**（同一 draw call 内所有顶点/片元看到相同值）
- 分两种：
  - **默认块 uniform**（如上，直接用 `glGetUniformLocation` + `glUniform` 设置）
  - **uniform block**（配合 UBO，见第 5 节）
- 关键字可用 `layout(binding=N)` 指定 UBO 绑定点；单个 uniform 的 location 用 `layout(location=N)`

### buffer — SSBO 存储

```glsl
layout(std430, binding = 1) buffer ParticleData {
    vec4 positions[];      // 可读写，变长
};
```
- 可读可写，适合 shader 端产生的数据（粒子、GPU 排序、计算结果）
- 详见第 4/5 节

### shared — compute shader 组内共享

```glsl
shared float tile[32][32];   // 仅 compute shader 可用
```

- 同一 **local work group** 内所有 invocation 共享
- 生命周期 = 整个 work group 的执行期
- 必须配合 `barrier()` + `memoryBarrierShared()` 保证可见性
- 只能用于标量/向量/矩阵/数组/结构体的组合，不能初始化

### attribute / varying — 已废弃

| 旧（GLSL 1.x） | 新（GLSL 3.x+） |
|----------------|-----------------|
| `attribute vec3 aPos;` | `in vec3 aPos;`（VS 中） |
| `varying vec3 vColor;` | `out vec3 vColor;`（VS）/ `in vec3 vColor;`（FS） |
| `gl_FragColor` | `layout(location=0) out vec4 fragColor;` |

## 2.2 插值限定符（仅 FS 输入 & 阶段间接口）

修饰光栅化阶段对接口变量的插值方式：

| 限定符 | 含义 | 典型用途 |
|--------|------|---------|
| `smooth`（默认） | 透视校正插值：先插值 `(value/w, 1/w)`，再除回去 | 颜色、UV、法线 |
| `flat` | 不插值，取 provoking vertex（通常是最后一个顶点）的值 | 整数必须用；材质ID、面ID |
| `noperspective` | 线性插值（屏幕空间），不做透视校正 | 屏幕空间位置 |

```glsl
// VS
flat out int vMaterialID;
smooth out vec3 vWorldPos;     // smooth可省略
noperspective out vec2 vScreenUV;

// FS —— 插值限定符必须与 VS 一致！
flat in int vMaterialID;
smooth in vec3 vWorldPos;
```

**关键规则：**
- **整数类型的接口变量必须用 `flat`**（整数无法插值）
- 前后阶段必须声明一致的插值方式，否则链接错误
- `noperspective` 的区别示例：贴在地面上的纹理，用 smooth 时近处纹理密、远处疏（透视正确）；noperspective 则均匀分布

## 2.3 内存限定符（用于 image / SSBO buffer 成员）

控制内存访问的可见性与同步：

| 限定符 | 含义 |
|--------|------|
| `coherent` | 写入对**同一图像/缓冲的其他 invocation 立即可见**（仍需 `memoryBarrier*` 控制顺序） |
| `volatile` | 声明数据可能被外部（其他 shader、CPU）并发修改，禁止编译器缓存/重排 |
| `restrict` | 承诺此变量是访问该缓冲的唯一引用，允许编译器优化 |
| `readonly` | 只读声明，写入会编译报错 |
| `writeonly` | 只写声明，读取会编译报错 |

```glsl
coherent layout(binding=0, rgba32f) writeonly image2D outputImage;
layout(std430, binding=1) readonly buffer Input { float data[]; };
```

**典型组合：**
- 读写图像（如 GPU 粒子写纹理）：`coherent` + `imageStore` + `glMemoryBarrier`（或 `memoryBarrier` shader 内）
- 只读输入 SSBO：`readonly`
- `restrict` 常与 `readonly`/`writeonly` 一起用，帮助优化

**注意：** `coherent` 只保证"同一次 dispatch/draw 内"的可见性；跨 draw call 的同步由 CPU 端 `glMemoryBarrier()` 完成。

## 2.4 精度限定符（桌面 GLSL 意义有限，移动端 GLSL ES 核心）

| 限定符 | 含义 |
|--------|------|
| `highp` | 高精度（桌面 GLSL 中等价于标准精度） |
| `mediump` | 中精度 |
| `lowp` | 低精度 |

```glsl
precision mediump float;   // 设置默认精度（ES中必须为float声明）
lowp vec3 color;
highp mat4 mvp;
```

- **桌面 GLSL 4.x：** 精度限定符被接受但基本无实际作用（除了 ES 兼容）
- **GLSL ES（WebGL/移动）：** 非常重要。fragment shader 中没有默认 float 精度，**必须** `precision mediump float;` 或逐变量声明，否则编译失败

## 2.5 layout 限定符

`layout` 是 GLSL 4.x 参数化数据布局的核心，不同上下文中含义不同：

### 顶点属性 / 片元输出（in/out 单变量）

```glsl
layout(location = 0) in vec3 aPos;       // 对应 glVertexAttribPointer 的 index
layout(location = 0) out vec4 fragColor; // MRT 的第0个渲染目标
layout(location = 1) out vec4 gNormal;   // GBuffer 的第1个目标
```

- `location` 显式指定，避免依赖链接器自动分配
- VS 的 `in location` 直接对应 `glBindAttribLocation` / `glVertexAttribPointer` 的 index
- FS 的 `out location` 对应 `glDrawBuffers` 的绑定

### uniform block / buffer block

```glsl
layout(std140, binding = 2) uniform Matrices { mat4 mvp; };
layout(std430, binding = 1) buffer Data { float arr[]; };
```

- `binding`：绑定到第几个 UBO/SSBO 绑定点（配合 `glBindBufferBase`），省去 `glUniformBlockBinding`
- `std140` / `std430` / `shared` / `packed`：内存对齐规则（见第 5 节）

### image / sampler

```glsl
layout(rgba8, binding = 3) uniform image2D img;   // 图像单元格式必须与实际纹理匹配
layout(binding = 4) uniform sampler2D tex;         // 4.20+ 支持 sampler 显式 binding
```

- `rgba8` 等声明图像的**内部格式**，必须与 `glTexStorage2D/glTexImage2D` 时的 internalFormat 一致，否则未定义行为
- sampler 的 `binding` 直接对应纹理单元（`glBindTexture` 后还需 `glBindImageTexture` 用于 image）

### 几何/曲面细分输出

```glsl
layout(triangles, invocations = 1) in;         // GS 输入图元类型
layout(triangle_strip, max_vertices = 6) out;  // GS 输出类型与上限（必填）

layout(vertices = 3) out;                       // TCS：控制点数
layout(triangles, equal_spacing) in;            // TES：图元域与细分策略
```

### compute shader

```glsl
layout(local_size_x = 16, local_size_y = 16) in;  // work group 大小（必填）
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
```

- 也可用 `glDispatchCompute` 时的 implicit 布局（4.20+ `local_size_variable` 需扩展）

### 其他常用

```glsl
layout(origin_upper_left) in vec4 gl_FragCoord;   // FS：窗口坐标左上原点
layout(pixel_center_integer) in vec4 gl_FragCoord;// FS：像素中心是整数坐标
layout(depth_any) out float gl_FragDepth;         // FS：提前声明深度行为，允许保守剔除优化
layout(early_fragment_tests) in;                  // FS：深度测试在着色前执行
```

## 2.6 invariant 与 precise

保证跨着色器/跨 pass 的**数值一致性**：

```glsl
invariant gl_Position;          // 对内置输出
invariant out vec4 vPos;        // 对自定义输出

invariant precise vec3 vNormal;  // 与precise组合：禁止任何改变结果的优化
```

- 浮点运算在不同优化（如 FMA 合并）下可能有微小差异，导致同一几何在两个 shader 中深度不一致 → z-fighting / 多 pass 轮廓对不齐
- `invariant`：相同输入产生相同输出（常用于 shadow pass 与主 pass 共用投影）
- `precise`：更强，禁止任何改变计算结果的代数变换

## 2.7 各阶段可用限定符对照表

| 限定符 | VS | TCS | TES | GS | FS | CS |
|--------|:--:|:---:|:---:|:--:|:--:|:--:|
| `in` / `out` | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ |
| `uniform` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `buffer` (SSBO) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `shared` | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| `flat/smooth/noperspective` | ✓(out) | ✓ | ✓ | ✓ | ✓(in) | ✗ |
| `layout(location)` in | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| `layout(location)` out | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ |
| `layout(local_size)` | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| `layout(max_vertices)` | ✗ | ✗ | ✗ | ✓ | ✗ | ✗ |
| `layout(vertices)` out | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ |
| `layout(primitive type)` | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ |
| `early_fragment_tests` | ✗ | ✗ | ✗ | ✗ | ✓ | ✗ |

---

# 3. 数据的传递方式与限制

## 3.1 数据流动全景

```
Application                    Application
     │                              │
     │ 顶点属性                       │ uniform / texture / UBO / SSBO
     ▼                              ▼
   ┌──────────────────────────────────────┐
   │  VS ──out──> TCS ──out──> TES ──out──> GS ──out──> 光栅化 ──插值──> FS
   │                                        │                        │
   │          transform feedback            │              gl_FragColor
   └──────────────────────────────────────┘                    ▼
                                                        Framebuffer
```

## 3.2 Application → Shader：三种途径

### ① 顶点属性

**每顶点不同的数据**：位置、法线、UV、混合权重等。

```glsl
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
```

```cpp
// C++ 端
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glEnableVertexAttribArray(0);                        // location 0
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,      // 3分量float
                      8 * sizeof(float),             // stride：每顶点8个float
                      (void*)(0 * sizeof(float)));   // 偏移
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
```

**限制：**
- 只有 **VS** 能读取顶点属性
- 最多 16 个属性槽（`GL_MAX_VERTEX_ATTRIBS`，通常16~32）
- 每个属性最多 4 分量（vec4 是上限，mat4 会占用 4 个 location）
- 支持 divisor 实例化：`glVertexAttribDivisor(loc, 1)` → 每 instance 更新一次（instance ID）
- 属性可来自 VBO（浮点/整数归一化），整数属性（`glVertexAttribIPointer`）在 GLSL 中必须声明为 `int`/`uint`，无归一化

### ② uniform（默认块）

**每次 draw call 更新的全局参数**：

```glsl
uniform mat4 uViewProj;
uniform vec3 uLightPos;
uniform float uTime;
```

```cpp
GLint loc = glGetUniformLocation(prog, "uViewProj");  // 按名字查询
glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(vp));
```

**限制：**
- 只读（shader 内）
- uniform 数量有限（`GL_MAX_UNIFORM_COMPONENTS`，fragment 通常 1024+ 个 vec4）
- 修改需要先 bind 对应 program（`glUniform` 影响当前 program）；`glProgramUniform*` 可免绑定
- **性能注意：** location 查询应缓存，不要每帧 glGetUniformLocation

### ③ uniform block + UBO

**多个 program 共享、批量更新的大块 uniform**，见第 5 节。典型：全局矩阵（VP）、灯光数组、材质参数。

## 3.3 Shader 阶段 → 阶段：in/out 接口

### 基本规则

```glsl
// ---------- VS ----------
out vec3 vNormal;      // 名字= vNormal
out vec2 vUV;

// ---------- FS ----------
in vec3 vNormal;       // 名字、类型、插值限定符必须完全匹配
in vec2 vUV;
```

**匹配规则（链接时检查，不一致 → link error）：**
1. 变量名相同
2. 类型相同（包括向量分量数）
3. 插值限定符相同（`flat`/`smooth`/`noperspective`）
4. 阶段相邻（VS→FS 直连则只要求 VS 的 out 和 FS 的 in 匹配；中间插了 GS，则 GS 需同时匹配两端）

**数量限制：**

| 接口 | 典型上限 |
|------|---------|
| VS out → 下游 | `GL_MAX_VARYING_COMPONENTS` / `MAX_VARYING_VECTORS`（通常 60 vec4 / 15 vec4 slot） |
| GS 输出 | `GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS`（包含 max_vertices × 每顶点分量） |
| FS out（MRT） | `GL_MAX_DRAW_BUFFERS`（通常 8） |

**location 也可以显式指定跨阶段接口变量（4.10+）：**

```glsl
layout(location = 0) out vec3 vNormal;   // VS
layout(location = 0) in vec3 vNormal;    // FS —— 按location匹配而非名字
```
- 用 location 匹配时，名字可以不同

### TCS / TES 特殊规则

**TCS（控制着色器）的输出是"每个控制点"的：**

```glsl
layout(vertices = 3) out;        // 3个输出控制点

in vec3 vNormal[];               // 输入自动是数组，长度 = gl_PatchVerticesIn
out vec3 tcNormal[];             // 输出也是数组

void main() {
    int i = gl_InvocationID;      // 当前invocation负责第i个控制点
    tcNormal[i] = vNormal[i];     // 只写自己负责的那个！
    if (gl_InvocationID == 0) {   // per-patch输出只写一次
        gl_TessLevelInner[0] = 4;
        gl_TessLevelOuter[0] = 4;
    }
}
```

**per-patch 输出（4.30+）：**

```glsl
patch out float patchTessFactor;   // 每patch一份（不是每控制点）
```

**TES：** 输入自动成为数组（控制点），输出是逐评估顶点：

```glsl
layout(triangles, fractional_odd_spacing, ccw) in;
in vec3 tcNormal[];               // 所有控制点可读
out vec3 vNormal;                 // 每个细分出的顶点
```

### GS 特殊规则

GS 的输入自动是**数组**，长度 = 输入图元的顶点数：

| 输入图元 | 数组长度 |
|---------|---------|
| `points` | 1 |
| `lines` / `line_strip` | 2 |
| `lines_adjacency` / `line_strip_adjacency` | 4 |
| `triangles` / `triangle_strip` | 3 |
| `triangles_adjacency` / `triangle_strip_adjacency` | 6 |

```glsl
layout(triangles) in;             // 声明输入类型
layout(triangle_strip, max_vertices = 3) out;

in vec3 vNormal[];                // vNormal[0..2]
in gl_PerVertex { vec4 gl_Position; } gl_in[];   // 内置的也是数组

void main() {
    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;   // 复制/变换
        EmitVertex();                          // 结束一个顶点
    }
    EndPrimitive();                            // 结束一个图元
}
```

### 光栅化对接口变量的处理

VS/TES/GS 输出的接口变量，经过**透视除法 + 视口变换 + 图元覆盖测试**后，在三角形内部做**重心插值**再交给 FS：

```
smooth:      v_fs = (v0/w0·a + v1/w1·b + v2/w2·c) / (1/w0·a + 1/w1·b + 1/w2·c)
flat:        v_fs = v[provoking vertex]
noperspective: v_fs = v0·a + v1·b + v2·c       （直接重心权重）
```

其中 a,b,c 是片元的重心坐标。这就是为什么 UV 必须用 smooth（否则透视下纹理会"弯曲"）。

## 3.4 Shader 内部/组内：shared 与原子操作

```glsl
// compute shader 内
shared int histogram[256];

void main() {
    uint idx = computeBin();
    atomicAdd(histogram[idx], 1);   // 原子操作避免竞争
    barrier();                       // 等同组全部执行到此
    memoryBarrierShared();           // shared内存可见性
    // ... 读取其他线程写入的数据
}
```

**barrier 规则：**
- `barrier()` 只能用在 uniform control flow 中（所有组内线程都必须到达，不能放在发散分支里）

## 3.5 输出到 Application / 帧缓冲

| 输出方式 | 机制 |
|---------|------|
| FS out → framebuffer | `layout(location=k) out vec4`；多目标 MRT |
| gl_FragDepth | 显式写深度（将使 early-z 失效，除非声明 conservative depth） |
| Transform Feedback | 把 VS/GS 输出捕获到 buffer（`glTransformFeedbackVaryings`），常做 GPU 模拟粒子/LOD |
| SSBO / image 写 | compute 或 FS 阶段直接写，CPU 再 map 回读或作为下一 pass 输入 |
| glReadPixels / PBO | 离线回读（慢，避免每帧） |

## 3.6 各种传递方式的对比总表

| 方式 | 方向 | 读写性 | 大小上限 | 更新频率 | 典型用途 |
|------|------|--------|---------|---------|---------|
| 顶点属性 | App→VS | 只读 | 16 attrib | 每顶点 | 位置/法线/UV |
| uniform（默认块） | App→All | 只读 | ~1KB | 每draw | MVP矩阵、时间、开关 |
| UBO | App→All | 只读 | 64KB（min） | 每draw/每帧 | 灯光数组、材质、骨架 |
| SSBO | App↔All | **读写** | ≥128MB | 任意 | 粒子、GPGPU数据 |
| 纹理 sampler | App→All（可写但一般读） | 读为主（含过滤） | 巨大 | 任意 | 贴图、LUT、缓存 |
| image | App↔All | **读写** | 巨大 | 任意 | 随机像素读写、原子操作 |
| in/out 接口 | 阶段间 | 单向 | ~60 vec4 | 每图元/每片元 | 插值数据 |
| shared | CS组内 | 读写 | ~32KB | — | 组内暂存、归约 |
| atomic counter | App↔All | 原子递增 | 数KB | 任意 | 计数、遮挡查询 |
| transform feedback | VS/GS→App | 只写 | buffer大小 | 每draw | GPU动画、粒子模拟 |

## 3.7 常见传递陷阱

1. **FS 读不到值**：多半是 VS/FS 接口名或类型不匹配 → 链接错误或被优化掉（用 `glGetProgramInfoLog` 查）
2. **整数接口变量没有 flat** → 链接错误
3. **TCS 输出数组索引写错**：每个 invocation 必须只写 `gl_InvocationID` 对应的槽
4. **在非 uniform 分支里调 barrier()** → 编译错误或死锁
5. **VS 里采样纹理**（vertex texture fetch）：GLSL 4.0+ 允许 sampler2D 等在任意阶段使用；但导数函数（`dFdx`/`dFdy` 等）仅 FS 可用，VS 中须用 `textureLod` 显式指定 mip 层
6. **GS max_vertices 不够** → 剩余顶点被丢弃
7. **UBO/std140 对齐算错** → 数据错位，第 5 节详解

---

# 4. Buffer、Texture 与 Image

## 4.1 Buffer 家族对比

| 类型 | 绑定目标 | GLSL 访问方式 | 读写 | 典型大小 | 用途 |
|------|---------|--------------|------|---------|------|
| VBO | `GL_ARRAY_BUFFER` | vertex attribute `in` | 读 | 任意 | 顶点数据 |
| IBO | `GL_ELEMENT_ARRAY_BUFFER` | （间接） | 读 | 任意 | 索引 |
| UBO | `GL_UNIFORM_BUFFER` | uniform block | 读 | 16~64KB | 共享常量 |
| SSBO | `GL_SHADER_STORAGE_BUFFER` | buffer block | **读写** | ≥128MB | GPU 读写数据 |
| TBO | `GL_TEXTURE_BUFFER` | samplerBuffer | 读 | ≥64MB | 一维数据数组 |
| Atomic Counter | `GL_ATOMIC_COUNTER_BUFFER` | `atomic_uint` | 原子增减 | KB | 计数器 |
| Transform Feedback | `GL_TRANSFORM_FEEDBACK_BUFFER` | （写） | 写 | 任意 | 捕获 VS/GS 输出 |
| Indirect | `GL_DRAW_INDIRECT_BUFFER` / `GL_DISPATCH_INDIRECT_BUFFER` | （间接） | — | 小 | GPU 发起 draw/dispatch |

### VBO / 顶点属性缓冲

见 3.2 ①。补充：现代 OpenGL 用 `glVertexAttribFormat` + `glVertexAttribBinding` 分离格式与缓冲绑定（DSA 风格）。

### UBO（Uniform Buffer Object）

```glsl
layout(std140, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    vec3 camPos;
    float nearFar[2];
};
```

```cpp
GLuint ubo; glGenBuffers(1, &ubo);
glBindBuffer(GL_UNIFORM_BUFFER, ubo);
glBufferData(GL_UNIFORM_BUFFER, size, data, GL_DYNAMIC_DRAW);
glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);   // binding=0
// 之后所有含此block的program自动读到
```

**特点：**
- **多个 program 共享同一份数据**（默认块 uniform 是每 program 一份）
- 一次 `glBufferSubData` 更新全部 → 批量高效
- shader 内**只读**
- 限制：`GL_MAX_UNIFORM_BLOCK_SIZE`（实现至少 16KB，桌面常见 64KB）
- 对齐规则：std140 较松（详见 5.2），std140 下 `vec3` 占 16 字节

### SSBO（Shader Storage Buffer Object）

```glsl
layout(std430, binding = 1) buffer Particles {
    vec4 posLife[];       // xyz=位置, w=生命
    vec4 velMass[];
    uint aliveCount;      // 头部元数据
};
```

```cpp
GLuint ssbo; glGenBuffers(1, &ssbo);
glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
glBufferData(GL_SHADER_STORAGE_BUFFER, N * 32, initData, GL_DYNAMIC_COPY);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
```

**特点：**
- shader 内**可读可写**，支持原子操作（`atomicAdd/Min/Max/Exchange/CompSwap` 及 `atomic*` 的 int/uint 版本）
- 大小远超 UBO（`GL_MAX_SHADER_STORAGE_BLOCK_SIZE` ≥ 128MB）
- **末尾成员可以是 unsized 数组**，长度由 buffer 实际大小决定（`length()` 查询）
- 所有阶段可用（包括 FS/CS）
- 可在 compute 中直接写，下一 draw 直接读（需 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`）

**典型用途：** GPU 粒子系统、光子映射、GPU 剔除、蒙皮结果缓存、实例数据。

### TBO（Texture Buffer）

```glsl
layout(binding = 0) uniform samplerBuffer particleData;   // 只能一维、无过滤

float x = texelFetch(particleData, 123).x;   // 按int索引取texel，无过滤无归一化
```

- 本质是把 buffer 当一维纹理访问
- **无过滤、无 mipmap、无 wrap**，只支持 `texelFetch`
- 在 SSBO 出现前是"大数组读取"的主力；现在多数场景可被 SSBO 替代，但某些硬件上采样吞吐更好
- 内部格式受限：`GL_RGBA32F`、`GL_R32UI` 等"纹理"格式

### Atomic Counter

```glsl
layout(binding = 0, offset = 0) uniform atomic_uint counter;

uint n = atomicCounterIncrement(counter);
uint m = atomicCounterDecrement(counter);
uint c = atomicCounter(counter);
```

- GPU 端原子递增/递减一个 32 位 uint
- 比 SSBO 原子操作**更快**（专用硬件路径）
- 用途：遮挡剔除中统计可见实例数、OIT（per-pixel linked list 的头指针生成）

## 4.2 纹理类型全解

### 基础采样器类型

| 类型 | 维度 | 说明 |
|------|------|------|
| `sampler1D` | 1D | 基本不用了 |
| `sampler2D` | 2D | **最常用** |
| `sampler3D` | 3D | 体积数据（雾、SDF、医学） |
| `samplerCube` | Cube | 天空盒、环境反射（6 面） |
| `sampler1DArray` / `sampler2DArray` | 数组 | 多层纹理，第三维=layer |
| `samplerCubeArray` | 数组cube | 多个环境（4.0+） |
| `sampler2DRect` | 2D 矩形 | 像素坐标采样，无归一化 |
| `samplerBuffer` | TBO | 见上 |
| `sampler2DMS` | 2D 多重采样 | MSAA 纹理，`texelFetch` 访问 |

### 变体前缀

| 前缀 | 元素类型 |
|------|---------|
| `sampler*` | float（归一化） |
| `isampler*` | int |
| `usampler*` | uint |
| `sampler*Shadow` | 深度比较采样（`sampler2DShadow`） |

### 浮点 vs 无符号整数采样

```glsl
uniform sampler2D colorTex;    // 内部格式 GL_RGBA8 → 采样结果归一化到 [0,1]
uniform usampler2D idTex;      // 内部格式 GL_R32UI → 采样结果是原始uint
uniform isampler3D sdfTex;     // GL_R32I → int

vec4 c = texture(colorTex, uv);
uint id = texture(idTex, uv).r;
```

### 采样 vs 取fetch

```glsl
vec4 a = texture(tex, uv);              // 带过滤/归一化坐标/wrap
vec4 b = texelFetch(tex, ivec2(x,y), 0);// 直接按texel索引取，无过滤
vec4 c = textureLod(tex, uv, 3.0);      // 显式mipmap层
vec4 d = textureOffset(tex, uv, ivec2(1,0));  // 采样+偏移（编译期常量）
int   e = textureQueryLevels(tex);      // 查询mip层数
ivec2  f = textureSize(tex, 0);         // 查询某层尺寸
```

### sampler 数组

```glsl
// 方法1：数组 + 常量索引（推荐给数组uniform指定binding）
layout(binding = 0) uniform sampler2D tex[4];   // 占用单元 0,1,2,3

// 方法2：4.0+ 允许"动态统一索引"（同一draw内所有invocation相同值）
uniform sampler2D texArr[16];
vec4 c = texture(texArr[materialIndex], uv);   // materialIndex须uniform值

// 方法3：现代做法——数组纹理
uniform sampler2DArray texArr;
vec4 c = texture(texArr, vec3(uv, layer));     // 第三维=层
```

**限制：** sampler 是不透明类型：
- 只能声明为 uniform（不能是局部变量、不能放进 out/in 接口）
- 比较只能用 `==`/`!=`
- 不能作为函数参数传递，也不能放进 in/out 接口结构体；惯例是内联调用或用宏包装

### Shadow Sampler（深度比较）

```glsl
uniform sampler2DShadow shadowMap;   // 内部格式须是 GL_DEPTH_COMPONENT*

// 传统：texture做一次比较
float lit = texture(shadowMap, vec3(uv, currentDepth));  // 返回 [0,1] 通过率

// 现代更常用：普通sampler手动比较+PCF
```

## 4.3 Image（可读写图像）

`image*` 类型让 shader 能**随机读写单个 texel**，绕过采样器（无过滤、无 mipmap）。

```glsl
// 声明：必须指定内部格式 + binding
layout(rgba32f, binding = 0) uniform image2D colorImage;
layout(rgba8,   binding = 1) uniform readonly image2D inputImage;
layout(r32ui,   binding = 2) uniform uimage2D counterImage;   // 整数图像才能原子操作

// 读写
vec4 v = imageLoad(colorImage, ivec2(x, y));       // ivec坐标，无归一化
imageStore(colorImage, ivec2(x, y), vec4(1,0,0,1));
ivec2 sz = imageSize(colorImage);

// 原子操作（只支持整数图像：r32i/r32ui等）
uint old = imageAtomicAdd(counterImage, ivec2(x,y), 1u);
```

```cpp
// C++ 端绑定图像单元
glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
//       单元, 纹理, mip层, 层化?, 首层, 访问模式, 格式(必须与shader声明一致)
```

**关键限制：**

1. **格式必须匹配**：shader 里 `rgba32f` 必须与纹理创建时 internalFormat 完全一致（`GL_RGBA32F`），否则**未定义行为**（往往表现为读出全 0/乱数据）
2. **支持的格式有限**：`rgba32f`, `rgba16f`, `rg32f`, `r32f`, `rgba32ui`, `rgba16`, `rgba8`, `r32ui` 等（见规范表 8.25）
3. **原子操作只对整数格式**（`r32i`/`r32ui`/`rgba32i`/`rgba32ui`）
4. **无过滤、无 mipmap 采样**——是"图像单元"不是采样器
5. 可读写 `image2DMS`（需 sample 索引）
6. **同步**：image 写入后，要让后续 dispatch/读取看到，需 shader 内 `memoryBarrier()` 系列或 CPU 端 `glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)`

**典型用途：** GBuffer 自定义压缩、OIT 的 per-pixel 链表头、GPU 光子图、计数纹理、粒子写入、体积数据 slice 写入。

## 4.4 三者关系一图

```
        Texture（GPU资源）
        ├── 作为 sampler：有过滤/mipmap/wrap，只读（写=渲染到FBO）
        ├── 作为 image  ：无过滤，随机读写，原子操作
        └── 作为 FBO attachment：作为渲染目标

        Buffer（GPU资源）
        ├── GL_ARRAY_BUFFER        → vertex attributes
        ├── GL_UNIFORM_BUFFER      → uniform block（只读）
        ├── GL_SHADER_STORAGE      → buffer block（读写+原子）
        ├── GL_TEXTURE_BUFFER      → samplerBuffer / imageBuffer
        └── GL_ATOMIC_COUNTER      → atomic_uint
```

**选型建议：**

| 需求 | 推荐 |
|------|------|
| 采样读取 + 硬件过滤 + mipmap | sampler 纹理 |
| 随机读像素 | sampler + texelFetch 或 image readonly |
| 随机写像素 | image（格式匹配） |
| 需要原子操作的像素数据 | image（整数格式） |
| 大型结构化数组读写 | SSBO |
| 大型结构化数组只读 | SSBO（readonly）或 UBO（≤64KB） |
| 多 shader 共享的常量 | UBO |
| 计数 | atomic counter（快）或 SSBO 原子（通用） |

## 4.5 内存模型与同步

GPU 并行写入带来的可见性问题需要显式同步：

**Shader 内部（同一次 dispatch）：**

```glsl
coherent uniform image2D img;
...
memoryBarrier();          // 所有image/buffer写入对后续读取可见（本invocation视角）
// 常用变体：
memoryBarrierImage();     // image 单元
memoryBarrierBuffer();    // SSBO/UBO
memoryBarrierShared();    // shared变量
groupMemoryBarrier();     // 同组
barrier();                // 执行屏障（等待组内全部线程）——常与memoryBarrierShared连用
```

**跨 draw/dispatch（CPU 端控制）：**

```cpp
glDispatchCompute(...);
glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
glDrawArrays(...);   // 之后的draw看到compute的写入
// GL_TEXTURE_UPDATE_BARRIER_BIT / GL_FRAMEBUFFER_BARRIER_BIT / GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT 等
```

**原则：** 谁消费写入，就打对应 barrier bit。bit 打多了只是浪费，打少了是数据错乱。

---

# 5. Interface Block

Interface Block = **一组接口变量打包，带块名、可选实例名、可选 layout**。

## 5.1 四种 Interface Block

| 块类型 | 限定符 | 作用 | 背后的 buffer |
|--------|--------|------|--------------|
| uniform block | `uniform` | 打包一组 uniform | UBO（`GL_UNIFORM_BUFFER`） |
| buffer block | `buffer` | 打包可读写存储 | SSBO（`GL_SHADER_STORAGE_BUFFER`） |
| in/out block | `in` / `out` | 打包跨阶段接口 | 无（走 varying 通道） |
| gl_PerVertex | （内置） | VS→GS/TCS/FS 的内置顶点数据 | 无 |

## 5.2 uniform block（与 UBO 配合）

### 声明与使用

```glsl
// 声明（可无实例名，则成员直接可见）
layout(std140, binding = 0) uniform LightBlock {
    vec4 lightPos[4];    // 位置+范围
    vec4 lightColor[4];  // 颜色+强度
    int lightCount;
};

void main() {
    for (int i = 0; i < lightCount; ++i) {   // 直接用 lightCount
        vec3 L = lightPos[i].xyz;
        ...
    }
}
```

```glsl
// 带实例名（推荐：命名空间清晰）
layout(std140, binding = 0) uniform LightBlock {
    vec4 lightPos[4];
    vec4 lightColor[4];
    int lightCount;
} lights;                      // 实例名

void main() {
    for (int i = 0; i < lights.lightCount; ++i) {
        vec3 L = lights.lightPos[i].xyz;
    }
}
```

### std140 对齐规则（最重要！）

CPU 端排布必须与 GLSL 侧 std140 规则一致，否则数据错位：

| 类型 | base alignment | 尺寸 |
|------|---------------|------|
| `float/int/uint/bool` | 4 | 4 |
| `vec2` | 8（2×4） | 8 |
| `vec3` | **16**（按 vec4 对齐！） | 12 |
| `vec4` | 16 | 16 |
| `mat2` | 8（两列 vec2） | 16 |
| `mat3` | 16（按 3×vec4 排，每列16） | **48（浪费12）** |
| `mat4` | 16 | 64 |
| `struct` | 对齐到成员的最大对齐，整体大小补齐到对齐倍数 | — |
| `T array[N]`（非vec3/vec3数组） | 元素对齐 ×N | — |
| `vec3 array[N]`（std140） | **每个元素 16 字节对齐**（即 16×N，不是 12×N） | — |

**std140 陷阱：**

```glsl
layout(std140) uniform Bad {
    vec3 a;      // offset 0,  size 12, 下一个成员须从16的倍数开始
    float b;     // offset 12 —— b 可以紧贴在 a 后面（12是4的倍数且 <16边界内的尾部）✓ 实际std140里b在12
    vec3 c;      // 必须 offset 16（对齐16）
};
// 注意：std140 里 vec3 后跟 float 是紧凑的（12+4=16），
//       但 vec3 后跟 vec3 必须 16 对齐 → 中间空 4 字节

layout(std140) uniform AlsoBad {
    mat3 m;      // 48字节（3列，每列按16对齐）
    ...
};
```

**std430（仅 SSBO）与 std140 的差异只有两点：**

1. **数组元素的 stride** 不再向上取整到 vec4（16 字节），而是取元素自身的 base alignment：
   - `float arr[]`：std140 中 stride = 16，std430 中 stride = 4
   - `vec2 arr[]`：std140 中 stride = 16，std430 中 stride = 8
   - `vec4 arr[]`：两种布局都是 16
   - ⚠️ `vec3 arr[]`：**两种布局下 stride 都是 16**——vec3 的 base alignment 本身就是 16（GLSL 硬规则），常被误认为 std430 下是 12
2. **结构体的 base alignment** 不再向上取整到 16，而是成员中的最大对齐：
   - `struct { float a; float b; }`：std140 中对齐 16（作为数组时 stride 16），std430 中对齐 4（stride 8）

**推论：** std430 下 `struct { vec3 p; float w; }` 大小 16 字节（p 占 0..11，w 补在 12..15），数组 stride 16；`struct { float a; float b; }` 大小 8 字节，数组 stride 8。

**务必用 `offsetof`/手写对齐计算或工具（glslangValidator、`std140_layout` 生成器）核对。** 实践建议：
1. **尽量避免 vec3，改用 vec4**（配合 padding 注释）——一劳永逸
2. 整数和浮点成组放，vec3 后手动 pad 一个 float
3. mat3 尽量避免（用 mat4）

### named uniform block 的优势

- **跨 program 共享**：一份 UBO 数据多个 program 读（如全局 VP 矩阵）
- **一次更新**：`glBufferSubData` 比逐个 `glUniform` 快
- **binding 布局固定**：`layout(binding=0)` 后所有 program 的该块都指向单元 0，无需 `glGetUniformBlockIndex` + `glUniformBlockBinding`

## 5.3 buffer block（与 SSBO 配合）

```glsl
layout(std430, binding = 1) buffer ParticleSSBO {
    uint count;
    Particle particles[];     // 变长数组
};

void update() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= count) return;
    particles[i].pos += particles[i].vel * dt;
    uint slot = atomicAdd(count, 0);   // 原子读（atomicAdd+0）
}
```

- 成员可写、可原子操作
- **最后一个成员可以 unsized**，`particles.length()` 在运行时返回实际长度
- 访问成员时若无实例名直接用名字；有实例名 `data.particles[...]`

## 5.4 in/out block（跨阶段接口打包）

```glsl
// ---------- VS ----------
out VertexData {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vOut;                    // 实例名必须！out block 必须有实例名

// ---------- FS ----------
in VertexData {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vOut;                    // 块名、成员、类型必须一致；实例名可以不同！

void main() {
    vec3 n = normalize(vOut.normal);
    ...
}
```

**规则：**
- 块名、成员名、类型、限定符必须两端一致；**实例名不需要一致**（实例名只是本阶段内的访问命名空间）
- 带实例名的 block 在 GS/TCS/TES 的输入侧会**自动数组化**（`vIn[]`），无需实例名时用不了这种语法
- GS 接收时：

```glsl
// GS
in VertexData {
    vec3 worldPos;
    vec3 normal;
} vIn[];                   // 自动成为数组，长度=图元顶点数

// TES
in VertexData {
    ...
} vIn[];                   // 同样数组化
```

**好处：**
1. 接口一改全改（编译期检查匹配）
2. 更高效（驱动可能整块传递）
3. 与 `gl_PerVertex` 统一风格

## 5.5 gl_PerVertex（内置接口块）

VS 的 `gl_Position`、`gl_PointSize` 等其实是一个内置 out block 的成员：

```glsl
// VS 里可以"重定义"（增删成员）：
out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
    float gl_ClipDistance[];   // 增加裁剪平面
    // vec4 gl_CullDistance[];  // 可选
};

// GS/TCS/TES 接收时（自动数组化）：
in gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
} gl_in[];
```

- 重新声明可以**移除不用的成员**（如不需要 gl_PointSize），省接口槽位
- 两端（VS 的 out 定义 vs GS 的 in 定义）成员集需兼容
- FS 不接收 gl_PerVertex（它接收插值后的 gl_FragCoord 等，见第 6 节）

## 5.6 各种形式的 block 在 C++ 侧的查询方法

### 5.6.0 总览：四种形式 × 查询接口对照

| block 形式 | 块级查询 | 成员级查询 | 绑定 API |
|-----------|---------|-----------|---------|
| uniform block (UBO) | `glGetUniformBlockIndex` / `GL_UNIFORM_BLOCK` | `glGetUniformIndices` + `glGetActiveUniformsiv` | `glUniformBlockBinding` |
| buffer block (SSBO) | `GL_SHADER_STORAGE_BLOCK`（仅统一接口） | `GL_BUFFER_VARIABLE`（仅统一接口） | `glShaderStorageBlockBinding` |
| in/out block | —（无块级资源，成员即资源） | `GL_PROGRAM_INPUT` / `GL_PROGRAM_OUTPUT` | —（查 location 供 VAO/字节码使用） |
| atomic counter buffer | `GL_ATOMIC_COUNTER_BUFFER` | — | `glBindBufferBase` |
| （对照）默认块裸 uniform | `glGetUniformLocation` | 同左 | `glUniform*` 直接设值 |

两代查询接口并存：**旧接口**（GL 3.0/3.1，函数名带 `Uniform/Attrib`）只覆盖 UBO 和裸变量；**统一接口**（`glGetProgramResourceIndex/iv/Name/Location`，GL 4.3）覆盖全部形式。SSBO、in/out block 成员、atomic counter 只有统一接口可查。

### 5.6.1 uniform block（UBO）

```cpp
// ---- 块级 ----
GLuint blockIdx = glGetUniformBlockIndex(prog, "LightBlock");   // 找不到 → GL_INVALID_INDEX
// 统一接口等价写法：
// GLuint blockIdx = glGetProgramResourceIndex(prog, GL_UNIFORM_BLOCK, "LightBlock");

GLint blockSize = 0;
glGetActiveUniformBlockiv(prog, blockIdx, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
// blockSize = 驱动按 std140 算好的整块字节数 → 据此 glNamedBufferStorage(ubo, blockSize, ...)

GLint curBinding = 0;
glGetActiveUniformBlockiv(prog, blockIdx, GL_UNIFORM_BLOCK_BINDING, &curBinding);

// ---- 成员级：查询名 = "块名.成员名"（实例名不参与！）----
const GLchar* names[] = { "LightBlock.lights", "LightBlock.lightCount" };
GLuint mid[2];
glGetUniformIndices(prog, 2, names, mid);       // 成员被优化掉 → GL_INVALID_INDEX

GLint offset[2], arrStride[2], matStride[2], rowMajor[2];
glGetActiveUniformsiv(prog, 2, mid, GL_UNIFORM_OFFSET,       offset);     // 成员偏移
glGetActiveUniformsiv(prog, 2, mid, GL_UNIFORM_ARRAY_STRIDE, arrStride);  // 数组元素步长
glGetActiveUniformsiv(prog, 2, mid, GL_UNIFORM_MATRIX_STRIDE, matStride); // 矩阵列步长
glGetActiveUniformsiv(prog, 2, mid, GL_UNIFORM_IS_ROW_MAJOR, rowMajor);
// 还有 GL_UNIFORM_SIZE（数组长度/向量分量数）、GL_UNIFORM_TYPE、GL_UNIFORM_BLOCK_INDEX

// ---- 绑定（若 shader 侧未写 layout(binding=N)）----
glUniformBlockBinding(prog, blockIdx, 0);       // 块 → binding point 0
glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);    // UBO → binding point 0
```

### 5.6.2 buffer block（SSBO）——全走统一接口

**块级：**

```cpp
GLuint ssboIdx = glGetProgramResourceIndex(prog, GL_SHADER_STORAGE_BLOCK, "Particles");

const GLenum blockProps[] = { GL_BUFFER_BINDING, GL_NUM_ACTIVE_VARIABLES, GL_BUFFER_DATA_SIZE };
GLint blockInfo[3];
glGetProgramResourceiv(prog, GL_SHADER_STORAGE_BLOCK, ssboIdx, 3, blockProps, 3, nullptr, blockInfo);
// blockInfo[2] = GL_BUFFER_DATA_SIZE：含 unsized 数组时只是"最小体积"（unsized 部分按 0/1 个
// 元素计，实现相关）——实际大小由 C++ 侧分配决定，别拿它当容量用
```

**成员级：`GL_BUFFER_VARIABLE` 接口**（SSBO 成员并非"查不了"，只是没有旧式 API）：

```cpp
// 路线 A（推荐）：块 → 成员索引列表 → 逐成员查属性
std::vector<GLint> varIdx(blockInfo[1]);                       // 成员数量来自上面
const GLenum av = GL_ACTIVE_VARIABLES;
glGetProgramResourceiv(prog, GL_SHADER_STORAGE_BLOCK, ssboIdx, 1, &av,
                       (GLsizei)varIdx.size(), nullptr, varIdx.data());

for (GLint vi : varIdx) {
    const GLenum mProps[] = { GL_NAME_LENGTH, GL_TYPE, GL_ARRAY_SIZE,
                              GL_OFFSET, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE,
                              GL_TOP_LEVEL_ARRAY_STRIDE, GL_IS_ROW_MAJOR };
    GLint m[8];
    glGetProgramResourceiv(prog, GL_BUFFER_VARIABLE, vi, 8, mProps, 8, nullptr, m);

    std::vector<GLchar> name(m[0]);
    glGetProgramResourceName(prog, GL_BUFFER_VARIABLE, vi, (GLsizei)name.size(),
                             nullptr, name.data());            // 如 "Particles.posVel"

    // m[3] = 成员偏移   m[4] = 数组元素步长   m[5] = 矩阵列步长
    // m[6] = 顶层（unsized）数组步长   m[2] = 数组长度（unsized 数组为 0）
}

// 路线 B：已知成员名，直接查
GLuint vi = glGetProgramResourceIndex(prog, GL_BUFFER_VARIABLE, "Particles.posVel");
```

**绑定：**

```cpp
glShaderStorageBlockBinding(prog, ssboIdx, 3);  // 块 → binding point 3
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo);
```

### 5.6.3 in/out block（跨阶段接口）

成员即资源，**没有块级索引**。名字规则与 UBO 相反：**实例名参与命名**——有实例名 → `"实例名.成员名"`；无实例名 → `"成员名"`。

```cpp
// ---- VS 输入（顶点属性）：查 location ----
GLuint loc = glGetProgramResourceLocation(prog, GL_PROGRAM_INPUT, "v.pos");
// ⚠ block 成员用 glGetAttribLocation 查不到（返回 -1）！必须走统一接口
glEnableVertexAttribArray(loc);
glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);

// ---- VS 输出 / FS 输入：查类型、location ----
GLuint oIdx = glGetProgramResourceIndex(prog, GL_PROGRAM_OUTPUT, "v.color");
const GLenum oProps[] = { GL_TYPE, GL_LOCATION, GL_ARRAY_SIZE };
GLint vals[3];
glGetProgramResourceiv(prog, GL_PROGRAM_OUTPUT, oIdx, 3, oProps, 3, nullptr, vals);

// ---- FS 输出（MRT）：查 location ----
GLuint rt1 = glGetProgramResourceLocation(prog, GL_PROGRAM_OUTPUT, "fragColor[1]");

// ---- GS/TCS/TES 输入侧自动数组化：名字带下标，每个已用元素是独立资源 ----
GLuint i0 = glGetProgramResourceIndex(prog, GL_PROGRAM_INPUT, "v.n[0]");

// ---- TCS/TES 的 patch 变量 ----
const GLenum pp = GL_IS_PER_PATCH;
GLint isPatch = 0;
glGetProgramResourceiv(prog, GL_PROGRAM_INPUT, someIdx, 1, &pp, 1, nullptr, &isPatch);
```

### 5.6.4 atomic counter buffer（简述）

```cpp
GLint nAcb = 0;
glGetProgramInterfaceiv(prog, GL_ATOMIC_COUNTER_BUFFER, GL_ACTIVE_RESOURCES, &nAcb);
for (int i = 0; i < nAcb; ++i) {
    const GLenum props[] = { GL_ATOMIC_COUNTER_BUFFER_BINDING,
                             GL_ATOMIC_COUNTER_BUFFER_DATA_SIZE };
    GLint vals[2];
    glGetProgramResourceiv(prog, GL_ATOMIC_COUNTER_BUFFER, i, 2, props, 2, nullptr, vals);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, vals[0], acb);
}
// 旧式等价：glGetActiveAtomicCounterBufferiv(prog, i, GL_ATOMIC_COUNTER_BUFFER_BINDING, &b);
```

### 5.6.5 块数组（instanced block）

```glsl
layout(std430, binding = 4) buffer Draw { mat4 mvp; } draws[];   // 每元素占一个连续 binding 单元
```

```cpp
GLuint d0 = glGetProgramResourceIndex(prog, GL_SHADER_STORAGE_BLOCK, "Draw[0]");
GLuint d1 = glGetProgramResourceIndex(prog, GL_SHADER_STORAGE_BLOCK, "Draw[1]");
// 成员查询名：块名[0].成员名"，如 "Draw[0].mvp"（glGetProgramResourceIndex(GL_BUFFER_VARIABLE, ...)）
// 各元素可分别 glShaderStorageBlockBinding 到不同单元；默认依次占 4,5,6...
```

### 5.6.6 查询名规则汇总与注意事项

**查询名规则汇总（最易错点）：**

| 形式 | 块查询名 | 成员查询名 |
|------|---------|-----------|
| uniform block | `块名` | `块名.成员名`（**实例名不参与**，有无实例名都一样） |
| buffer block (SSBO) | `块名` | `块名.成员名`（实例名不参与） |
| 块数组 | `块名[0]`、`块名[1]`… | `块名[0].成员名` |
| in/out block | —（无块级资源） | `实例名.成员名`（**实例名参与**！无实例名则裸 `成员名`） |
| GS/TCS/TES 数组化输入 | — | `实例名.成员名[i]` |
| 默认块裸 uniform | — | `成员名`（`glGetUniformLocation`） |

**注意事项：**

1. **优化剥离**：shader 中未使用的 block/成员可能被链接器整体剥掉，查询返回 `GL_INVALID_INDEX`（或资源不出现）——这是"查询失败"最常见的原因，先确认成员真的被 main 可达路径使用
2. 查询前必须**链接成功**（`glLinkProgram` 后、`GL_LINK_STATUS == GL_TRUE`）
3. `glGetProgramResourceiv` 的属性只能查**该接口合法的属性**（如 `GL_UNIFORM_OFFSET` 对 `GL_BUFFER_VARIABLE` 无效，两者各有一套），传错返回 `GL_INVALID_ENUM`
4. unsized 数组的 `GL_ARRAY_SIZE` 为 0，实际长度 = buffer 大小 ÷ 元素步长，需 C++ 侧自行换算
5. 离线替代方案：`glslangValidator` 编译期反射、SPIRV-Cross 的 `Compiler::get_shader_resources()`——不依赖驱动的反射信息，适合做资源预生成

### 5.6.7 带 "Uniform" 字样的 API 全家福比较（含 subroutine）

OpenGL 中 "Uniform" 一词横跨**三种不同的东西**，API 名字却长得像，是最容易混的一族：

| "Uniform" 指什么 | 存在哪 | 设值方式 | 查询定位方式 |
|-----------------|--------|---------|-------------|
| ① 默认块裸 uniform | program 对象内 | `glUniform*`（按 location） | `glGetUniformLocation` |
| ② 命名 block 内的成员 | UBO 显存里 | 填 buffer（见 5.6.1），**不能** `glUniform*` | `glGetUniformIndices`（"块名.成员名"） |
| ③ subroutine uniform | program 内（按 stage） | `glUniformSubroutinesuiv`（按 stage 整体设置） | `glGetSubroutineUniformLocation` 等 subroutine 族 |

**API 分类清单：**

```cpp
// ========= ① 设值类（只对默认块 uniform 有效）=========
glUniform1f / 2f / 3f / 4f / 1i / 1ui / ...        // 标量/向量
glUniform1fv / 4fv / ...                            // 数组版本（指针 + count）
glUniformMatrix2fv / 3fv / 4fv / 2x3fv / ...        // 矩阵（含 transpose 参数）
// ⚠ 对 block 成员调 glUniform* → GL_INVALID_OPERATION（数据在 UBO 里，不在 program 里）

// ========= ② 定位/枚举类 =========
GLint loc = glGetUniformLocation(prog, "mvp");      // 默认块裸 uniform（找不到 → -1）
GLuint idx = glGetUniformIndices(prog, n, names, out);  // block 成员 → uniform index
glGetActiveUniform(prog, i, ...)                    // 按序号枚举活跃 uniform（含 block 成员，名字 "Block.member"）
glGetActiveUniformName(prog, i, ...)                // 只取名

// ========= ③ 属性查询类（输入是 index，不是 location！）=========
glGetActiveUniformsiv(prog, n, idx, GL_UNIFORM_OFFSET / _TYPE / _SIZE /
                      _ARRAY_STRIDE / _MATRIX_STRIDE / _IS_ROW_MAJOR /
                      _BLOCK_INDEX / _NAME_LENGTH, out);
// glGetActiveUniform（单数）= 老的逐个版本，一次查一个

// ========= ④ 读回类 =========
glGetUniformfv / glGetUniformiv(prog, location, out);
// 怪癖：默认块 → 传 location；block 成员 → 传 uniform index（glGetUniformIndices 的结果）
// （此时 index 兼作 location，规范明文允许）

// ========= ⑤ Block 类（见 5.6.1）=========
glGetUniformBlockIndex / glGetActiveUniformBlockiv / glUniformBlockBinding

// ========= ⑥ Subroutine 类 =========
GLuint loc = glGetSubroutineUniformLocation(prog, GL_FRAGMENT_SHADER, "shadeMode");
GLuint idx = glGetSubroutineIndex(prog, GL_FRAGMENT_SHADER, "phong");
glUniformSubroutinesuiv(GL_FRAGMENT_SHADER, 1, &idx);   // 设值（见下）
glGetUniformSubroutineuiv(GL_FRAGMENT_SHADER, loc, &cur);   // 读回
glGetActiveSubroutineUniform(prog, stage, i, ...)      // 枚举 subroutine uniform 属性
glGetActiveSubroutineUniformName / glGetActiveSubroutineName
glGetProgramStageiv(prog, stage, GL_ACTIVE_SUBROUTINE_UNIFORMS / GL_ACTIVE_SUBROUTINES, &n)
```

**subroutine 完整用法示例（GLSL 4.0+）：**

```glsl
// —— 声明：subroutine 类型（函数签名）——
subroutine vec3 ShadeMode(vec3 n, vec3 v, vec3 l);

// —— 多个候选实现 ——
subroutine(ShadeMode) vec3 phong(vec3 n, vec3 v, vec3 l) { ... }
subroutine(ShadeMode) vec3 lambert(vec3 n, vec3 v, vec3 l) { ... }

// —— subroutine uniform：运行期可换的"函数指针" ——
subroutine uniform ShadeMode shadeMode;

void main() {
    color = shadeMode(normalize(N), V, L);   // 调用点固定，实际执行哪个由 C++ 决定
}
```

```cpp
// C++ 侧：一次 draw 前选好实现
GLuint loc = glGetSubroutineUniformLocation(prog, GL_VERTEX_SHADER, "shadeMode");
GLuint ph   = glGetSubroutineIndex(prog, GL_VERTEX_SHADER, "phong");
GLuint la   = glGetSubroutineIndex(prog, GL_VERTEX_SHADER, "lambert");

glUniformSubroutinesuiv(GL_VERTEX_SHADER, 1, &ph);   // 用 phong
drawMeshA();
glUniformSubroutinesuiv(GL_VERTEX_SHADER, 1, &la);   // 换 lambert
drawMeshB();
```

**subroutine 的三个怪癖：**

1. **按 stage 整体设置**：`glUniformSubroutinesuiv` 的参数是"该 stage 所有 subroutine uniform 的下标数组"，数组第 i 位对应 location 为 i 的 subroutine uniform——即使只有一个也要传长度 1 的数组；不能单独设某一个（想只换一个，得把其他的一并填进数组）
2. **状态属于 program 的 stage**，不是全局 context 状态；换 program 后要重设
3. **每 stage 每次绘制只能激活一套组合**，不同物体要不同实现 → 每次切换都得重新 `glUniformSubroutinesuiv`

**与其它"运行期选择"手段对比：**

| 手段 | 切换粒度 | 开销 | 灵活性 | 备注 |
|------|---------|------|--------|------|
| subroutine | 每次 draw | 极低（改 program 内下标） | 同一 shader 内换函数 | GL 4.0+；**Vulkan 无此机制**；现代 GL 实践中日渐边缘化 |
| 默认块 int uniform + `if/switch` | 每次 draw | 分支（现代 GPU 可忽略） | 灵活 | 最常用，配合 UBool/UBO |
| 多 program 切换 | 每次 draw | program 切换较贵 | 完全独立 | shader 数量爆炸时不利 |
| UBO + 索引表（跳转表） | 每次 draw | 一次间接寻址 | 高 | 函数表思想，SSBO 存参/索引 |
| 专用扩展（NV_shader_thread_*) 等 | — | — | — | 非通用 |

**统一接口等价物（GL 4.3+，`glGetProgramResource*` 一套通吃）：**

| 旧 API | 统一接口等价 |
|--------|-------------|
| `glGetUniformLocation` | `glGetProgramResourceLocation(prog, GL_UNIFORM, name)` |
| `glGetUniformIndices` | `glGetProgramResourceIndex(prog, GL_UNIFORM, name)` |
| `glGetActiveUniformsiv` 各属性 | `glGetProgramResourceiv` + `GL_UNIFORM_*` 同名属性（另多出 `GL_LOCATION`、`GL_REFERENCED_BY_*_SHADER` 等） |
| `glGetActiveUniformName` | `glGetProgramResourceName` |
| `glGetActiveSubroutineUniform*` | `GL_VERTEX_SUBROUTINE_UNIFORM` / `GL_FRAGMENT_SUBROUTINE_UNIFORM` 等接口 |
| `glGetActiveSubroutineName` | `GL_VERTEX_SUBROUTINE` / `GL_FRAGMENT_SUBROUTINE` 等接口 |



---

## 5.7 block 成员写法大全（GLSL 声明 ↔ C++ 镜像 ↔ 引用规则）

本节逐类型给出 block 内成员的完整写法：shader 侧如何声明、C++ 侧如何镜像、如何引用、有哪些硬性要求。

### 5.7.1 成员类型支持总表

| 成员类型 | uniform block | buffer block (SSBO) | in/out block | 备注 |
|----------|:---:|:---:|:---:|------|
| 标量 `int/uint/float/bool` | ✓ | ✓ | ✓ | **bool 在 GLSL 内存占 4 字节**，C++ 镜像必须用 `int32_t`，不能用 `bool`（C++ bool 仅 1 字节） |
| 向量 `vec2/vec3/vec4` 等 | ✓ | ✓ | ✓ | 含 ivec/uvec 等 |
| 矩阵 `matN` / `matCxR` | ✓ | ✓ | ✓ | 列主序，列间有 matrix stride |
| 定长数组 `T name[N]` | ✓ | ✓ | ✓ | N 必须编译期常量 |
| **unsized 数组 `T name[]`** | ✗ | ✓（仅最后一个成员） | ✗ | 运行时 `.length()` 取实际长度 |
| 结构体（可嵌套） | ✓ | ✓ | ✓ | 大小按布局规则补齐 |
| `sampler*` / `image*` | ✗ | ✗ | ✗ | 不透明类型禁止进入任何 block |
| `atomic_uint` | ✗ | ✗ | ✗ | 走独立 Atomic Counter Buffer |
| 成员级限定符 | ✗ | 内存限定符（部分驱动支持，不建议） | ✓（flat/smooth 等插值限定符） | 限定符规则见 5.7.6 |

### 5.7.2 普通变量（标量与向量）

**GLSL 侧：**

```glsl
layout(std140, binding = 0) uniform Globals {
    float time;          // 标量：offset 0，4 字节
    int   frame;         // 标量：offset 4
    bool  enabled;       // 标量：offset 8（GLSL 中占 4 字节！）
    vec2  resolution;    // 对齐 8 → offset 16
    vec3  lightDir;      // 对齐 16 → offset 24
    vec4  baseColor;     // offset 48
} g;                     // 实例名
```

**C++ 侧镜像（手写 std140 对齐）：**

```cpp
struct Globals {                 // 必须是 standard-layout 类型
    float    time;               // 0
    int32_t  frame;              // 4
    int32_t  enabled;            // 8   ← bool 用 int32_t 镜像！
    glm::vec2 resolution;         // 16  ← 前面补了 4 字节 padding（隐式由对齐产生）
    glm::vec3 lightDir;           // 24  ← C++ 中 glm::vec3 只对齐 4！见下方陷阱
    float    _pad0;              // 28  ← 手动补齐到 32
    glm::vec4 baseColor;          // 32
};
static_assert(sizeof(glm::vec4) == 16);
// 上传
glNamedBufferSubData(ubo, 0, sizeof(Globals), &globals);
```

**关键陷阱：** `glm::vec3` / `float[3]` 在 C++ 中对齐只有 4，而 std140 要求 vec3 成员对齐 16。**必须手动加 padding 成员**，否则整个结构体错位。这也是"能用 vec4 就别用 vec3"的根本原因。

**引用（shader 内）：**

```glsl
// 有实例名：g.time、g.lightDir
float t = g.time;

// 无实例名：成员直接暴露到全局作用域
layout(std140, binding = 0) uniform Globals {
    float time;
    vec2  resolution;
};
void main() { ... time ... resolution ... }
```

**引用（C++ 侧查询 location —— 注意名字规则）：**

```cpp
// 块成员查询名 = "块名.成员名"，实例名不参与命名！
GLint loc = glGetUniformLocation(prog, "Globals.time");      // ✓
GLint bad = glGetUniformLocation(prog, "g.time");            // ✗ 错，实例名不属于查询名
```

### 5.7.3 矩阵成员

**GLSL 侧：**

```glsl
layout(std140, binding = 1) uniform Matrices {
    mat4 mvp;          // 64 字节：4 列 × 列步长 16
    mat3 normalMat;    // 48 字节：3 列 × 列步长 16（每列当 vec4 填充）
    mat2x3 m23;        // 2 列，每列 vec3 → std140 列步长 16 → 32 字节
} m;
```

**要点：**
- GLSL 矩阵 = **列向量数组**，"matrix stride"（列间距）由布局决定：
  - std140：每列一律按 16 字节槽位 → mat4 步长 16、mat3 步长 16（浪费 12/列）
  - std430：列步长 = 列向量类型的 base alignment（mat4→16，**mat3 列是 vec3 仍是 16**，mat2x3 的列 vec2→8）
- `mat3` 两种布局下都是 48 字节、列步长 16 —— **std430 救不了 mat3**，工程上统一用 mat4
- 列主序与 `glm::mat4` 的默认内存排布一致，可整块 memcpy

**C++ 侧：**

```cpp
struct Matrices {
    glm::mat4 mvp;        // 0..63，glm 默认列主序，直接对应 std140
    glm::mat3 normalMat;  // C++ 只有 36 字节！std140 需要 48 —— 不能直接镜像
};

// 方案 A：mat3 手动扩展为 3 个 vec4
struct MatricesSafe {
    glm::mat4 mvp;
    glm::vec4 normalCol0, normalCol1, normalCol2;   // 3×16 = 48
};

// 方案 B：逐成员按查询到的 offset/matrixStride 上传
GLint matStride;
glGetActiveUniformsiv(prog, 1, &idx, GL_UNIFORM_MATRIX_STRIDE, &matStride);
for (int c = 0; c < 3; ++c)
    glNamedBufferSubData(ubo, offNormal + c * matStride,
                         12, glm::value_ptr(normalMat)[c] /* 或 &col.x */);
```

**矩阵成员的元素级读/写（GLSL 侧）**——矩阵本质是"列向量数组"，一切访问按 `[列]` 优先：

```glsl
uniform Matrices { mat4 mvp; mat3 nm; } m;

// 读
vec4 col0   = m.mvp[0];        // 第 0 列整体
float m00   = m.mvp[0][0];     // [列][行]！与数学记法 row,col 相反
float m12   = m.mvp[1][2];     // 第 1 列第 2 行
vec3 transl = m.mvp[3].xyz;    // 平移分量在第 3 列（列主序的直接好处）
vec3 xAxis  = m.nm[0];         // mat3 的列，隐式得 vec3

// 写（局部变量 / SSBO 中的矩阵）
mat4 model = mat4(1.0);            // 单位阵
model[3] = vec4(offset, 1.0);      // 整列赋值：直接改平移列
model[3].xyz += delta;             // 列内分量
model[0][0] = 2.0;                 // 单元素
mat4 composed = mat4(c0, c1, c2, c3);   // 4 个列向量构造
```

**C++（glm）侧完全同构**——glm 的 `operator[]` 也返回列引用，`[列][行]` 与 GLSL 一致：

```cpp
glm::mat4 m(1.0f);
glm::vec4 col0 = m[0];
float     m00  = m[0][0];
m[3] = glm::vec4(pos, 1.0f);    // 改平移列
m[3].xyz += delta;

glm::value_ptr(m);              // → float*：16 个 float 列主序连续排列
```

**填进 UBO 的完整流程：**

```cpp
// mat4：glm 内存布局 == std140 布局（列主序 + 列步长 16）→ 整块直拷
glm::mat4 mvp = proj * view * model;
glNamedBufferSubData(ubo, offMvp, sizeof(glm::mat4), glm::value_ptr(mvp));

// mat3：std140 需要 48B（每列占 16B 槽），glm::mat3 是 36B 紧凑排列 → 逐列补齐
glm::mat3 nm = glm::mat3(model);
glm::vec4 padded[3];
for (int c = 0; c < 3; ++c)
    padded[c] = glm::vec4(nm[c], 0.0f);          // 每列 12B 有效 + 4B padding
glNamedBufferSubData(ubo, offNm, sizeof(padded), &padded[0].x);
```

### 5.7.4 数组成员

**GLSL 侧：**

```glsl
layout(std140, binding = 2) uniform ArrayBlock {
    float weights[4];     // std140：元素步长 16（补齐到 vec4！）→ 64 字节
    vec4  positions[8];   // 步长 16 → 128 字节
    mat4  bones[64];      // 步长 64 → 4096 字节（骨骼动画典型用法）
};

layout(std430, binding = 3) buffer Particles {
    uint   count;         // offset 0
    vec4   posVel[];      // unsized：仅 SSBO 末位成员，步长 16
    // .length() 运行时返回实际元素数
};

// shader 内引用
void main() {
    float w = weights[2];
    vec4 pv = posVel[gl_GlobalInvocationID.x];
    int n = posVel.length();     // SSBO 专属
}
```

**数组步长是布局差异的重灾区：**

| 数组类型 | std140 元素步长 | std430 元素步长 |
|----------|:---:|:---:|
| `float a[]` | 16 | 4 |
| `int a[]` | 16 | 4 |
| `vec2 a[]` | 16 | 8 |
| `vec3 a[]` | 16 | **16**（vec3 硬规则） |
| `vec4 a[]` | 16 | 16 |
| `struct{float a,b;} s[]` | 16 | 8 |

**C++ 侧（std140 的 float 数组必须手动填充步长）：**

```cpp
// ❌ 直接 float w[4] = {...} 上传 —— 步长错（4 vs 16）
// ✓ 正确镜像：
struct ArrayBlock {
    glm::vec4 weights[4];     // 每个元素只用 .x，其余是 padding
    glm::vec4 positions[8];
};
// 上传时把 float 塞进 vec4：
for (int i = 0; i < 4; ++i) data.weights[i] = glm::vec4(w[i], 0, 0, 0);
```

**SSBO unsized 数组的 C++ 侧：** 实际长度由 `glNamedBufferData`/`glBufferStorage` 分配的字节数决定，shader 端 `.length()` 据此计算；C++ 端无法反查，需要容量时通常另存一个 `count` 成员或按 buffer size 换算。

**GLSL 侧使用：**

```glsl
// 索引可以是任意整型表达式：循环变量、uniform、计算值、甚至 SSBO 里的数据
// （与 sampler 数组"必须常量索引"的限制不同，普通数组无此限制）
float w = weights[2];
vec4  p = positions[i * 2 + 1];
mat4  b = bones[vBoneIdx];              // 数组元素是矩阵
vec4  skinned = b * vec4(aPos, 1.0);

// UBO 数组只读；循环遍历
for (int i = 0; i < 4; ++i)
    sum += weights[i] * positions[i].x;

// SSBO 数组可读可写（compute shader 典型模式）
uint idx = gl_GlobalInvocationID.x;
if (idx >= posVel.length()) return;      // 先做长度守卫
posVel[idx].xy += posVel[idx].zw * dt;   // 读-改-写
posVel[idx] = vec4(newPos, newVel);      // 整元素覆写
```

**C++ 侧填值与更新：**

```cpp
// std140 定长数组：按"元素步长"填（float 数组步长 16，见上表）
struct ArrayBlock {
    glm::vec4 weights[4];       // float[4] 的 std140 镜像
    glm::vec4 positions[8];
} data;
for (int i = 0; i < 4; ++i)
    data.weights[i] = glm::vec4(w[i], 0, 0, 0);   // 有效值放 .x
glNamedBufferSubData(ubo, 0, sizeof(data), &data);

// 部分更新：只改 bones[3]（元素步长 64）
glNamedBufferSubData(ubo, offBones + 3 * 64, sizeof(glm::mat4), &bones[3]);

// SSBO 全量重传（简单场景够用）
std::vector<glm::vec4> pv(1024);
glNamedBufferData(ssbo, sizeof(glm::vec4) * pv.size(), pv.data(), GL_DYNAMIC_DRAW);
glNamedBufferSubData(ssbo, 0, bytes, pv.data());

// 高频更新首选 persistent mapping：map 一次，长期直写（配 fence 同步）
void* p = glMapNamedBufferRange(ssbo, 0, bytes,
           GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
// 之后 ((glm::vec4*)p)[i] 随时可写
```

**std430 的标量数组才是紧凑的**——这是海量 float/int 数据选 SSBO+std430 的核心理由：

```glsl
layout(std430, binding = 7) readonly buffer Curve {
    float knots[];    // std430：步长 4，真·紧凑（std140 下会是 16！）
};
```

```cpp
std::vector<float> knots = { /* ... */ };   // 不再需要 vec4 padding
glNamedBufferData(ssbo, knots.size() * 4, knots.data(), GL_STATIC_DRAW);
```

### 5.7.5 结构体成员（含嵌套）

**GLSL 侧：**

```glsl
struct Light {
    vec3  position;     // offset 0,  12 字节
    float radius;       // offset 12
    vec3  color;        // offset 16
    float intensity;    // offset 28
};                      // std140：结构体对齐 = max(成员对齐) 再补齐到 16 → 32 字节

layout(std140, binding = 4) uniform LightBlock {
    Light  lights[4];   // std140 数组步长 = 32（结构体补齐后大小）
    int    lightCount;
} ubLights;

// 嵌套结构体
struct Material {
    vec4  albedo;
    float roughness;
    float metalness;
    float _padA;        // 建议显式 padding 成员，注释标明
    float _padB;
};                      // 32 字节

struct DrawItem {
    Material material;  // offset 0
    mat4     transform; // offset 32
};                      // 96 字节
```

**结构体规则（std140 vs std430）：**

| 规则 | std140 | std430 |
|------|--------|--------|
| 结构体 base alignment | 成员最大对齐，**再向上取整到 16** | 成员最大对齐（不取整到 16） |
| 结构体总大小 | 补齐到 base alignment 的整数倍 | 同左（但 alignment 更小） |
| `struct{float a,b;}` 大小/数组步长 | 16 / 16 | 8 / 8 |
| `struct{vec3 p; float w;}` | 16 / 16 | 16 / 16（vec3 拉高了对齐） |

**C++ 侧镜像：**

```cpp
struct alignas(16) Light {
    glm::vec3 position;   // C++ 对齐 4 —— 靠 alignas(16) + 手动 padding 保证
    float     radius;
    glm::vec3 color;
    float     intensity;
};
static_assert(sizeof(Light) == 32);      // 编译期验证布局

struct LightBlock {
    Light lights[4];
    int32_t lightCount;   // 注意 lightCount 的 offset = 128（4×32）
};
static_assert(offsetof(LightBlock, lightCount) == 128);
```

**GLSL 侧使用：**

```glsl
// 成员访问：有实例名 → 实例名.成员（数组元素再加 [i]）
vec3  lp = ubLights.lights[i].position;
float lr = ubLights.lights[i].radius;

// 局部构造：按声明顺序逐成员给值（结构体名即构造函数）
Light l = Light(vec3(0, 5, 0), 10.0, vec3(1.0), 4.0);

// 当函数参数传递
vec3 shade(Light l, vec3 P) {
    float d2 = dot(P - l.position, P - l.position);
    return l.color * (l.intensity / (1.0 + d2 / (l.radius * l.radius)));
}
```

**C++ 侧填值：**

```cpp
LightBlock data{};
for (int i = 0; i < 4; ++i) {
    data.lights[i].position = positions[i];   // glm::vec3 直接赋
    data.lights[i].radius   = 5.0f;
}
glNamedBufferSubData(ubo, 0, sizeof(data), &data);

// 只更新第 3 盏灯：offset = 3 * sizeof(Light)（= 96，元素步长即结构体大小）
glNamedBufferSubData(ubo, 3 * sizeof(Light), sizeof(Light), &oneLight);
```

**结构体数组（AoS）vs 数组结构体（SoA）——海量数据的两种组织方式**

```glsl
// ============ AoS：Array of Structs（结构体数组）============
// 一个元素的所有字段连续存放
struct Particle { vec4 pos; vec4 vel; };        // std430 元素步长 32
layout(std430, binding = 5) buffer PS { Particle particles[]; };

uint i = gl_GlobalInvocationID.x;
particles[i].pos.xyz += particles[i].vel.xyz * dt;   // 一次访存拿全字段

// ============ SoA：Struct of Arrays（数组结构体）============
// 同一字段的所有元素连续存放
// ✗ 一个 block 里不允许两个 unsized 数组（unsized 只能是最后一个成员）！
buffer PS2 { vec4 pos[]; vec4 vel[]; };         // 编译错误

// ✓ 写法 1：前面的数组定长，只留最后一个 unsized
buffer PS2 { vec4 pos[4096]; vec4 vel[]; };

// ✓ 写法 2（工程惯例）：拆成多个独立 SSBO
layout(std430, binding = 5) buffer PosBuf { vec4 positions[]; };
layout(std430, binding = 6) buffer VelBuf { vec4 velocities[]; };
positions[i].xyz += velocities[i].xyz * dt;
```

```cpp
// ============ C++ 侧镜像 ============
// AoS：一个结构体数组 → 一个 buffer
struct Particle { glm::vec4 pos, vel; };        // 32B
static_assert(sizeof(Particle) == 32);
std::vector<Particle> ps(1024);
glNamedBufferData(ssbo, sizeof(Particle) * ps.size(), ps.data(), GL_DYNAMIC_DRAW);
// 更新单个粒子：offset = i * sizeof(Particle)

// SoA：多个平铺数组 → 多个 buffer
std::vector<glm::vec4> pos(1024), vel(1024);
glNamedBufferData(posSSBO, sizeof(glm::vec4) * pos.size(), pos.data(), GL_DYNAMIC_DRAW);
glNamedBufferData(velSSBO, sizeof(glm::vec4) * vel.size(), vel.data(), GL_DYNAMIC_DRAW);
```

| | AoS 结构体数组 | SoA 数组结构体 |
|---|---|---|
| GLSL 访问 | `particles[i].pos` | `positions[i]` / `velocities[i]` |
| 一个 block 多个 unsized 数组 | 不涉及 | ✗ 非法 → 拆多个 SSBO |
| C++ 镜像 | 一个 `struct` 数组 | 多个 `vector` |
| 全字段都读写的核（粒子积分） | ✓ 一次访存拿全字段 | 需读多个 buffer |
| 只用部分字段的核（只读 pos 渲染） | ✗ 无用字段浪费带宽（32B 只用一半） | ✓ 只拉需要的字段 |
| 部分更新粒度 | 按元素 `i * sizeof(T)` | 按字段按 buffer 独立更新 |
| 典型场景 | 光源表、材质表、DrawItem 表 | 海量粒子、GPU 排序/裁剪输出、逐字段流式处理 |

**选用经验：**
- 表格类小数据（几～几百项，字段总是配套使用）→ **AoS**：Light、Material、DrawItem
- 海量数据 + 不同 pass 只碰部分字段 → **SoA**：粒子系统、实例化渲染的 per-instance 数据
- GPU 上 warp 内相邻线程访问相邻 i，AoS 的 32B 块同样 coalesced，**性能差距远小于 CPU SIMD 场景**——先按可维护性选，profile 后再调

### 5.7.6 引用规则与硬性要求汇总

**shader 内引用：**

```glsl
// uniform/buffer block：
uniform B { float x; } b;      → b.x
uniform B { float x; };        → x（无实例名，成员直通全局作用域）

// in/out block：必须带实例名（见 5.4）
out VertexData { vec3 n; } v;  → v.n

// GS/TCS/TES 输入侧自动数组化：
in VertexData { vec3 n; } v[]; → v[i].n
```

**C++ 侧查询命名规则：**

| 目标 | 查询名 | API |
|------|--------|-----|
| block 本身 | `块名` | `glGetUniformBlockIndex` / `glGetProgramResourceIndex` |
| block 成员 | `块名.成员名` | `glGetUniformIndices` |
| 数组成员元素 | `块名.成员名[0]`（查步长看第 0 个即可） | `glGetUniformIndices` |
| SSBO 成员 | `GL_BUFFER_VARIABLE` 接口查询（见 5.6.2）；无旧式 API，手算布局仍是常态 | `glGetProgramResourceiv(GL_OFFSET/GL_ARRAY_STRIDE/...)` |

**硬性要求（违规即编译/链接错误或数据错乱）：**

1. 不透明类型（sampler/image/atomic_uint）禁止出现在任何 block 中
2. uniform block 内成员不能加插值/存储限定符；in/out block 成员**可以**加 `flat` 等插值限定符
3. **同一块名在多个 shader 间的声明必须逐字节一致**（成员名、类型、顺序、布局），否则链接失败
4. unsized 数组只能是 buffer block 的最后一个成员
5. 块名与实例名不能同名；两个不同 block 的成员若无实例名，不得重名
6. C++ 镜像结构体必须是 standard-layout；`bool` → `int32_t`；`vec3` → 手动 padding 或改 `vec4`
7. 跨 program 共享 UBO 时**必须显式 std140**（默认 shared 布局驱动相关，不可移植）
8. `layout(binding=N)` 的 N 在同一 program 内对同类 block 必须唯一
9. 定长数组大小必须是编译期常量（const 表达式）

---

## 5.8 std140 与 std430：使用与比较

### 5.8.1 适用范围

| 布局限定 | uniform block (UBO) | buffer block (SSBO) | 最低版本 |
|----------|:---:|:---:|---------|
| `shared`（默认） | ✓ | ✓ | 3.1 / 4.3 |
| `packed` | ✓ | ✓ | 3.1 / 4.3 |
| `std140` | ✓ | ✓ | 3.1 / 4.3 |
| `std430` | **✗** | ✓ | 4.3 |

- **std430 只允许用于 SSBO**（及 Vulkan 的 push constant，GL 中无此概念）
- `shared`/`packed`：偏移由实现决定（packed 可能更紧凑），**无法在 C++ 端稳定预测偏移**，跨程序共享数据时禁用；实践中显式写 std140/std430
- UBO 没得选：只有 **std140** 一种可移植布局

### 5.8.2 对齐规则逐项对比

N = 标量大小（float/int = 4，double = 8）。

| 规则项 | std140 | std430 |
|--------|--------|--------|
| 标量 | 对齐 = 大小 | 同 std140 |
| vec2 | 8 | 8 |
| **vec3** | **16** | **16**（两种布局下都一样！） |
| vec4 | 16 | 16 |
| **数组元素步长** | **一律补齐到 16** | 元素自身的 base alignment |
| 矩阵列步长 | 每列占 16 槽位 | 列向量类型的 base alignment（mat4→16；mat3 因列是 vec3 仍为 16） |
| **结构体对齐** | max(成员对齐) **再取整到 16** | max(成员对齐)，不取整 |
| 结构体/数组总大小 | 补齐到对齐的倍数 | 同左 |

**只有两条规则不同**（数组步长、结构体取整），但它们恰恰是大数组场景下内存占用的决定因素：

```
float data[1000]:   std140 → 16000 字节   std430 → 4000 字节   (4×)
struct{f,f;} a[1000]: std140 → 16000 字节  std430 → 8000 字节   (2×)
vec4 data[1000]:    两种都是 16000 字节（vec4 天然 16 对齐）
```

### 5.8.3 同一块两种布局的完整算例

```glsl
struct Item { vec2 pos; vec2 vel; };   // std140: 对齐16, 大小16
                                        // std430: 对齐8,  大小16
// 同一成员序列，分别按两种布局标注 offset：

uniform/std140:                        // UBO 场景
    float time;        // off 0
    vec2  res;         // off 8
    vec3  dir;         // off 16   (vec3 恒 16)
    float k;           // off 28
    vec4  color;       // off 32
    mat4  mvp;         // off 48   (64 字节)
    mat3  rot;         // off 112  (48 字节, 列步长16)
    float w[4];        // off 160, 步长16, 64字节
    Item  items[8];    // off 224, 步长16, 128字节

buffer(std430):                        // SSBO 场景，同一序列
    float time;        // off 0
    vec2  res;         // off 8
    vec3  dir;         // off 16   (vec3 恒 16)
    float k;           // off 28
    vec4  color;       // off 32
    mat4  mvp;         // off 48   (64 字节)
    mat3  rot;         // off 112  (48 字节)
    float w[4];        // off 160, 步长4,  16字节
    Item  items[8];    // off 176, 步长16, 128字节
```

对应的真实声明：

```glsl
layout(std140, binding = 0) uniform Data {
    float time;  vec2 res;  vec3 dir;  float k;
    vec4 color;  mat4 mvp;  mat3 rot;
    float w[4];  Item items[8];
};

layout(std430, binding = 1) buffer DataSSBO {
    float time;  vec2 res;  vec3 dir;  float k;
    vec4 color;  mat4 mvp;  mat3 rot;
    float w[4];  Item items[8];
};
```

**逐项核对（std430 列）：**
- `w`：4 对齐 → 紧跟在 rot(112+48=160) 后；4×4=16 字节，结束于 176
- `items`：元素对齐 8，但结构体大小 16 → 步长 16，起始 176（对齐 OK，176 是 8 的倍数）
- 若 Item 只有 `{float a; float b;}`：std430 大小 8、步长 8；std140 大小 16、步长 16 —— 差距立现

### 5.8.4 C++ 端偏移获取：两条路线

**路线 A：手写镜像 + static_assert（推荐用于固定布局）**

```cpp
struct Data {           // 对应 std430
    float     time;
    glm::vec2 res;
    glm::vec3 dir;       // ⚠ std430 下 vec3 仍 16 对齐，需 padding
    float     _pad0;
    glm::vec4 color;
    glm::mat4 mvp;
    glm::vec4 rotCol0, rotCol1, rotCol2;   // mat3 的三列，各 16
    float     w[4];
    // items...
};
static_assert(offsetof(Data, color) == 32);
static_assert(offsetof(Data, mvp) == 48);
```

**路线 B：驱动查询偏移（UBO 专属，免疫布局计算错误）**

```cpp
GLuint prog = ...;
const GLchar* names[] = { "Data.mvp", "Data.w[0]", "Data.items[0]" };
GLuint idx[3];
glGetUniformIndices(prog, 3, names, idx);

GLint offset[3], arrStride[3], matStride[3];
glGetActiveUniformsiv(prog, 3, idx, GL_UNIFORM_OFFSET,      offset);
glGetActiveUniformsiv(prog, 3, idx, GL_UNIFORM_ARRAY_STRIDE, arrStride);
glGetActiveUniformsiv(prog, 3, idx, GL_UNIFORM_MATRIX_STRIDE, matStride);

// 之后可逐成员上传，无需关心对齐规则：
glNamedBufferSubData(ubo, offset[0], 64, glm::value_ptr(mvp));
```

注意：SSBO 成员没有 `glGetUniformIndices` 这套**旧式**查询，但可走统一接口 `GL_BUFFER_VARIABLE`（`GL_OFFSET`/`GL_ARRAY_STRIDE`/`GL_MATRIX_STRIDE` 等，完整示例见 5.6.2）。std430 偏移手算 + static_assert 仍是工程主流（简单、编译期验证），驱动查询适合做通用反射工具。

### 5.8.5 使用建议与典型组合

| 场景 | 布局选择 | 理由 |
|------|---------|------|
| UBO（相机矩阵、灯光参数） | std140（唯一选择） | 短小，浪费可忽略 |
| SSBO 大标量数组（索引、权重） | **std430** | std140 的 16 步长会让 `float[]` 膨胀 4 倍，浪费带宽与显存 |
| SSBO 结构体数组（粒子、实例数据） | **std430** | 成员排布更紧凑；结构体设计时避免 vec3 |
| 需要 GPU/CPU 端与 Vulkan 互通 | std140 / std430（与 Vulkan 一致） | 两个布局在 GL 与 Vulkan 中语义相同，便于共享镜像结构体 |
| 跨程序共享、多驱动部署 | 显式声明，禁用默认 shared | 默认布局不可移植 |

**选择口诀：**
- UBO → `std140`，没有选择余地
- SSBO → 默认 `std430`；只有当数据全是 vec4/mat4 时两者等价
- 无论哪种：**vec3 陷阱永远存在**（对齐 16、数组步长 16），结构体设计阶段就规避

**版本兼容：** std140 需要 GL 3.1+/GLSL 1.40（binding 限定符需 4.20+）；std430 需要 GL 4.3+/GLSL 430。由于 SSBO 本身就是 4.3 引入的，**只要能用 SSBO 就能顺手用 std430**，无需额外顾虑版本。

---

# 6. 内置变量

## 6.1 全阶段通用的保留名

```glsl
gl_Position, gl_FragCoord, gl_VertexID ...   // gl_ 前缀保留
```
- 用户变量不能以 `gl_` 开头

## 6.2 Vertex Shader

| 变量 | 类型 | 读写 | 含义 |
|------|------|------|------|
| `gl_Position` | vec4 | 写（必须） | 裁剪空间位置。**VS 必须写**（否则结果未定义） |
| `gl_PointSize` | float | 写 | 点大小（像素）。需 `GL_PROGRAM_POINT_SIZE` |
| `gl_ClipDistance[]` | float[] | 写 | 用户裁剪平面距离；负值被裁掉 |
| `gl_CullDistance[]` | float[] | 写 | 用户剔除距离（ARB_cull_distance） |
| `gl_VertexID` | int | 读 | 当前顶点索引（不含 baseVertex） |
| `gl_InstanceID` | int | 读 | 当前实例编号（不含 baseInstance） |
| `gl_VertexIndex` | int | 读 | = gl_VertexID + gl_BaseVertex（Vulkan/SPIR-V 风格名称，GL 经 ARB_gl_spirv 可用） |
| `gl_InstanceIndex` | int | 读 | = gl_InstanceID + gl_BaseInstance |
| `gl_BaseVertex` / `gl_BaseInstance` | int | 读 | draw call 的 base 参数（4.6 / ARB_shader_draw_parameters） |
| `gl_DrawID` | int | 读 | 多绘制调用（`glMultiDraw*`）中的当前 draw 序号（ARB_shader_draw_parameters） |

```glsl
#version 460 core
void main() {
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
    gl_PointSize = 4.0;
}
```

**典型用法：**
- `gl_VertexID` 全屏三角形：`vec2 p = vec2[3](...)[gl_VertexID]`，无需 VBO
- `gl_InstanceID` 实例化植被/草地
- `gl_ClipDistance` 反射水面裁剪

## 6.3 Tessellation Control Shader

| 变量 | 类型 | 读写 | 含义 |
|------|------|------|------|
| `gl_InvocationID` | int | 读 | 当前 invocation（控制点）编号：0~N-1 |
| `gl_TessLevelOuter[4]` | float[] | 写 | 外边缘细分等级 |
| `gl_TessLevelInner[2]` | float[] | 写 | 内部细分等级 |
| `gl_PatchVerticesIn` | int | 读 | 输入 patch 的顶点数 |
| `gl_PrimitiveID` | int | 读 | patch 编号 |
| `gl_in[]` | 数组 | 读 | 输入控制点（含 gl_Position 等） |

```glsl
#version 460 core
layout(vertices = 3) out;

void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0) {                    // 只写一次
        gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = gl_TessLevelOuter[2] = 5.0;
        gl_TessLevelInner[0] = 5.0;
    }
}
```

## 6.4 Tessellation Evaluation Shader

| 变量 | 类型 | 读写 | 含义 |
|------|------|------|------|
| `gl_TessCoord` | vec3 | 读 | 当前评估点在 patch 域内的重心/参数坐标 |
| `gl_PatchVerticesIn` | int | 读 | 控制点数量 |
| `gl_PrimitiveID` | int | 读 | patch 编号 |
| `gl_TessLevelOuter/Inner` | float[] | 读 | 细分等级（可读） |
| `gl_Position` | vec4 | 写 | 评估顶点的裁剪坐标（**必须写**） |

```glsl
layout(triangles, equal_spacing, ccw) in;

void main() {
    vec3 bc = gl_TessCoord;                       // barycentric
    vec4 p = bc.x*gl_in[0].gl_Position
           + bc.y*gl_in[1].gl_Position
           + bc.z*gl_in[2].gl_Position;
    gl_Position = p;                              // 直接作为细分点
}
```

## 6.5 Geometry Shader

| 变量 | 类型 | 读写 | 含义 |
|------|------|------|------|
| `gl_in[]` | 数组 | 读 | 输入顶点（长度=图元顶点数） |
| `gl_PrimitiveIDIn` | int | 读 | 输入图元编号（注意带 In 后缀） |
| `gl_PrimitiveID` | int | 写 | 输出图元编号（供 FS 读取） |
| `gl_Position` | vec4 | 读写 | 当前正在构建的顶点位置 |
| `gl_PointSize` | float | 读写 | 点大小 |
| `gl_Layer` | int | 读写 | 渲染到 cubemap/纹理数组的哪一层 |
| `gl_ViewportIndex` | int | 读写 | 多视口（GL_viewport_array）选哪个视口 |
| `gl_InvocationID` | int | 读 | GS 实例化（`layout(invocations=N)`）中的编号 |

**GS 专用函数（非变量但必须掌握）：**

```glsl
EmitVertex();      // 结束当前顶点，开始下一个（gl_Position等被消费）
EndPrimitive();    // 结束当前图元
```

**典型用法：** 单 pass 立方体贴图阴影（`gl_Layer` = 6 个面）、单 pass 点光源阴影数组、 billboard 展开。

## 6.6 Fragment Shader

| 变量 | 类型 | 读写 | 含义 |
|------|------|------|------|
| `gl_FragCoord` | vec4 | 读 | 窗口坐标。xy=像素位置（左下原点，中心在半像素 0.5）；z=深度值；w=1/w_clip |
| `gl_FrontFacing` | bool | 读 | 当前片元是否正面朝向相机 |
| `gl_PointCoord` | vec2 | 读 | 点精灵内坐标 [0,1]²（左上或左下取决于设置） |
| `gl_FragDepth` | float | 写（out） | 显式写深度。**写它会使 early-z 失效**（可用 layout(depth_any/less/greater) 缓解） |
| `gl_PrimitiveID` | int | 读 | 图元编号（来自 GS 或自动生成） |
| `gl_SampleID` | int | 读 | 当前 sample 编号（MSAA，ARB_sample_shading） |
| `gl_SamplePosition` | vec2 | 读 | sample 在像素内的位置 |
| `gl_SampleMaskIn[]` | bool[] | 读 | 覆盖的 sample 掩码 |
| `gl_SampleMask[]` | int[] | 写 | 输出 sample 掩码（控制写哪些 sample） |
| `gl_Layer` / `gl_ViewportIndex` | int | 读 | GS 传入的层/视口 |
| `gl_FragColor` | vec4 | （已废弃） | 兼容模式输出；核心模式须自声明 out |
| `gl_FragData[n]` | vec4[] | （已废弃） | 旧版 MRT |
| `gl_HelperInvocation` | bool | 读 | 是否为辅助线程（helper invocation，quad内因导数而衍生的线程） |

```glsl
#version 460 core
layout(location = 0) out vec4 fragColor;

void main() {
    if (!gl_FrontFacing) {
        // 背面：翻转法线
    }
    vec2 pixel = floor(gl_FragCoord.xy);
    float dither = fract(sin(dot(pixel, vec2(12.9898,78.233))) * 43758.5453);
    // gl_FragDepth = ...;  // 尽量避免
    fragColor = vec4(...);
}
```

**`gl_HelperInvocation` 用途：** 写 image/SSBO 时跳过 helper 线程，避免污染数据：

```glsl
if (!gl_HelperInvocation) imageStore(counterImg, ivec2(gl_FragCoord.xy), val);
```

## 6.7 Compute Shader

### Work group 与 invocation 坐标系

```
gl_NumWorkGroups        = dispatch的组数 (X,Y,Z)          ← glDispatchCompute(nx,ny,nz)
gl_WorkGroupSize        = 组内线程数 (local_size_x..z)     ← layout限定符
gl_WorkGroupID          = 当前组编号 (X,Y,Z)               0 .. gl_NumWorkGroups-1
gl_LocalInvocationID    = 组内线程编号 (X,Y,Z)              0 .. gl_WorkGroupSize-1
gl_GlobalInvocationID   = 全局线程编号 = WorkGroupID*WorkGroupSize + LocalInvocationID
gl_LocalInvocationIndex = 组内一维编号 = LocalInvocationID.z*Wx*Wy + y*Wx + x
```

| 变量 | 类型 | 含义 |
|------|------|------|
| `gl_WorkGroupSize` | uvec3（const） | 组大小，编译期常量 |
| `gl_NumWorkGroups` | uvec3 | dispatch 的组数 |
| `gl_WorkGroupID` | uvec3 | 当前组号 |
| `gl_LocalInvocationID` | uvec3 | 组内 3D 线程号 |
| `gl_GlobalInvocationID` | uvec3 | 全局 3D 线程号（**最常用**） |
| `gl_LocalInvocationIndex` | uint | 组内 1D 线程号（用于 shared 数组索引） |

```glsl
#version 460 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform image2D img;

void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    if (pix.x >= imageSize(img).x || pix.y >= imageSize(img).y) return;
    imageStore(img, pix, computeColor(pix));
}
```

```cpp
glDispatchCompute((w+7)/8, (h+7)/8, 1);
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
```

## 6.8 已废弃（兼容模式）内置变量

| 旧变量 | 替代 |
|--------|------|
| `gl_ModelViewProjectionMatrix` 等 | 自定义 uniform 矩阵 |
| `gl_Vertex` / `gl_Normal` / `gl_MultiTexCoord0` | 自定义 attribute in |
| `gl_FragColor` | `layout(location=0) out vec4` |
| `gl_FragData[n]` | 多个 out 变量（MRT） |
| `gl_TexCoord[n]` | 自定义 out/in |
| `gl_FogFragCoord` | 自定义 |

---

# 7. Shader 编译与 Program 生命周期

## 7.1 核心概念辨析

| 概念 | 是什么 | GL 侧对象 |
|------|--------|----------|
| **shader object**（着色器对象） | **单个 stage** 的源代码编译单元（.vs/.fs 源码 → 编译成中间代码） | `GLuint`（非 0） |
| **program object**（程序对象） | 各 stage 编译产物的**链接**结果——解析跨 stage 的 in/out 匹配、分配 location、计算 block 布局，形成可执行的整体 | `GLuint`（非 0） |
| **program pipeline**（程序管线对象） | 把多个**separable program** 按stage 组合起来共用的容器（GL 4.1+） | `GLuint` |

类比 C 语言：**shader = .c 编译成 .o，program = 链接器把多个 .o 链成可执行文件，pipeline = 动态链接/插件组合**。

关键认知：
- `glCompileShader` 只做**单 stage 语法/语义检查**，跨 stage 的接口匹配（VS out ↔ FS in）在 `glLinkProgram` 才检查
- program 是状态容器：uniform 默认值、block binding、subroutine 选择都存在 program 里（不是全局的）
- shader 链接完成后即可删除（见 7.5）

## 7.2 完整流水线：创建 → 编译 → 链接 → 查询 → 使用 → 销毁

```
┌─ shader 对象 ──────────────────────────┐
│ glCreateShader(stage)                  │
│ glShaderSource / glShaderSource...     │
│ glCompileShader                        │
│ glGetShaderiv(GL_COMPILE_STATUS)       │ ── 失败 → glGetShaderInfoLog
└───────────────┬────────────────────────┘
                │ glAttachShader（可挂多个，每 stage 至多一个）
┌─ program 对象 ┴────────────────────────┐
│ glCreateProgram                        │
│ [pre-link 设置：glBindAttribLocation / │
│  glTransformFeedbackVaryings /         │
│  glProgramParameteri]                  │
│ glLinkProgram                          │
│ glGetProgramiv(GL_LINK_STATUS)         │ ── 失败 → glGetProgramInfoLog
│ [链接后查询：location / block / 接口]  │
└───────────────┬────────────────────────┘
                │ glUseProgram / glBindProgramPipeline
          ┌─────┴─────┐
          │ 渲染循环中 │  glUniform* / glProgramUniform*
          └─────┬─────┘
                │ glDeleteShader / glDeleteProgram（+ pipeline）
```

### 阶段一：创建与编译 shader

```cpp
// 1. 创建（指定 stage 类型）
GLuint vs = glCreateShader(GL_VERTEX_SHADER);      // GL_VERTEX_SHADER / GL_FRAGMENT_SHADER /
                                                   // GL_GEOMETRY_SHADER / GL_TESS_CONTROL_SHADER /
                                                   // GL_TESS_EVALUATION_SHADER / GL_COMPUTE_SHADER
// 2. 填源码（可多段拼接，末尾自动补 '\0'）
const GLchar* src = "#version 450 core\n ...";
glShaderSource(vs, 1, &src, nullptr);              // (shader, count, strings, lengths)

// 3. 编译
glCompileShader(vs);

// 4. 查询编译结果
GLint ok = 0;
glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);         // GL_TRUE / GL_FALSE
if (!ok) {
    GLint len = 0;
    glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &len);   // 含 '\0' 的日志长度
    std::string log(len, '\0');
    glGetShaderInfoLog(vs, len, nullptr, log.data());
    // 输出：行号 + 错误描述（驱动格式各异：NVIDIA "0(12) : error ..."）
}

// 其他可查询：GL_SHADER_TYPE / GL_SHADER_SOURCE_LENGTH / GL_DELETE_STATUS
// glGetShaderSource(vs, ...) 可读回源码
```

### 阶段二：创建并链接 program

```cpp
GLuint prog = glCreateProgram();                   // 返回 0 表示失败

glAttachShader(prog, vs);                          // 可挂任意多个，但每个 stage 至多一个
glAttachShader(prog, fs);                          // 也可以只挂一个 compute shader
// glDetachShader(prog, vs);                       // 可选：解除挂载

// ---- pre-link 设置（必须在 glLinkProgram 之前！）----
// ① 固定顶点属性 location（替代 shader 内 layout(location=N)）
glBindAttribLocation(prog, 0, "aPos");             // 位置 0 绑到属性名
// ② 指定 TF 捕获的 varying
const GLchar* tfVaryings[] = { "vWorldPos" };
glTransformFeedbackVaryings(prog, 1, tfVaryings,
                            GL_INTERLEAVED_ATTRIBS);   // 或 SEPARATE_ATTRIBS
// ③ 链接行为开关（GL 4.1+）
glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, GL_TRUE);   // 允许进 pipeline
glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

glLinkProgram(prog);

// 查询链接结果
GLint linked = 0;
glGetProgramiv(prog, GL_LINK_STATUS, &linked);     // GL_TRUE / GL_FALSE
if (!linked) {
    GLint len = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
    std::string log(len, '\0');
    glGetProgramInfoLog(prog, len, nullptr, log.data());
    // 典型错误：VS 输出与 FS 输入的类型/名字不匹配、缺 stage、超出 uniform 上限
}
```

### 阶段三：链接后查询（全部详见 5.6 / 5.6.7）

```cpp
GLint n = 0;
glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);          // 活跃 uniform 数
glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &n);        // 顶点属性数
glGetProgramiv(prog, GL_ACTIVE_UNIFORM_BLOCKS, &n);    // uniform block 数
// SSBO / atomic counter / subroutine 只能走统一接口：
glGetProgramInterfaceiv(prog, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &n);

// 便捷查询宏（GL 4.3+，一步拿"该接口资源总数"）
// glGetProgramInterfaceiv(prog, GL_UNIFORM / GL_PROGRAM_INPUT / GL_PROGRAM_OUTPUT / ..., ...)

// 完整性校验（调试用：检查当前 GL 状态下 program 能否运行）
glValidateProgram(prog);
glGetProgramiv(prog, GL_VALIDATE_STATUS, &ok);         // 附带当前状态渲染是否合法
```

### 阶段四：使用

```cpp
glUseProgram(prog);                                // 设为当前（0 = 固定管线/无 program）
// ---- 之后的 GL 状态 ----
//   glUniform*        → 写入 prog 的 uniform（要求 prog 是 current！）
//   glDraw*           → 用 prog 的 VS/FS... 执行
//   glBindBufferBase  → 绑定的是全局 binding point，与哪个 program 无关
```

```cpp
// DSA 替代：glProgramUniform* 直接对任意 program 设值，无需 use（GL 4.1+）
glProgramUniform1f(prog, locTime, 1.5f);           // 渲染循环外预填别的 program 也不打断当前状态
glProgramUniformMatrix4fv(prog, locMvp, 1, GL_FALSE, glm::value_ptr(mvp));
```

### 阶段五：销毁

```cpp
glDeleteShader(vs);                                // 若仍 attached：仅标记删除，
glDeleteShader(fs);                                //   等 detach 后真正释放。所以
                                                   //   link 成功后立即 delete 是安全且推荐的
glDeleteProgram(prog);                             // 若是 current program：标记，等
                                                   //   glUseProgram(0) 或换 program 后释放
glUseProgram(0);                                   // 好习惯：退出前解除绑定

// pipeline 相关（若使用）
glDeleteProgramPipelines(1, &pipeline);
// 判活：glIsShader / glIsProgram / glIsProgramPipeline
```

## 7.3 一步到位：glCreateShaderProgramv（便捷函数）

```cpp
// = create + source + compile + link + 自动 delete shader，返回 program
GLuint prog = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &src);
// 内部过程：
//   sh = glCreateShader(stage); glShaderSource; glCompileShader;
//   prog = glCreateProgram(); glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, GL_TRUE);
//   glAttachShader(prog, sh); glLinkProgram(prog); glDetachShader(prog, sh); glDeleteShader(sh);
// ⚠ 出错时返回 0；info log 要用 glGetProgramInfoLog 查（不是 glGetShaderInfoLog！）
// ⚠ 生成的 program 自动是 separable 的（为 pipeline 而生）
```

适合工具链/简单场景；正式引擎通常还是走完整流程（便于缓存 shader 对象、复用编译结果）。

## 7.4 Separable Program 与 Program Pipeline（GL 4.1+）

**痛点**：传统 program 是"全套打包"——想换 FS 就得链接一个新 program，各 program 的公共 VS 部分重复且状态切换贵。

**方案**：separable program（每 stage 一个独立小 program）+ pipeline 容器按 stage 组合：

```cpp
// ① 逐 stage 创建 separable program
GLuint progVS = glCreateShaderProgramv(GL_VERTEX_SHADER,   1, &vsSrc);
GLuint progFS = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fsSrc);
// 或手动流程 + glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, GL_TRUE) 再 link

// ② 创建 pipeline，按 stage 挂 program
GLuint pipeline;
glGenProgramPipelines(1, &pipeline);
glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT,   progVS);
glUseProgramStages(pipeline, GL_FRAGMENT_SHADER_BIT, progFS);
// 可用位或组合：glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT | GL_GEOMETRY_SHADER_BIT, progA);
// ALL_SHADER_BITS = 全部 stage

// ③ 使用（替代 glUseProgram）
glBindProgramPipeline(pipeline);                   // 0 = 解绑
// ⚠ pipeline 与 glUseProgram 互斥：
//   glBindProgramPipeline(!=0) 时，当前 program 的对应 stage 被 pipeline 覆盖

// ④ 设值必须用 DSA 版（没有"current program"概念了）：
glProgramUniform1f(progFS, locTime, t);            // 对 separable program 设值

// ⑤ 校验/查询
glValidateProgramPipeline(pipeline);
glGetProgramPipelineiv(pipeline, GL_VALIDATE_STATUS, &ok);
glGetProgramPipelineiv(pipeline, GL_VERTEX_SHADER, &curVS);   // 查某 stage 挂的 program
```

**separable program 的硬性要求**：单独链接时**跨 stage 接口必须显式匹配**——因为链接器看不到隔壁 stage，`layout(location=N)` 必须两端写死（或用 glBindAttribLocation/glLocation 预分配），不能靠链接器自动配对。

## 7.5 各 stage 总览

| stage 枚举 | pipeline 位 | 作用 | 必需？ | 版本 |
|-----------|------------|------|:---:|------|
| `GL_VERTEX_SHADER` | `GL_VERTEX_SHADER_BIT` | 顶点变换，输出 clip space | **渲染必需** | 2.0 |
| `GL_TESS_CONTROL_SHADER` | `..._TESSELLATION_CONTROL..._BIT`* | 细分控制：定细分等级 | 可选 | 4.0 |
| `GL_TESS_EVALUATION_SHADER` | `..._TESSELLATION_EVALUATION..._BIT`* | 细分计算：算细分后顶点 | 与 TCS 成对 | 4.0 |
| `GL_GEOMETRY_SHADER` | `GL_GEOMETRY_SHADER_BIT` | 图元级处理：增删改图元 | 可选 | 3.2 |
| `GL_FRAGMENT_SHADER` | `GL_FRAGMENT_SHADER_BIT` | 片元着色，输出颜色 | **渲染必需** | 2.0 |
| `GL_COMPUTE_SHADER` | `GL_COMPUTE_SHADER_BIT` | 通用计算（无光栅化） | **独立调度** | 4.3 |

*准确位名：`GL_TESSELLATION_CONTROL_SHADER_BIT` / `GL_TESSELLATION_EVALUATION_SHADER_BIT`。

规则：
- 传统 program：**每 stage 至多挂一个** shader；渲染类 program 必须有 VS+FS（GS/TCS/TES 可选）；compute program 只能且必须只有一个 CS
- compute 的调度不走 `glDraw*`，走 `glDispatchCompute(gx, gy, gz)`（配 `glMemoryBarrier` 同步，见 4.5）
- TCS 与 TES 必须成对出现（只有 TES 而无 TCS 时可用 `GL_PATCH_DEFAULT_OUTER_LEVEL` 等常量替代）

## 7.6 Program 间共享的量及方式

| 量 | 能否跨 program 共享 | 方式 | 说明 |
|----|:---:|------|------|
| **shader 编译产物** | ✓ | 同一 shader 对象 attach 到多个 program | 编译一次，多处链接（最常用的"源码级"复用） |
| **UBO 数据** | ✓ | `glBindBufferBase(GL_UNIFORM_BUFFER, N, ubo)` + 各 program 的 block 都指到 binding N（shader 内 `layout(binding=N)` 统一） | **数据**全局共享；但每个 program 的 block binding 是各自状态 |
| **SSBO 数据** | ✓ | 同上，`GL_SHADER_STORAGE_BUFFER` | 同上 |
| **纹理** | ✓ | 纹理绑到 texture unit（全局），各 program 的 sampler uniform 值指向同一 unit | `glUniform1i(loc, unit)` / `glBindTextureUnit(unit, tex)` |
| **image** | ✓ | `GL_IMAGE_UNITS` 同 texture unit 机制 | `glBindImageTexture` |
| **atomic counter buffer** | ✓ | `GL_ATOMIC_COUNTER_BUFFER` binding point | 各 program 的 counter offset 各自声明 |
| **顶点属性/VAO、FBO 等上下文状态** | ✓（与 program 无关） | 全局 context 对象 | 本就不属于 program |
| **默认块 uniform 的值** | ✗ | —（改用 UBO 才共享） | 值存在各 program 内，`glUniform*` 只写当前 program |
| **subroutine 选择** | ✗ | — | program（且分 stage）私有状态 |
| **跨 stage 接口匹配信息** | 仅 pipeline 内 | separable program + 显式 location | pipeline 保证了挂进来的各 stage program 接口兼容 |

**block index / binding point / buffer 三层结构（"各自状态"的含义）：**

```
program A                          全局 context                     显存
┌─────────────────┐
│ block "Camera"  │──block index 0 ──┐
│  (A 内部的编号)  │                  │
└─────────────────┘                  ▼
                            binding point 0  ◄── glBindBufferBase ── UBO(camera)
┌─────────────────┐                  ▲
│ block "Light"   │──block index 1 ──┘ (假设也指到 0，则读到同一 UBO)
└─────────────────┘
```

| 概念 | 归属 | 是什么 |
|------|------|--------|
| **block index** | 每个 program 私有 | block 在**这个 program 内部**的编号（`glGetUniformBlockIndex` 返回值），由声明顺序/驱动决定，跨 program 无意义。A 的 Camera 是 0，B 的 Camera 可能是 2 |
| **binding point**（绑定槽） | **全局**（context 级） | `GL_UNIFORM_BUFFER` 的第 N 号插槽，所有 program 共用的"插座" |
| **buffer ↔ 插座 的连接** | 全局 | `glBindBufferBase(GL_UNIFORM_BUFFER, N, ubo)`——绑一次全 program 生效，这就是"数据全局共享" |
| **block ↔ 插座 的连接** | **每个 program 私有** | "`Camera` 这个 block 从几号插座取数据"这条**映射关系**存在 program 里 |

"block↔插座"映射每个 program 各存一份，两种设置方式：

```cpp
// 方式①：GLSL 里写死（GL 4.2+，推荐）——写在每个 program 自己的源码里
layout(std140, binding = 0) uniform Camera { ... };

// 方式②：C++ 逐 program 设置（运行期状态，存在 program 对象里）
GLuint idxA = glGetUniformBlockIndex(progA, "Camera");   // A 里可能是 0
GLuint idxB = glGetUniformBlockIndex(progB, "Camera");   // B 里可能是 2
glUniformBlockBinding(progA, idxA, 0);                    // A 的 Camera → 插座 0
glUniformBlockBinding(progB, idxB, 0);                    // B 的 Camera → 插座 0（要单独设！）
```

> ⚠ **典型坑**：给 A 设了 binding，以为 B 也生效——不行，B 的映射是 B 自己的状态；没设的 block **默认全指到 binding 0**（这也是为什么"只有一个 UBO 且恰好绑在 0"时侥幸能跑，多 UBO 后立刻错乱）。
>
> 一句话：**index 是 program 内部门牌号，binding point 是全局插座号，"门牌号→插座号"的接线表每家（program）自己一份。**


其实就是buffer object一类的数据，不同类型的buffer object(UBO TBO SSBO等)实际是在GPU上各自分配了一大块global的显存，
每种类型的这一块显存可以看做是一个指针数组，通过**binding point也就是数组的index**可以访问对应的显存，glBindBufferBase就可以更新指定类型 指定buffer对象 指定binding point的那部分内存

上面只是分配了内存池并填充了内存的值，在program里想要访问这块内存，这时候就需要把program内部的uniform block/shader storage block变量绑定到这块显存
而这些变量在program内是通过blockindex代替的，所以有glUniformBlockBinding/glShaderStorageBlockBinding接口来完成绑定


**共享 UBO 的标准姿势（相机矩阵等全局数据）：**

```glsl
// —— 所有 shader 源文件里写同一份声明（binding 固定）——
layout(std140, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    vec4 pos;
} cam;
```

```cpp
GLuint cameraUBO;
glNamedBufferStorage(cameraUBO, sizeof(CameraData), nullptr, GL_DYNAMIC_STORAGE_BIT);
glBindBufferBase(GL_UNIFORM_BUFFER, 0, cameraUBO);   // 绑一次，全局生效
// 此后所有 program（只要 block 指到 binding 0）自动读到同一份相机数据
// 每帧只更新一次：
glNamedBufferSubData(cameraUBO, 0, sizeof(CameraData), &data);
```

**shader 对象复用：**

```cpp
GLuint commonVS = compile("common.vs");              // 编译一次
GLuint p1 = link({commonVS, f1});                    // 两处 attach
GLuint p2 = link({commonVS, f2});                    // 同一 vs 参与两次链接
// link 完即可 glDeleteShader(commonVS)（标记删除，两个 program 仍持有引用）
```

**纹理 unit 共享：**

```cpp
glBindTextureUnit(0, albedoTex);                     // 纹理 → unit 0（全局、一次）
// program A、B 的 sampler uniform 都设成 0 → 都采样同一张纹理
glProgramUniform1i(progA, locA, 0);
glProgramUniform1i(progB, locB, 0);
```

## 7.7 常见坑汇总

1. **只编译不链接 / 链接失败不看日志**——黑屏双雄；`glLinkProgram` 之后必须查 `GL_LINK_STATUS`
2. **pre-link 设置放在了 link 之后**——`glBindAttribLocation`、TF varyings、`GL_PROGRAM_SEPARABLE` 全部静默失效（不报错！），必须重来一次链接
3. **link 成功后不敢删 shader**——其实立即可删（引用计数保护），拖着反而泄漏
4. **separable program 没写死 location**——单 stage 链接看不到邻居，in/out 没显式 location 时行为未定义（通常全 0 或错乱）
5. **`glUniform*` 打到非 current program**——`GL_INVALID_OPERATION`，静默不生效；多 program 管理改用 `glProgramUniform*`
6. **pipeline 与 glUseProgram 混用不自知**——pipeline 绑定时覆盖 program 的对应 stage，调试时先理清"这个 draw 到底谁在生效"
7. **改了 shader 源码重新 glShaderSource 后忘记重新 glCompileShader / 重新 glLinkProgram**
8. **driver 的 info log 格式不同**——NVIDIA `0(12) : error`、AMD/ANGLE 另有格式；解析行号要按驱动家族适配

---

# 附录A：常见错误与排查

| 症状 | 原因 |
|------|------|
| 链接错误 "interpolation qualifier mismatch" | 前后阶段 in/out 的 flat/smooth 不一致 |
| 链接错误 "type mismatch" | 接口变量类型/名字不一致，或 block 定义不一致 |
| FS 读到的值全 0 | 接口变量在 VS 被优化掉（未使用）；或未被写 |
| 整数接口变量链接报错 | 忘了 `flat` |
| UBO 数据错位 | std140 对齐算错（vec3 后接 vec3、mat3、结构体嵌套） |
| C++ 镜像结构体 sizeof/offsetof 对不上 | glm::vec3 对齐 4 vs 布局要求的 16；bool 1 字节 vs GLSL 4 字节；忘记手动 padding |
| glGetUniformLocation 查不到 block 成员 | 名字必须是 `块名.成员名`，实例名不参与查询名 |
| SSBO 成员 offset 算错且无法验证 | SSBO 无偏移查询 API，用 glslangValidator 编译验证或改用 UBO 查询法 |
| 链接错误 "block mismatch" | 同名 block 在两个 shader 中声明不一致（成员/顺序/布局） |
| image 读出全 0 | 格式不匹配（shader rgba32f vs 纹理 rgba8） |
| compute 写的数据读不到 | 少了 `glMemoryBarrier(...)` |
| 数据随机错乱 | 竞争条件，缺 `barrier()`/原子操作/coherent |
| GS 无输出 | 忘了 EmitVertex 或 max_vertices 太小 |
| 黑屏 + 无报错 | gl_Position 未写；或深度测试问题；或 out location 未绑 |
| gl_VertexID 不对 | 混淆 gl_VertexID（不含 base）与 gl_VertexIndex |
| 局部变量未初始化导致随机结果 | GLSL 未初始化值未定义 |

**调试工具：**
- `glGetProgramInfoLog` / `glGetShaderInfoLog`（链接/编译错误）
- `glValidateProgram`（状态检查）
- glslangValidator / RenderDoc / Nsight Graphics / apitrace
- `#pragma optimize(off)` `#pragma debug(on)`

---

# 附录B：速查表

## B.1 限定符总结

```
完整声明语法：
[layout(...)] [invariant] [precise] [flat|smooth|noperspective]
[coherent|volatile|restrict|readonly|writeonly] [const] [in|out|uniform|buffer|shared|patch]
type name [= value];
```

## B.2 一句话选择指南

- **逐顶点数据** → attribute `in`
- **全局小常量** → uniform
- **多程序共享常量块** → uniform block + UBO（std140）
- **GPU 读写大数组** → buffer block + SSBO（std430）
- **贴图采样** → sampler + texture()
- **随机读写像素** → image + imageLoad/Store
- **计数** → atomic_uint
- **组内暂存** → shared
- **跨阶段插值数据** → out/in（或 in/out block）

## B.3 版本速查

| 特性 | 最低版本 |
|------|---------|
| 核心模式 in/out 替代 attribute/varying | 130 |
| 显式 fragment out location | 330 |
| UBO + std140 | 140（核心 130+ARB） |
| GS / `gl_PerVertex` 重声明 | 150 |
| sampler 数组 binding、textureSize 等 | 420 |
| compute shader、SSBO、image、atomic | 430 |
| block location 限定（跨阶段） | 410 |
| `gl_HelperInvocation` | 440 |
| explicit uniform location | 430 (ARB 4.20+) |
| SPIR-V 输出、glslang | 450+ / KHR |
| `gl_BaseVertex`/`gl_DrawID` | 460 (ARB_shader_draw_parameters) |

---

> 参考来源：
> - GLSL 4.60 规范：https://registry.khronos.org/OpenGL/specs/gl/GLSL/glslangspec.4.60.pdf
> - OpenGL 4.6 规范（内存模型、image 格式表）：https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf
> - Khronos Wiki：https://www.khronos.org/opengl/wiki/
> - LearnOpenGL（实践示例）：https://learnopengl.com/





