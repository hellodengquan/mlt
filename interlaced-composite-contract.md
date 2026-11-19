# 隔行视频合成转场合约：场序、几何与透明通道分析

> 分析对象：MLT 7.x 隔行（interlaced）视频参与合成转场时的场序属性、图像获取链路与 Alpha 叠加。
> 主要证据文件：
> - `src/modules/core/transition_composite.c`（场感知合成，YUV422，已标记 *DEPRECATED*）
> - `src/modules/core/transition_luma.c`（同构的场循环 luma 转场）
> - `src/modules/core/filter_fieldorder.c`（场序归一化）
> - `src/modules/core/filter_rescale.c`（整帧重采样缩放）
> - `src/modules/core/filter_resize.c`（整帧补边/填充，不做重采样）
> - `src/modules/plus/transition_affine.c`、`src/modules/qt/transition_qtblend.cpp`（非场感知的现代合成器）
> - `src/modules/avformat/filter_avdeinterlace.c`、`link_avdeinterlace.c`（去隔行）
> - `src/framework/mlt_frame.c`、`mlt_consumer.c`、`mlt_transition.c`（属性传递与帧获取）
> - `src/modules/core/loader.ini`（规范化滤镜链顺序）

---

## 1. 管线中的场序属性与图像获取

### 1.1 三个层级的场属性

| 属性 | 所在对象 | 语义 |
| --- | --- | --- |
| `progressive` | 帧（`mlt_frame` 属性，由生产者写入） | 该帧本身是否为逐行扫描；`0` 表示隔行帧，上下两场采样于不同时刻 |
| `top_field_first` (TFF) | 帧 | 隔行帧的场主导顺序：`1`=上场优先，`0`=下场优先 |
| `consumer.progressive` / `consumer.top_field_first` | 帧（由 consumer 下发，`mlt_consumer.c:697-708`） | 输出端要求：是否必须去隔行、要求的归一化场序 |

生产者侧（`producer_avformat.c:3148-3189, 3294-3299`）从 `AV_FRAME_FLAG_INTERLACED` / `AV_FRAME_FLAG_TOP_FIELD_FIRST` 与解码器 `field_order` 探测这两个值，并同时写入帧属性与 `meta.media.*`；可用 `force_progressive` / `force_tff` 人工覆盖。帧间传递时 `copy_image_state()` 通过 `mlt_properties_pass_list` 原样保留它们（`mlt_frame.c:1154-1157`，白名单含 `progressive,top_field_first`）。

`loader.ini` 定义的规范化滤镜链顺序为：

```
image_convert → color → deinterlace → fieldorder → crop → rescaler(swscale/rescale) → resizer(resize)
```

即**去隔行与场序归一化发生在任何几何缩放之前**。`fieldorder` 滤镜（`filter_fieldorder.c`）按 `consumer.top_field_first` 把所有帧归一化到同一出场顺序，必要时整体下移一行（`shifting_fields`），并处理 `meta.swap_fields`。

### 1.2 合成转场的图像获取

`transition_composite.c` 的 `transition_get_image()` 依次获取：

1. A 帧（背景轨）：`mlt_frame_get_image(a_frame, ...)`，强制 `mlt_image_yuv422`（第 984 行）；
2. B 帧（叠加轨）：经 `get_b_frame_image()` 按几何矩形 `geometry->sw/sh` 请求缩放后的 YUV422 图像与独立 alpha 缓冲 `mlt_frame_get_alpha(b_frame)`；
3. 然后按场循环调用 `composite_yuv()` 把 B 逐行混合到 A。

该合成器**只支持 YUV422**，宽度被强制取偶（`get_b_frame_image` 第 827 行 `*width -= *width % 2`），因为色度子采样要求宏块对齐。

---

## 2. 场序结论（条件化）

### 2.1 场渲染的启用条件

`transition_composite.c:1073-1106`：

```c
int progressive = mlt_properties_get_int(a_props, "consumer.progressive")
                  || mlt_properties_get_int(properties, "progressive");
...
for (field = 0; field < (progressive ? 1 : 2); field++) {
    int field_id = progressive ? -1 : (top_field_first ? (1 - field) : field);
```

**条件：**

- 当 A 帧 `consumer.progressive=1` **或** 转场属性 `progressive=1`（构造默认即为 1，第 1226 行）时，循环只执行一次，`field_id = -1`，`composite_yuv` 内 `step = 1`，按**整帧**处理全部行；
- 两者皆为 0（隔行输出路径）时，循环执行两次：`step = 2`，每次只写一个场的行。
- 转场的 `deinterlace=1` 属性会对 A、B 两帧同时设置 `consumer.progressive=1`（第 1017-1020 行），等价于在合成前强制去隔行。

### 2.2 上下场与物理行的映射

`composite_yuv()` 第 423-442 行的注释与代码明确了**目标侧**的场约定：

- `field 0 = lower field（下场）`，目标行号 y（0 基）应为**奇数**；
- `field 1 = upper field（上场）`，目标行号 y 应为**偶数**；
- 当 `y % 2 == field` 落在错误奇偶性上时，目标指针上下移动一个 `stride_dest` 做一行对齐修正，保证每次循环只写目标缓冲中同一场的行集合；
- 第二场（`field == 1`）B 帧**源**指针额外加一个源 stride（并令 `height_src--`），使两场从 B 缓冲的相邻行起按 2 行步进读取互不相同的两套行。

需要注意：旧 composite 的源/目标行奇偶性是**相反**的（field 1 源指针先 +1 行，再写目标偶数行），即 B 的偶数行参与下场写入、B 的奇数行参与上场写入；这与较新的 `transition_luma.c`（第 463-466 行 `p_row = p_src + field*stride; q_row = p_dest + field*stride`，源/目标同奇偶）采用不同约定。本报告只陈述代码可观察行为，不对已弃用合成器的源行偏移意图做进一步推断。

**TFF 只重映射场 ID，不重映射处理时序。** `field_id = top_field_first ? (1 - field) : field` 仅交换两次循环的场编号（循环 0/1 ↔ 上/下场）；但循环次序恒为“先 field 0 后 field 1”，而两次循环的动画采样时刻是 `field_position = position + field * delta * length`（第 1108 行），即循环 0 取较早时刻、循环 1 取晚半帧时刻，且没有按场序交换这两个时刻。对照新版 luma 转场显式按 `field_order` 交换 `field_pos[0]/field_pos[1]`（`transition_luma.c:441-443`），旧 composite 正缺少这一交换。元数据 `transition_composite.yml:24-25` 对此有明确声明：

> `bugs: - Assumes lower field first during field rendering.`

### 2.3 场序条件清单

| 条件 | 行为 |
| --- | --- |
| A 帧 `consumer.progressive=1` 或转场 `progressive=1` | 整帧单次合成，`step=1`，不区分场 |
| 隔行路径 + `top_field_first=0`（下场优先） | 与合成器“假设下场优先”的固定时序一致；但注意源/目标行奇偶相反的栅格约定（§2.2），运动素材需实测确认 |
| 隔行路径 + `top_field_first=1`（上场优先） | 场 ID 被交换到正确的目标行集合，但两场的动画采样时刻未随场序交换，运动叠加存在半场（半帧周期）时序误差，即 YAML 记录的已知缺陷 |
| `deinterlace=1` | 两帧先经 deinterlace 链路（`avdeinterlace` 等，Bob/线性混合等）变成逐行帧，再走整帧路径，场问题被消除但垂直分辨率/时间分辨率由去隔行算法决定 |
| `fieldorder` 滤镜已把流归一化 | `top_field_first` 被改写为 consumer 要求值（`filter_fieldorder.c:115-116`），可纠正误报，但无法消除 2.2 的合成器假设本身 |
| 使用 `affine` / `qtblend` | 二者在取 B 帧前显式 `consumer.progressive=1`（`transition_affine.c:596-597`、`transition_qtblend.cpp:227-228`，注释 “This is not a field-aware transform.”），**强制逐行整帧处理，不提供逐场合成** |

---

## 3. 逐场采样的时间与空间约束（场感知合成路径）

逐场采样指 `composite` / `luma` 在隔行路径下，对同一帧缓冲跑两遍 `composite_yuv`，每遍只触碰奇偶行中的一套。

### 3.1 时间约束

1. **一场 = 半帧时间偏移。** 两场的几何位置不同：`field_position = position + field * delta * length`，其中 `delta = mlt_transition_get_progress_delta()`（`mlt_transition.c:285-307`），其值为转场进度在一帧内的半宽，即 `1 / (2 * (out-in+1))`。关键帧动画 `geometry`（`composite_calculate` → `mlt_properties_anim_get_rect`）因此在每场被重新求值，运动叠加层在两场位于各自真实采样时刻的位置。
2. **两场必须配对属于同一个帧周期。** 场循环只重算几何，不重新取像：A、B 图像缓冲在循环外只获取一次。B 帧若是隔行素材，其缓冲内本身织入了两个时刻的行；合成器按行奇偶各取所需，不跨帧取数据。
3. **时间相位依赖主导场顺序。** 采样时刻按“循环 0 早、循环 1 晚”固定分配，代码不按 `top_field_first` 交换；新版 `transition_luma.c` 才有显式交换（`field_pos[0]/field_pos[1]`，第 441-443 行）。因此旧 composite 的时间模型只在下场优先假设下自洽，TFF 素材的运动几何会带半场相位误差。
4. 透明度权重 `weight`（由 `geometry.o` 换算，第 354 行）与 luma 进度也按场位置计算，因此**溶解/擦除的过渡前沿在两场可以处于不同进度**，这是逐场时间正确性的一部分，而不是误差。

### 3.2 空间约束

1. **行地址步长翻倍。** `step=2`，源/目标 stride 均乘 2（第 444-445 行），每个场只处理 `height_src/2` 条有效行；垂直方向上合成器**不做任何跨行重采样**，只做平移与裁剪，因此不会把两个时间点的行混合，不产生梳齿。
2. **奇偶性即场身份。** 上场=偶数行、下场=奇数行（0 基）。目标 y 的奇偶性与 field_id 不符时做 ±1 行对齐（第 427-432 行）；几何 y 坐标为奇数的叠加在两场间会落到不同的物理行集合。
3. **色度约束更严。** YUV422 水平色度子采样要求 x 坐标与宽度取偶；`uneven_x != uneven_x_src` 时源指针补 2 字节做色度相位对齐（第 450-452 行）。垂直方向 422 不子采样，场切换不需要色度行修正，但 420p 输入必须先转换（`fieldorder` 滤镜即如此处理）。
4. **尺寸在两场间不变。** B 帧的缩放在场循环外由 `get_b_frame_image()` 一次完成，两场共用同一 `width_b/height_b`；场循环内只允许裁剪（`crop`/`crop_to_fill`）与对齐变化，不允许每场独立缩放。
5. Alpha 缓冲按同样的 stride 步进（`alpha_b_stride`/`alpha_a_stride`，第 446-447 行），保证遮罩与像素行严格同场同行。

---

## 4. 整帧缩放的时间与空间约束（resize/rescale/非场感知路径）

整帧缩放把整幅交织缓冲当作单一时刻的图像做二维重采样或补边。

### 4.1 时间约束

1. **垂直重采样会混合两个时刻的场。** 隔行帧相邻行来自相差半帧周期的两场，任何跨行插值（双线性、双三次、lanczos 等）都会把不同时间的样本加权到一起，产生梳齿/鬼影。
2. **框架的强制护栏。** `filter_rescale.c:216-219`：

   ```c
   // Deinterlace if height is changing to prevent fields mixing on interpolation
   // One exception: non-interpolated, integral scaling
   if (iheight != oheight && (strcmp(interps, "nearest") || (iheight % oheight != 0)))
       mlt_properties_set_int(properties, "consumer.progressive", 1);
   ```

   即：只要输出高度变化且不是“整数倍最近邻”，就触发去隔行信号，由 loader 链中位于 rescaler **之前**的 deinterlace 链接/滤镜先把两场融合成逐行帧。缩放本身由此只面对单一时间面。
3. **例外的语义。** 纯最近邻且垂直比为整数时，每个输出行只复制一个输入行、不做跨行混合，场时间结构不被污染，故豁免；水平缩放不跨行，不受此限。
4. **非场感知合成器同样强制逐行。** `affine`、`qtblend` 在取 B 帧前置 `consumer.progressive=1`，因为仿射变换（旋转、任意缩放、透视）无法在场分离的行集合上保持几何闭合，必须先去隔行得到单一时间面再整体变换。
5. `resize`（`filter_resize.c`）只做补边/居中粘贴（`memcpy` 整行），不重采样像素，**不混合场**；但补出的边属于“人造行”，其 alpha 由 `resize_alpha` 机制决定（见 §5）。

### 4.2 空间约束

1. 重采样在完整 W×H 栅格上进行，输出行与输入场行没有固定奇偶对应；几何精度由插值核决定，不再受“行奇偶=场身份”约束。
2. 缩放后 alpha 通道必须同步缩放：`filter_rescale.c:122-147` 的 `scale_alpha()` 用同一套最近邻步进比例独立重采样 alpha；`resize` 则用 `resize_alpha()` 以居中偏移逐行拷贝并以 `alpha_value` 填充新增区域。
3. 宽高比修正（`distort=0` 路径）在缩放前按 SAR/DAR 计算目标尺寸，可能留下补边区；YUV422 下宽度再次取偶。
4. 整帧路径的几何对应是“输出像素 ← 输入图像同一时刻的二维邻域”，与逐场路径“输出场行 ← 输入同场行的一维平移”是两种不同的空间契约（详见 §6）。

### 4.3 两条路径约束对照

| 维度 | 逐场采样（composite/luma，隔行路径） | 整帧缩放（rescale/resize/affine/qtblend） |
| --- | --- | --- |
| 时间模型 | 每行只属于一个场时刻；几何按半帧偏移分别求值 | 整帧视为同一时刻；跨行插值前必须先去隔行 |
| 垂直操作 | 仅平移/裁剪，`step=2`，无跨行混合 | 任意重采样，护栏：高度变化+非整数最近邻 ⇒ 强制 `consumer.progressive=1` |
| 几何求值频率 | 每场一次（两场位置/透明度可不同） | 每帧一次 |
| 尺寸变化 | 循环外统一缩放，场内只裁剪 | 帧内一次性缩放/补边，alpha 同步变换 |
| 色度 | YUV422，x/宽度强制取偶，色度相位按 2 字节对齐 | 420p 等在补边时先转 422/打包格式（`filter_resize.c:289-303`） |
| 已知缺陷 | 硬编码“下场优先”，TFF 素材动画时间相位反半场 | 去隔行算法本身的垂直/时间分辨率损失（Bob 倍线、线性混合柔化等） |

---

## 5. 透明度（Alpha）条件结论

### 5.1 Alpha 的来源与形态

- YUV422 像素本身不带 alpha，MLT 在帧属性上挂独立的单字节 `alpha` 缓冲（`mlt_frame_set_alpha()`，尺寸 = W×H）；带内嵌 alpha 的 `rgba`/`rgba64` 格式下 `mlt_frame_get_alpha()` 返回 NULL（`mlt_frame.c:551-563`），透明度改由像素第 4 通道承载——`composite` 强制 422，所以它始终操作独立 alpha 缓冲。
- alpha 由带透明通道的生产者（png/pixbuf、pango/qtext 字幕、watermark、matte 等）或 `imageconvert` 转换生成；`resize` 在 yuv422 无 alpha 且显式给定非 255 的 `resize_alpha` 时会新建遮罩（`filter_resize.c:182-187`）。

### 5.2 混合权重的构成（颜色混合规则）

`composite_line_yuv()` 对每个像素（`transition_composite.c:169-181`）：

```c
mix = ((luma ? smoothstep(...) : weight) * (alpha_b + 1)) >> 8;
*dest = (src * mix + dest * (65536 - mix)) >> 16;
if (alpha_a) *alpha_a = (mix >> 8) | *alpha_a;
```

最终覆盖系数是三个因子的乘积：

1. **几何不透明度** `weight`：由 `geometry.o`（0–100%）线性换算到 16 位定点；
2. **B 帧遮罩** `alpha_b`：缺失时按 255（完全不透明）处理（第 170 行三元表达式）；
3. **luma 遮罩**（可选）：`smoothstep(luma[j], luma[j]+softness, step)`，`softness` 控制过渡带宽度；无 luma 时直接用 `weight`。

`or`/`and`/`xor` 运算符改变的是 `alpha_b` 与目标已有 `alpha_a` 的位组合方式（第 184-275 行），即遮罩布尔代数，不改变 YUV 混合公式本身。

### 5.3 透明度条件清单

| 条件 | 叠加结果 |
| --- | --- |
| `alpha_b == NULL` | B 按全不透明（255）参与，混合量仅由 `geometry.o` 与 luma 决定 |
| `alpha_b[j] == 0` | `mix=0`，该像素保持 A 原值，B 完全不可见 |
| `alpha_b[j] == 255` 且 `o==100` 且无 luma | `mix=65535`，B 完全覆盖 A |
| `geometry.o == 0` 或几何宽高为 0 | 第 1044 行提前返回，**完全跳过合成**（优化路径） |
| `no_alpha=1` 且几何为全屏 100% | 第 1023-1030 行直接用 B 替换 A 图像，不做逐像素混合 |
| `aligned=0`（非对齐模式） | `get_b_frame_image` 调 `mlt_frame_set_resize_alpha(b_frame,1,255)`，补边区强制不透明，B 以矩形块全覆盖，不出现透明边 |
| `aligned=1`（默认） | 保留 B 自身 alpha，补边区透明，叠加保持素材形状 |
| 转场显式设置 `alpha_a`/`alpha_b` 属性 | 第 1100-1104 行用 `memset` 把对应遮罩整体覆写为指定常量，可“彻底抹掉”任一侧 alpha |
| 目标 alpha 存在 | `*alpha_a = (mix>>8) | *alpha_a`：单调累加（OR 语义），输出遮罩只增不减，供下游继续叠加 |
| rgba/rgba64 路径（qtblend/affine） | 无独立 alpha 缓冲，透明度走像素内嵌通道，`mlt_image_is_opaque()` 判定后可走零合成快速路径（`transition_qtblend.cpp:246-253`） |
| 隔行场渲染 | alpha 与像素行同 stride、同场步进；两场可因 `o`/luma 的场位置不同而写出不同的逐行遮罩值，遮罩本身也是逐场时间采样的 |

### 5.4 尺寸变化与 alpha 的耦合

- B 帧任何缩放都发生场循环之前；`rescale` 的 `scale_alpha()` 与图像使用**相同的 x/y 步进比例**，保证遮罩与彩色几何不发生错位；`resize` 的 `resize_alpha()` 用同一居中偏移 `(offset_x, offset_y)` 拷贝。
- 若图像缩放而 alpha 未同步（尺寸不匹配的防护见 `filter_rescale.c:256-258`），逐行索引会错位，叠加轮廓与颜色边缘错开——框架通过“alpha 尺寸不等于 ow×oh 才重缩放”的判定保持两者一致。
- luma 图本身在 `get_luma()` 中按 `width_b × height_b` 最近邻缩放（`scale_luma`，第 497-520 行），使第三层遮罩与 B 几何同栅格。

---

## 6. 几何正确性与颜色混合正确性的区分

两类错误彼此独立，必须分别验证：

### 6.1 视频几何对应（geometry correspondence）——“B 的哪个样本落到 A 的哪个位置”

判定标准：输出场行的来源行必须与场身份同奇偶，变换后坐标对应同一时刻的场景点。

- 逐场路径的几何是**一维行平移 + 二维裁剪**：`composite_yuv` 只做指针偏移与 stride 步进，不发明新像素；几何正确的充要条件是场 ID↔目标行奇偶映射正确、两场动画采样时刻与主导场序一致、色度相位对齐。TFF 缺陷（§2.2）属于**几何/时间对应错误**（动画晚/早半场），而非颜色错误；旧合成器源/目标行奇偶相反的栅格约定（§2.2）则要求对运动素材做逐场实测，不能只凭静态帧判断。
- 整帧路径的几何是**二维仿射/重采样**：正确性要求去隔行先把两场折叠为单一时间面，否则插值核跨行取到不同时刻样本，几何对应在时间维度上失效（空间坐标仍然连续，但对应点不属于同一瞬间）。
- 裁剪、`halign/valign`、`crop_to_fill`、consumer 比例尺（`x = item.x * width_dest / nw`，第 358-359 行）只改变映射的坐标参数，不改变混合数学。

### 6.2 颜色混合（color blending）——“落点确定后，像素值如何加权”

判定标准：在几何映射已正确的前提下，YUV 各分量按覆盖系数做凸组合（`sample_mix`，系数和恒为 1），alpha 输出满足 OR 累加语义。

- 混合公式本身与场无关：无论 `step=1` 还是 `step=2`，每行调用的是同一个 `line_fn`，定点权重 `mix ∈ [0,65536]` 保证不越界、不饱和漂移。
- 颜色正确性的失效模式来自权重侧：alpha 与图像尺寸/步长不同步、`alpha_a/alpha_b` 被 memset 覆写、luma 图缩放栅格不一致、非对齐模式补边 alpha=255 导致矩形硬边。
- YUV422 的色度相位对齐（§3.2 第 3 点）是几何问题；而 `smoothstep` 软边、`o` 透明度、OR/AND/XOR 遮罩运算是颜色/遮罩问题。
- 去隔行引入的行间平均（如 `avdeinterlace` 的 `[-1,4,2,4,-1]/8` 核，`filter_avdeinterlace.c:88-113`）发生在合成之前，它改变的是输入像素值（颜色），但其必要性由几何/时间约束触发——两个层面在管线中接力，不可混为一谈。

### 6.3 验收要点

1. 几何验收：用运动竖条/网格素材检查隔行输出的每场行集合，TFF 素材重点核对动画时间相位；对照 §2.2 核验源/目标行的场归属（旧 composite 奇偶相反，luma 同奇偶），确认无场行错位、无色度 1 像素偏移。
2. 颜色验收：用带 alpha 的字幕/PNG 检查轮廓软边、`o` 渐变、luma 擦除边界与输出 alpha 的 OR 累加；确认补边模式与对齐模式的透明/不透明预期。
3. 缩放验收：高度变化时确认 `consumer.progressive` 已置位、deinterlace 先于 rescaler 执行；整数最近邻豁免路径确认无跨行采样。

---

## 7. 结论速查

**场序条件**

- 逐场合成仅在 `consumer.progressive=0` 且转场 `progressive=0` 时启用；上场=偶数行、下场=奇数行，两场几何按半帧进度差分别采样。
- 下场优先（TFF=0）与合成器的固定时序假设一致；上场优先（TFF=1）时场 ID 映射到正确的目标行集合，但动画时间相位因缺少按场序的时刻交换而反半场（`transition_composite.yml` 明示的已知缺陷；新版 `transition_luma.c` 已通过 `field_pos[]` 交换修正同类问题）。
- 现代合成器 `affine`/`qtblend` 不做场渲染，强制逐行；`fieldorder` 滤镜负责在缩放前归一化场序。

**透明度条件**

- 覆盖量 = 几何不透明度 `o` × B 帧 alpha（缺省 255）× 可选 luma 遮罩（含 softness）；目标 alpha 单调 OR 累加。
- `o=0`/零尺寸跳过合成；`no_alpha` 全屏走替换；`aligned=0` 补边强制不透明，`aligned=1` 保留素材透明；`alpha_a/alpha_b` 属性可整体覆写遮罩。
- 图像、alpha、luma 三者必须在同一缩放栅格上（框架以同比例步进与尺寸校验保证），否则几何与颜色分别出现错位类错误。

**两条空间-时间契约**

- 逐场采样：同一缓冲、两套奇偶行、两个半帧时刻，只平移不重采样，垂直时间结构保真但受下场优先假设约束。
- 整帧缩放：单一时间面上的二维重采样，以“高度变化即先去隔行（整数最近邻除外）”为护栏，代价是去隔行算法带来的分辨率损失，收益是任意几何变换与连续插值。
