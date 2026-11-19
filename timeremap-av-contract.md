# timeremap 音画同步契约分析

**对象**：`src/modules/core/link_timeremap.c`（`timeremap` link），以及它所依赖的框架行为与相邻实现：
`src/framework/mlt_link.c`、`src/framework/mlt_chain.c`、`src/framework/mlt_frame.c`、`src/framework/mlt_audio.c`、
`src/framework/mlt_animation.c`、`src/framework/mlt_property.c`、`src/framework/mlt_producer.c`、
`src/modules/core/producer_timewarp.c`、`src/modules/rubberband/filter_rbpitch.cpp`、
`src/modules/plus/filter_wsola.cpp`、`src/modules/resample/filter_resample.c`。

**方法声明（重要）**：本报告完全来自**静态阅读**。当前分支（`task/100`，HEAD `2a51ed9`）没有构建产物，也没有任何针对 `timeremap` 的测试
（`src/tests/` 下无 timeremap 用例，`grep -rn timeremap src/tests` 为空），因此下文标注为
「可推导」的结论是**读码得出的契约**，不是运行验证的结果；标注为「无法推出」的部分是代码没有形成闭环的地方。
为避免夸大，凡是依赖运行时条件（模块是否编译、宿主是否设置属性、消费者线程模型）的结论都归入第 8 节。

---

## 1. 调用链与坐标系统

`timeremap` 是一个 **link**，挂在 chain 上。取帧路径：

```
mlt_chain.c:488  producer_get_frame(chain)
  └─ mlt_producer_seek(base->begin, mlt_producer_frame(parent))   // 链的 _frame = in + position
     └─ link_get_frame(link)            link_timeremap.c:525
        ├─ integrate_source_time() / time_map / 直通   → source_time, source_duration
        ├─ 逐帧 seek(self->next) 取源帧，存入 unique_properties（键=源帧号）   :583-621
        ├─ push_get_image → link_get_image_nearest / _blend                  :650-657
        └─ push_audio     → link_get_audio                                   :659-660
```

几个坐标量必须区分清楚（它们决定了端点与关键帧的含义）：

| 量 | 来源 | 单位/含义 |
|---|---|---|
| `position` | `mlt_producer_position(link)` | link 帧号（链内的绝对坐标） |
| `in` | link 的 `in` 属性 | 默认**被清空**为 0（`mlt_link.c:57`） |
| `in_time` | `in / link_fps`（`link_timeremap.c:536-537`） | 秒；映射原点 |
| `link_fps` | link 的 profile（`link_configure` 设为 chain profile，`:97-102`） | 输出帧率 |
| `source_fps` | `mlt_producer_get_fps(self->next)`（`:533`） | **源**帧率，可与 link_fps 不同 |
| `source_time` | 映射结果 + `in_time` | 秒，源时间轴 |
| `source_duration` | `t(p+1) - t(p)` | 秒，**带符号**（负=倒放） |
| `source_speed` | `source_duration / (1/link_fps)` | 倍速（带符号），回写到 `speed` 属性（`:663`） |

关键点：

1. **动画索引是 `position - in`，不是 0 基**（`:552-556`、`:78`/`:84`）。link 初始化时 `mlt_link_init` 清空了 `in/out/length`
   （`mlt_link.c:57-59`），所以除非 XML/宿主显式写入 `in`，动画的 0 帧对应的是**链内绝对帧号**（chain 的 `_frame`，含链自身的 in，
   见 `mlt_chain.c:499` 与 `mlt_producer.c:369-376`）。这正是「相对关键帧是否真的相对」无法从实现推出的根源之一。
2. **`length` 同样被清空**（`mlt_link.c:59`；`mlt_properties_clear` 会移除值，`mlt_properties.c:2449-2461`，
   之后 `get_position` 读到的是 0），而 `mlt_producer_get_length(link)` 直接读这个属性（`mlt_producer.c:590-593`），
   于是 link 传给 `mlt_properties_anim_get_double()` 的 `length` 参数是 0。`mlt_animation_parse_item()` 对**负关键帧**的策略是
   `item->frame += mlt_animation_get_length(self)`（`mlt_animation.c:453-455`），而 `mlt_animation_get_length()` 在
   `self->length <= 0` 时回退为**动画节点中的最大帧号**（`mlt_animation.c:368-383`）。因此 `-1=...` 的实际含义变成
   「相对最后一个关键帧自己的帧号」，既不是相对片段长度，也不是相对片段 in 点；当唯一关键帧就是 `-1=x` 时结果是 `x` 落在 0 帧，
   多关键帧时则整体错位。这是硬约束，不是风格问题。
3. **仅当存在 `_frc` 链接时**，chain 才把源设为原生帧率（`mlt_chain.c:534-548`），`source_fps` 才真正是源的原生帧率；
   否则源被设为链的帧率，`source_fps == link_fps`，映射退化为时间轴上的普通缩放。
4. `in_time` 用 `link_fps` 计算，而源帧号用 `source_fps` 换算（`:588`）。当两者不等（帧率转换模式），源起点是
   `in * source_fps / link_fps` 帧，而不是 `in` 帧。**只有 fps 相等时 `in` 才字面成立。**

---

## 2. 时间重映射的积分（`speed_map`）

### 2.1 算法（`link_timeremap.c:59-95`）

`source_time(p) = in_time + Σ_{k=in}^{p-1} speed(k)/link_fps`，即对离散速度序列做**左黎曼和**（每个输出帧取该帧起始处的速度，
乘以 1 帧时长）。实现上：

- 缓存 `prev_integration_position` / `prev_integration_time`，每次只走 `prev → position` 的增量（`:76-88`）；
- 反向走时逐帧**相减**（`:82-88`），因此「回头」在数值上是精确逆运算（同一速度序列）；
- 缓存失效条件：`prev < in`，或 `|prev - position| > position - in`（`:68-72`）。后一条意味着**向后跳得比离 in 还远**时从 `in` 重算。
- `speed_map` 属性变化会把缓存清成 `(0, 0.0)`（`:51-56`），下一次积分因 `prev < in` 再次从 `in` 重算。

### 2.2 由此得出的判断

| 判断 | 依据 | 后果 |
|---|---|---|
| 曲线是**分段常值**近似 | 每帧只用起点速度，误差 ≤ 线性段斜率×1 帧 | 非线性变速段存在**系统性位置偏差**，最大约半帧/段；与「精确积分」不一致 |
| 该偏差**不破坏音画同步** | 音频与视频使用**同一个** `source_time`（`:548-549` 与 `:140`） | 偏差只是「选错了内容位置」，不是「音画错位」 |
| 突变（离散关键帧）→ 单帧 `|speed|` 极大 | `source_duration` 由相邻两帧差决定（`:549-550`） | 会被音频阈值（0.1..10，见 4.4）判为静音；视频仍按窗口取帧 |
| 积分成本随跳转距离增长 | 前跳超距时从 `in` 重算（`:68-72`） | 长片段的随机 seek 为 O(position-in) |
| 循环播放 `p+1` 也走一次积分 | `:549` 调用后缓存停在 `p+1`，下一帧命中缓存 | 顺序播放时摊薄为 O(1)/帧 |

**数值示例**（25 fps、`in=0`、`length=25`、速度从 1 线性升到 3）：
左黎曼和给出 `t(24) = 1.88 s`，而该曲线的精确积分（梯形）为 `1.92 s`——**一个整帧的系统性滞后**。
但 `source_duration` 用的是同一套差分，音频窗口同样落在 `1.88 s` 附近，所以**音画仍对齐，只是内容整体比理论曲线靠后一帧**。
这就是「映射精度」与「音画同步」必须分开承诺的原因。

### 2.3 `time_map` 分支（`:551-557`）

不积分，直接 `source_time = anim(time_map, p-in) + in_time`，`source_duration` 取相邻两帧动画值之差。
因此 `time_map` 的导数就是瞬时速度，**没有 0.1..10 的平滑保护**：一个离散关键帧跳变会产生巨大的 `|source_duration|`，
视频侧会把 `[floor(t), t+|d|)` 之间的**全部源帧**逐帧拉取（`:595-621`，与 `image_mode` 无关），一个跨越数秒的跳变就会在单帧内
顺序解码成百上千帧；音频侧则因 `|speed| > 10` 直接返回静音（`:152-161`）。这是 `time_map` 独有的病态路径。

### 2.4 无映射分支（`:558-563`）

`speed = 1`、`duration = 1/link_fps`，等价于**纯帧率转换**。chain 在「源 profile 不可变且无 `_frc` 链接」时会自动挂一个无参数的
`timeremap` 来做 FRC（`mlt_chain.c:550`）。此时音频是连续拷贝 + 频率不变（见 5.3），没有任何拉伸，同步最强的一档。

---

## 3. 视频采样：最近帧与混合帧

### 3.1 取帧集合（`:583-621`）

```
in_frame_pos = floor(source_time * source_fps)
frame_time   = in_frame_pos / source_fps
source_end   = source_time + fabs(source_duration)      // 注意 fabs：方向不影响取帧集合
while (frame_time < source_end) { 取 in_frame_pos; in_frame_pos++; ... }
```

- 集合 = `[floor(t), 第一个起点时间 ≥ t+|d| 的帧)`，即**包含 t 所在帧，不含恰好落在终点的帧**；
- 唯一例外：窗口退化（`t` 正好在帧边界且 `|d| = 0`）时 `+1e-10` 强取一帧（`:591-594`）；
- **取帧集合与播放方向无关**（`fabs`）。倒放时仍然向**大帧号**方向读，音频最后整体反转（`:283-286`）。
  因此 -2x 倒放时，每输出帧都会多解码 `|d|` 帧，虽然只显示其中一帧。

### 3.2 `image_mode=nearest`（默认，`:464-523`、`:653`）

只取集合中的**第一帧**，即 `floor(source_time*source_fps)`——**「最近帧」实际是「t 之前（含）的那一帧」，是 floor，不是四舍五入**。
效果：快放 = 丢帧（时间混叠/抖动），慢放 = 重复帧。v1 的「最近邻」没有任何运动补偿或快门近似。

### 3.3 `image_mode=blend`（`:349-462`）

- 对集合中**至多 10 帧**做**等权算术平均**（`MAX_BLEND_IMAGES`，`:347`），16 位/8 位分别累加后整除；
- 集合只有 1 帧时（含 `|d|` 小于一帧、或速度 0）**退回 nearest**（`:653` 的 `in_frame_count == 1` 短路），不产生任何平均；
- **权重与帧的时间重叠无关**：`|d| = 2.5` 帧时平均 3 帧，各占 1/3，而第三帧只贡献半帧 → 时间重心偏移；
- 超过 10 帧（`|speed| > 10`）时**静默截断**：视频只覆盖窗口的前 10 帧，曝光短于映射要求，且与音频（此时为静音）不同步长；
- **只有 `rgba64` 走 16 位分量路径**；`yuv444p10`/`yuv420p10`/`yuv422p16` 等 16 位/每样本格式落进**按字节**的 `uint8_t` 路径（`:440-451`），
  对高/低字节分别求平均——一旦平均值在字节间产生进位（几乎必然），结果就不是分量平均，而是**损坏的像素值**；
- rgba64 路径把 `size` 减半后写回 `mlt_frame_set_image`（`:429-439` 与 `:453`），**图像大小元数据只有实际值的一半**（数据本身正确）——其他消费者若信赖该 size 会读错；
- nearest 会**整块 `memcpy`** 源图（`:501-504`）并单独复制 alpha（`:511-517`）；blend 只对像素字节求平均，**不复制 alpha**，也不做预乘。

### 3.4 与音频窗口的对应关系

视频取 `floor(t)`，音频窗口从 `t` 开始（正放）或以 `t+|d|` 结束（倒放）。所以：

- 正放：音频第一个样本对应源时间 `t`，视频显示包含 `t` 的那一帧 → 内容错位 ≤ 1 个源帧，音频**领先**该帧起点 `frac(t)`；
- 倒放：音频最后一个样本对应 `t`，视频仍显示 `floor(t)` → 内容错位 ≤ 1 个源帧，锚点在窗口**另一端**；
- 两者都不做亚帧插值。**「最近帧/混合帧」是离散量化，音频是连续重采样**——这是本实现最本质的策略差异（见第 6 节）。

---

## 4. 音频获取

### 4.1 每帧样本数与源区间（`:169-174`）

```
link_sample_count = samples_per_frame(link_fps, freq, frame_position)   // 输出应为多少
sample_count      = lrint(link_sample_count * |source_speed|)           // 需要从源取多少（同频率下）
in_frame_pos      = floor(source_time * source_fps)
first_out_sample  = llrint(source_time * freq)
```

`sample_count` 是**按同一频率**换算的源样本数，等价于 `|d| * freq`。也就是说音频读取的**源时间区间与视频取帧窗口完全相同**，
起始偏移精确到样本（`samples_to_skip = first_out_sample - first_in_sample`，`:203-206`）。

### 4.2 跨源帧拼接（`:224-273`）

按 `in_frame_pos++` 顺序取 `unique_properties` 里缓存的源帧，用 `mlt_audio_copy` 拼接；不足则：
- 帧缺失 → `Frame not found`；
- 取样失败/格式为 none → 中止；
- 最终 `samples_copied != sample_count` → `Sample under run` 并 `mlt_audio_shrink`（`:275-281`）。
**收缩后返回的样本数少于预期**，下游 `resample`/`swresample` 会用重复/静音补齐（见 `filter_resample.c:155` 的 "Repeat samples"），
这会让**内容时间轴发生偏移**——同步的强保证在这里被削弱。

### 4.3 样本连续性（±4 采样）（`:176-201`）

- 正放：若上一帧的结束样本 `_continuity_sample` 与本帧 `first_out_sample` 相差 **< 4 采样**，则把起点改成上一帧的终点，
  同时从 `sample_count` 里增减相同数量；
- 倒放：对称地终点对齐（`:191-201`）；
- 差值 ≥ 4 时**不做任何处理**，接缝处可能有跳变（硬编码常量 `SAMPLE_CONTINUITY_ERROR_MARGIN = 4`，`:177`）。

量化：48 kHz 下 4 采样 = **0.083 ms**。这是实现里唯一显式的「音画对齐容差」。
代价：为保证终点不变，每帧实际消费的源区间被平移最多 4 采样，即**内容相对视频有 ≤0.083 ms 的偏移**；
并且 `out.frequency` 的重标定用的是**修正后的** `sample_count`（`:294-295`），所以修正会带来 ≤4/1920 ≈ **0.21% 的瞬时速率/音高抖动**
（以 25 fps / 48 kHz 计；总时长仍严格一帧，因为重标定让输出样本数恒为 `link_sample_count`）。

### 4.4 速度阈值与静音（`:152-161`）

`|source_speed| < 0.1` 或 `> 10` 时，**返回等长静音**（用 `mlt_frame_original_position` 计算样本数），不做拉伸、不做降级采样。
所以速度 0（暂停）是「视频定格 + 音频静音」，而不是保持最后一个样本；>10x 的快速段也完全无声。
这个阈值**没有写进 `link_timeremap.yml`**（yml 只声明了只读 `speed`）。

### 4.5 频率重标定——真正的「变调变速」（`:293-297`）

```
out.frequency = lrint(out.frequency * sample_count / link_sample_count)   // ≈ freq * |speed|
```

link 并**不**自己做时间拉伸：它把 `|speed|` 倍的样本按「采样率变成了 freq×|speed|」来**贴标签**，真正的重采样交给下游
`resample`/`swresample` 归一化链接（`chain_normalizers.ini:21`）。这一条决定了默认听感：

- `pitch = 0`（默认）→ 变调变速（快放升调、慢放降调），音频时长准确、音高随速度走；
- 下游归一层缺失时，消费者直接拿到标着 `freq×|speed|` 的数据，等价于变速变调（通常是预期的「无补偿」行为）；
- 源采样率与请求采样率不同时，link 会**覆盖请求频率**为源频率（`:163-166`），依赖归一层换算回来。

### 4.6 `pitch = 1`：只走 rbpitch（`:303-337`）

- 无条件 `mlt_factory_filter(..., "rbpitch", NULL)`；**从不使用 wsola**；
- 设 `stretch = 1`、`pitchscale = in_frequency / requested_frequency`，然后 `mlt_filter_process` + 末尾再调一次
  `mlt_frame_get_audio`（`:339-344`）让滤镜生效；
- `stretch=1` 时 rbpitch 的 `timeratio = requested_samples / in.samples ≈ 1/|speed|`（`filter_rbpitch.cpp:94-98`），
  即由 RubberBand 完成时间拉伸并保持音高；`pitchscale≈1`，只用于修正源/输出采样率差；
- **若 rbpitch 不可用**（未编译 rubberband 模块），回退到 `resample`（`:321-337`）——只是重采样，
  **音高仍然随速度变化**，即「勾了音高补偿却静默失效」；
- rbpitch 将 `in_samples - out_samples` 写进**它自己的**滤镜属性 `latency`（`filter_rbpitch.cpp:214`），
  link 不读取、不上报、不补偿任何延迟。

### 4.7 倒放（`:283-291`）

先按**正序**拷贝 `[t, t+|d|)`，再 `mlt_audio_reverse()` 整体倒序（`mlt_audio.c:344`），并把 `_continuity_sample` 设为
`first_out_sample`。于是倒放音频的内容窗口与正放相同，只是播放方向相反，锚点在窗口末端。
**注意**：倒放时若开启 `pitch=1`，被反转的音频是**逐帧**送入一个有状态的实时拉伸器，且拉伸器跨帧状态在倒放流上复用，
其质量没有任何实现层面的保证（RubberBand 实时引擎按正向流设计）。

---

## 5. 变速曲线、倒放、源端点如何影响输出

### 5.1 变速曲线

| 曲线形态 | 视频 | 音频 |
|---|---|---|
| 恒速 1x，无映射 | 逐帧直通 | 连续拷贝，时长严格 |
| 恒速 s ∈ [0.1, 10] | 每输出帧取 `ceil(...)` 帧；nearest 丢/重帧，blend 等权平均 | `sample_count = 1920·|s|` 个源样本，重标定频率 → 时长精确等于 1/link_fps |
| 缓变（线性/样条） | 取帧集合随 `t` 连续移动，边界处切换 | 每帧重新取样，起点精确到样本；±4 采样接缝修正 |
| 突变/离散关键帧 | 取帧窗口在单帧内跳变（可能拉取大量帧） | `|s|>10` → **整帧静音**；否则一次大跨度拷贝 |
| 速度 0 | 定格（`+1e-10` 强取一帧） | 静音 |
| 速度符号翻转 | 取帧集合仍向大帧号方向；显示帧随 `t` 反向 | `source_duration<0` → 反转；跨符号时接缝无淡入淡出 |

### 5.2 倒放

- **视频**：`floor(t)` 随 `t` 递减而递减，等价于逐帧倒序显示；`blend` 仍平均窗口内**较大**帧号的两帧，
  与 `nearest` 选 `floor(t)` 相比，两者在倒放时的**时间锚点不同**（blend 偏向窗口高帧号端）；
- **音频**：先正序取 `[t, t+|d|)` 再整体反转，第一个输出样本对应 `t+|d|`；
- **A/V**：两者锚点分别是窗口首/尾，因此倒放时 A/V 的内容相位与正放**不共轭**（相差一个 `|d|`），
  在 `|s| > 1` 的倒放上可达整帧级差异；
- **连续性**：倒放走另一套终点对齐分支（`:191-201`），阈值同为 4 采样；超出阈值时接缝跳变。

### 5.3 源素材端点

- **start 之前**：`integrate_source_time()` 在 `position < in` 时因 `position - in < 0`、条件 `integration_distance > (position - in)` 恒真而**从 `in` 重算并向回减**
  （`:68-72`）。若曲线首个关键帧不等于 1x，减出来的 `source_time` 就不是「in 之前按 1x 走」，注释（`:535`）的假设**不成立**。
  `source_time` 一旦降到 0 以下，`in_frame_pos` 为负：`mlt_producer_seek(self->next, 负值)` 对普通 producer 会被夹到 0
  （`mlt_producer.c:358-359`），于是**负键号下反复存的是第 0 帧**；音频的实际起点 `first_in_sample` 也是按负位置算的，
  `samples_to_skip` 仍落在 0 附近，所以通常表现为**画面/声音一起冻结在素材第 0 帧**，而不是静音；
  只有当源 producer 取帧失败或帧数不足时才会走到 `Frame not found` / `Sample under run` → `mlt_audio_shrink`（`:229`、`:275-281`）。
  **端点没有任何显式补边或钳制逻辑。**
- **end 之后**：link 自身不做边界检查（`mlt_link.c:124-141` 明确「link 不做 bounds check」），chain 还把源的
  `length` 设成 `0x7fffffff`、in/out 设为 0..N-1（`mlt_chain.c:209-212`）。所以「超出素材末尾」到底是**冻结末帧**还是**黑场/测试帧**，
  完全由**源 producer 的 eof 行为**决定，link 只是接受它返回的任何帧并当作真实素材参与 blend/平均。
- **chain 的 in/out 与素材的 in/out 是两套坐标**：链的 `_frame = 链 in + position` 传给 link（`mlt_chain.c:499`），
  源 producer 的 in/out 已被链改成 0..huge。因此映射的 0 点相对的是**底层资源**，不是片段的 in 点。
- **帧率转换时**，`in_time` 用 `link_fps`、源帧号用 `source_fps` 换算（`:536-537` 与 `:588`），
  两 fps 不等时起点 = `in·source_fps/link_fps` 帧，端点整体按比例缩放。

---

## 6. 视频离散采样 vs 音频连续拉伸：策略差异（结论）

| 维度 | 视频 | 音频 |
|---|---|---|
| 时间表示 | **离散**：选/平均整帧；无亚帧插值 | **连续**：按样本定位，`first_out_sample` 精确到 1 采样 |
| 重采样方式 | 无（nearest 复制；blend 像素平均） | 频率**重标定** + 下游 sinc 重采样（`resample`/`swresample`） |
| 时间拉伸 | 无 | 默认 = 重采样（变调）；`pitch=1` 时 = RubberBand 保持音高 |
| 音高 | 不涉及 | `pitch=0` **必然随速度变调**；`pitch=1` 依赖 rbpitch 且失败静默降级 |
| 时间量化误差 | ≤1 源帧（blend 权重误差再 +≤1 帧；>10 帧截断） | ≤1 采样（起点），接缝修正 ≤4 采样 |
| 极端速度 | 仍出图（>10x 时 blend 截断到 10 帧） | `|s|<0.1` 或 `>10` → **静音** |
| 倒放锚点 | 窗口低端 `floor(t)` | 窗口高端 `t+|d|`（播放方向反转） |
| 延迟 | 无 | rbpitch 有内部延迟，但 **link 不补偿、不对外报告** |

**一句话**：视频是「在源时间轴上做离散采样与等权时间平均」，音频是「在源时间轴上做连续采样并按频率重标定做变速」。
两者共用同一个 `source_time`，所以**时间基准一致**；但**内容粒度、音高、极端速度下的可用性、倒放相位**都不同。

---

## 7. 可以保持的同步保证（逐项，附依据）

> 前提：下游存在 `resample`/`swresample` 归一化链接（chain 自动附加，`chain_normalizers.ini:21`），且消费者按帧顺序请求。

| # | 保证 | 强度 | 依据 |
|---|---|---|---|
| G1 | **每输出帧的音频时长 = 视频帧时长**（1/link_fps），与速度无关 | 严格，除非 underrun | `:294-295` 重标定使输出样本数恒为 `link_sample_count`；`:169-171` |
| G2 | **音频与视频使用同一 `source_time`**，不存在独立的音频时钟 | 严格 | `:548-549` 写属性，`:140` 读同一值 |
| G3 | **音频内容窗口 = 视频取帧窗口** `[t, t+|d|)`（同一 `fabs` 语义） | 严格 | `:590` 与 `:172-174` 都基于同一对 `source_time/source_duration` |
| G4 | **A/V 内容错位 ≤ 1 个源帧**（正放锚在窗口首，倒放锚在窗口尾） | 定量上界 | `:479` floor 取帧；`:174` 精确样本起点 |
| G5 | **接缝采样误差 ≤ 4 采样（48 kHz 下 0.083 ms）**，且总时长不受影响 | 定量上界 | `:177-201` |
| G6 | **1x、无映射、且 `source_fps == link_fps` 时为严格逐帧直通**，无任何重采样/拉伸 | 严格 | `:558-563` + `:293-297`（`speed=1` → 重标定比 1:1）；fps 不等的情形见 G11 |
| G7 | **速度 0 时视频定格、音频静音，长度仍精确** | 严格 | `:152-161`、`:591-594` |
| G8 | **`|s| < 0.1` 或 `|s| > 10` 时给出等长静音**（不是错位、不是漂移）；端点值 0.1 与 10 本身仍走正常拉伸 | 严格 | `:152` 的严格不等号 |
| G9 | **seek 后映射可从 `in` 重算**，顺序播放摊薄为 O(1)/帧 | 严格（缓存正确性） | `:68-72`、`:549-550` 的缓存回填 |
| G10 | **`time_map` / `speed_map` 互斥优先级固定**（`speed_map` 优先），`map` 自动转 `time_map` | 严格 | `:546-553`、`:47-50` |
| G11 | **FRC 模式下音频不变调、时长精确** | 严格 | 重标定比 = 1（`sample_count == link_sample_count`） |

**不能由 G1–G4 推出「逐样本精确音画对齐」**：G4 的误差是**最多一个源帧**，不是零；混合模式还会引入 ≤1 帧的加权误差；
`pitch=1` 的内部延迟没有补偿。

---

## 8. 无法从当前实现推出的同步保证（也是不能直接承诺的音质）

### 8.1 结构性「无法推出」

| # | 无法承诺 | 为什么从实现推不出 |
|---|---|---|
| U1 | **相对（末尾）关键帧按片段长度生效** | link 的 `length` 在 `mlt_link_init` 被清空（`mlt_link.c:59`），link 从不从 `next` 回填；`mlt_animation_get_length()` 因此回退到「最大关键帧帧号」（`mlt_animation.c:368-383`），`-1=...` 不再是相对片段结尾 |
| U2 | **关键帧相对片段 in 点** | link 的 `in` 同样被清空（`:57`），除非宿主写入，动画索引是链内绝对帧（`mlt_chain.c:499`） |
| U3 | **改变 `in` / `length` / profile 后积分仍然正确** | 只有 `speed_map` 属性变化才清缓存（`:51-56`）；`in`/`length` 变化不会重置 `prev_integration_*` |
| U4 | **`pitch=1` 真的保持音高** | 只找 `rbpitch`；找不到就退到 `resample`（`:321-337`），音高仍随速度变化，且无日志/属性暴露 |
| U5 | **有音高补偿时无附加延迟** | link 从不读 rbpitch 的 `latency`（`filter_rbpitch.cpp:214`），也不重写时间戳；延迟是否存在取决于 RubberBand 内部缓冲 |
| U6 | **`blend` 的时间平均是正确曝光** | 等权平均，与重叠时长无关（`:434-450`）；>10 帧静默截断（`:347`） |
| U7 | **倒放时音画相位一致** | 视频锚 `floor(t)`，音频锚 `t+|d|`（`:283-286` + `:479`）；`|s|>1` 时差可达整帧 |
| U8 | **素材端点被正确补边** | 起点靠 producer 的 `<0` 钳制与 underrun 兜底；终点靠源 producer 的 eof 行为（`mlt_chain.c:209-212` 把源长度改成近乎无限）；link 自身无端点逻辑 |
| U9 | **`time_map` 跳变不会静默残帧** | 跳变产生巨大 `|d|`：视频顺序解码海量帧，音频判静音（`:152-161`），`Frame not found` 后 `shrink`（`:275-281`） |
| U10 | **并行消费者下音频连续性正确** | `_continuity_sample`（`:178`、`:286-290`）与 `pdata->resample_filter/pitch_filter` 的惰性创建（`:305-336`）**无锁**；而同一文件里的图像路径是加锁的（`:376`、`:486`）。多线程 get_audio 下存在竞争 |
| U11 | **克隆/多消费者共享同一 link 实例时互不干扰** | `_continuity_sample` 存在 **link 属性**（整个服务共享），不是 per-frame（对比 `mlt_frame_unique_properties` 的用法）；多个播放序列会互相改这个锚点 |
| U12 | **输出采样率始终等于消费者请求值** | 源有 `audio_frequency` 时 link 覆盖请求频率（`:163-166`），最终频率取决于下游归一层；无归一层时拿到的是 `freq×|speed|` |
| U13 | **音频重采样是无损/无重复的** | 归一层在样本不足时会重复输入（`filter_resample.c:155`），underrun 时 link 会 `shrink`；两者都会让内容时间轴偏移 |
| U14 | **`|s| > 10` 的段落有音频** | 直接静音（`:152-161`），无文档 |
| U15 | **速度阈值/混合上限/连续性容差是公开契约** | 0.1、10、10 帧、4 采样都是硬编码常量（`:152`、`:347`、`:177`），`link_timeremap.yml` 未声明 |
| U16 | **link 的输出音频格式符合 yml 声明** | `link_timeremap.yml:13-14` 声明 `audio_formats: [float]`，但 `link_get_audio` 在 `pitch=0` 时**不做格式转换**——`mlt_audio_copy` 要求源与目标格式一致（`mlt_audio.c:454-457`），于是它只是原样传出源格式（s16 等），靠下游 `audioconvert` 归一化（`chain_normalizers.ini:20`）。无归一层时消费者拿到的是与元数据不符的格式 |
| U17 | **schema/元数据与实现的语义一致** | yml 把 `speed_map` 描述为「input speed to output frame times」，`time_map` 描述为「input frame times to output frame times」（`:25-38`），都没说清单位（秒）、坐标系（相对 `in`）、以及 `speed_map` 优先；`image_mode` 的 10 帧上限、`blend` 的等权语义均未声明 |
| U18 | **该契约有回归测试保护** | `src/tests/` 无任何 timeremap 用例；`producer_timewarp`+`wsola` 有测试，但走的是另一条代码路径（见 8.2） |

### 8.2 「音质」类不能直接承诺的项

| # | 不能承诺 | 说明 |
|---|---|---|
| Q1 | **变速段音高自然** | 默认 `pitch=0` 是变调变速；这是「设计行为」而非缺陷，但若产品文案说「保持音高」则与实现不符 |
| Q2 | **wsola 的保音高质量** | `timeremap` link **只**用 rbpitch（`:308`）。WSOLA 是 `producer_timewarp` 的选项（`producer_timewarp.c:186-195`），且 WSOLA 在速度比 1 附近并非靠自身拉伸（`filter_wsola.cpp:174` 的 `rate`），两者不可混为一谈 |
| Q3 | **极端速度下无金属感/无相位跳变** | RubberBand 在 `pitchscale` 极端时切到 HighConsistency/Smooth（`filter_rbpitch.cpp:127-141`），本质是保音质优先的折中，非无损 |
| Q4 | **倒放 + 保音高的质量** | 反转音频逐帧送入实时拉伸器，跨帧状态在反向流上复用（`:283-286` + rbpitch 状态机），无任何质量约束 |
| Q5 | **双拉伸不劣化** | 若源是 `timewarp` producer（它自身已做 `1/|speed|` 的 pitchscale 与 `fabs(speed)` 频率缩放，`producer_timewarp.c:123`、`:197-200`），再叠 `timeremap` 的 `pitch=1`，就是两级有状态拉伸串联，延迟与伪影叠加 |
| Q6 | **混合帧无重影** | 等权平均在高速段等效于「长快门」；无运动补偿，>2x 时重影不可避免 |
| Q7 | **`pitch=1` 时音频与视频的样本级相位** | 见 U5；link 的 4 采样容差只覆盖自身拼接，不覆盖滤镜延迟 |

---

## 9. 逐项判断清单（对应需求条目）

1. **积分**：`speed_map` 是逐帧左黎曼和 + 增量缓存；前跳超距或有 `in` 变化时从 `in` 重算；曲线变化才失效缓存。精度误差 ≤ 半帧/线性段，**不影响 A/V 相对同步**，只影响内容位置。`time_map` 不积分，直接取动画值，导数即速度。
2. **视频采样**：`nearest` = `floor(t*source_fps)` 的整帧（不是最近邻的四舍五入）；`blend` = 窗口中至多 10 帧的**等权**平均，与重叠时长无关，>10 帧截断，1 帧时退回 nearest，非 `rgba64` 的 10/16 位格式按字节平均（结果不可用），rgba64 的 size 元数据被减半，blend 不处理 alpha。
3. **音频获取**：按 `sample_count = link_samples·|s|` 从**同一窗口**逐源帧拷贝，起点精确到样本；±4 采样接缝修正；`|s|∉[0.1,10]` → 等长静音；倒放整体反转。
4. **变速**：音频靠**频率重标定 + 下游重采样**实现（默认变调）；`pitch=1` 才引入 RubberBand 保音高，且失败静默降级为变调。
5. **倒放**：音频窗口不变、方向反转；视频仍向大帧号读、显示 `floor(t)`；两者锚点位于窗口两端，`|s|>1` 时相位差可达整帧。
6. **源端点**：`in` 之前从 `in` 向回减积分（曲线的首段速度会外推，不是假设 1x）；负源时间被 producer 钳到 0 帧，音频按负位置算偏移，通常表现为整段冻结在素材首帧，帧缺失时才降级为 `shrink`/静音；终点完全交给源 producer 的 eof；fps 不等时 `in` 的起点会按 `source_fps/link_fps` 缩放。
7. **同步保证**：G1–G11（时长严格、同一时间基准、窗口一致、≤1 源帧错位、≤4 采样接缝、1x 直通、0 速定格+静音、超阈值静音、seek 重算、映射互斥、FRC 不变调）。
8. **不能保证**：U1–U18 与 Q1–Q7，其中最关键的五条是：**相对关键帧依赖宿主设 `length`**（U1）、**`pitch=1` 可能静默失效**（U4）、**端点无显式补边**（U8）、**倒放相位不保证**（U7）、**音频路径无锁且无测试**（U10/U18）。

---

## 10. 附：建议的最小验证集（若要把上述结论变成可执行契约）

- 固定 `in=0`、`length=N`，用 1→3 线性 `speed_map` 校验 `source_time` 序列 = 左黎曼和，并断言音频样本数与 `link_sample_count` 恒等（G1）。
- 0.5x / 2x / 3x / 10x / 10.5x / 0 / -1x / -2x 各跑 1 秒，断言：静音阈值（G8）、倒放锚点（U7）、blend 帧数上限（U6）。
- `time_map` 用一个离散跳变，断言不会触发大规模解码且不产生 `shrink`（U9）。
- 采样率 48k→44.1k 与 30fps 源 + 25fps 链，断言最终频率等于请求值（U12）与 FRC 不变调（G11）。
- 并行消费者（`real_time` 多线程）下跑长序列，断言 `_continuity_sample` 竞争（U10）。
- 端点：`in>0` 的片段、`in` 前 1 秒、`out` 后 1 秒，记录实际返回的帧类型（冻结/黑场）以确认 U8 的运行时语义。
