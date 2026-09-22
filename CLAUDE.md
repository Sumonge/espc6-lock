# CLAUDE.md

XIAO ESP32C6 定时锁固件。**多文件** `.ino`，`espc6-lock.ino` 是主文件，
其余按字母序拼接（见「多文件」一节）。
面向人的说明在 `README.md`，这里只记**约定**和**踩过的坑**。

## 编译 / 烧录

```bash
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$CLI" compile --fqbn esp32:esp32:XIAO_ESP32C6 --warnings all /c/Users/14919/Desktop/espc6-lock
"$CLI" upload  -p COM5 --fqbn esp32:esp32:XIAO_ESP32C6 /c/Users/14919/Desktop/espc6-lock
```

改动后**必须编译一遍**再交付，并报出 Flash / RAM 数字。已验证的环境：

| | 版本 |
|---|---|
| esp32 core | 3.3.11 |
| U8g2 | 2.36.19（`C:\Users\14919\Documents\Arduino\libraries\U8g2`） |
| 编译基线 | 524458 字节 (40%) Flash / 17084 字节 (5%) RAM —— **默认分区** |

基线会随功能走，改完就更新上表。切到 `huge_app` 之后（计划 Step 4）报数要
**同时给分区名和余量绝对值**——`huge_app` 下百分比会突然变小，只报百分比
会看不出真实变化：

```bash
# 切分区之后编译要加这一项，否则会莫名装不下
"$CLI" compile --fqbn esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app ...
```

## 不要动的东西

用户明确要求过：**不要改显示驱动**，动画只能在现有 U8g2 调用上实现。
以下配置是硬件绑定的，改之前先问：

- 引脚：`PIN_I2C_SDA 22` / `PIN_I2C_SCL 23` / `PIN_BTN1..4 = 0,1,2,21` / `PIN_SERVO 19`
  —— **注意 PCB 版计划把 K3 从 D2/GPIO2 挪到 D6/GPIO16**（D2 让给电池分压，
  见 `hardware/DESIGN.md` §5）。固件**还没改**。真要改时先想清楚面包板和
  PCB 怎么办：改了 `PIN_BTN3` 面包板就废了，两者要么共用一套、要么留编译开关
- 显示构造：`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`，第 3、4 个参数必须显式传 SCL/SDA
  （本项目 I2C 不在芯片默认脚上，留空屏幕完全不亮）
- 第 4 行那条进度条，用户要求保留（原为 1px 细线，2026-09-22 按要求改成
  两端圆角的胶囊样式，见下方「坑 6」和 `drawTrackCapsule()`）
- 解锁提示画面的开锁动画（`drawLock()`）：锁梁绕左腿根部转 **90°** 躺平
  指向左，是用户指定的动作。`LOCK_OPEN_DEG` **不是风格旋钮**——小一点不叫
  水平，大一点转到锁体下面去。要改观感调 `LOCK_ANIM_MS`。
  它转到底时锁梁会横穿锁体内部，这是铰链没拔出来的必然结果，别去"修"。
- 状态语义：计时状态不持久化，上电回开锁位（fail-open）

## 桌面版面

用户 2026-09-22 拍板（当时给了三种，选了中间这种）：

```
y0 ..y15    状态栏   锁状态 / 信号 / 电量
y16..y47    图标行   一行三格，每格 42x32，三等分
y48..y63    留空
```

16/32/16 上下对称，图标行是居中的。

**两个后果要记住：**

1. **桌面最多三个应用。** `APP_COUNT` 目前是 3，已顶格。第 4 个应用必须
   先改版面（翻页，或退回两行），**不是把 3 改成 4** —— 那样第 4 格会画进
   y48 的空白区，而且第 4 格绕回来时会盖住第 1 格。
2. **状态栏里只有「锁状态」是真的。** 信号要等 Step 7 的 WiFi；电量要等
   PCB —— **面包板这一版上根本没有空闲的 ADC 脚**：C6 的 ADC 只有
   GPIO0~GPIO6，而 GPIO0/1/2 是 K1/K2/K3、GPIO3 是模块内部射频开关
   （且不在排针上）、GPIO4/5/6 没引到排针上。
   **PCB 版已经把 K3 挪到 D6、把 GPIO2 让给电池分压**（`hardware/DESIGN.md`
   §5），那块板子做出来才有真数据。
   所以后两项画的是**空心** = 「还没数」，**不是**「信号为零、电池没电」。
   宁可看着像占位，也不要画成实心让人以为电量真是 0。

应用标签是**两个汉字**（`定时` / `网络` / `设置`，实测 23/24/23px）。原来的
「定时锁」是 35px，塞是塞得下，但三个标签长短不一，三等分的网格看上去就不齐。
这个标签同时是心跳日志里的应用名，改它两处都会跟着变。

**K1 向右、K2 向左**（用户 2026-09-22 指定）。这和常见的排法相反——一般会
把左边的键当"上一个"。别看着别扭就顺手换回去：光标往哪边走是手感问题，
不是对错问题。

改任何几何之前：`python tools/desktop_sim.py`。

## 设备名

`LM-XXX-XXX`（`DEVNAME_*` 宏在主 .ino 的配置区）。**上电随机起一次，写进
NVS 固定下来**，不是每次开机换一个——它要用来在手机/路由器列表里认出这块
板子。

- 生成用 `esp_random()`（硬件随机源），**不用** Arduino 的 `random()`。后者
  在这个 core 里也是 `esp_random` 兜底，但那是实现细节，写明白意图才不会
  换 core 版本时悄悄变成种子固定的伪随机。
- 字符集 36 个（0-9 + A-Z，**无小写**，用户要求）。6 位 ≈ 22 亿种。
- 定义在**主 .ino** 而不是 `app_net.ino`：它是设备属性（和 `FW_VERSION`
  同类），将来 BLE 广播名（Step 5）和 mDNS 主机名（Step 7）都要用。那时候
  服务层在 `net_lock.ino`，排在 `app_net.ino` **后面**——从那个位置回头取
  一个应用文件里的 `static` 是取不到的，所以必须放前面。
- `devNameFill()` 手写循环而不是 `snprintf`：`snprintf` 返回的是"本该写多
  长"，一旦超出 cap 就会在长度变量上越过缓冲区继续累加。边界条件只有一个
  更好审。逻辑在主机上单跑过（8 次格式 + cap=1..12 逐个截断）。
- `devNameInit()` 放在 `servoUnlock()` **之后**：首次开机要写 NVS，那点
  耗时不能挡在"上电即开锁"前面。同一个理由让将来的 `bleInit()` 也排最后。
- 存不进去**不能**卡住启动：名字留在 RAM 里照样能用，只是下次开机换一个。
- 重新起名要**擦整块 Flash**（Arduino IDE 的 `Erase All Flash Before Sketch
  Upload`，或 `esptool.py erase_flash`）。`arduino-cli` **没有** erase 子命令。

## 坑

### 1. `drawXBMP` 是「低位在左」，不是 MSB

**这是本项目最贵的一个坑**，花了三轮才定位。U8g2 2.36.19
`src/clib/u8g2_bitmap.c` 里 `u8g2_DrawHXBMP()`：

```c
mask = 1;
while(len > 0) { ... x++; mask <<= 1; if (mask == 0) { mask = 1; b++; } len--; }
```

从 bit0 起步往左移，所以**每个字节的 bit0 才是这一行最左边的像素**。
注意同文件里 `u8g2_DrawHXBM()`（`drawXBM` 用的）在这个版本也是 `mask = 1`，
两者都是 LSB-first。

按 MSB 打包会同时出两个错：

1. 每个字节内部 8 个像素左右镜像；
2. **超出的位根本不会被读**——`u8g2_DrawXBMP` 每行只数 `w` 位，宽 14 的图
   每行只读 14 位，第二个字节的高 2 位永远读不到。所以那些像素是**静默丢弃**的。

表现就是「脚丫只显示一半」，而且看起来像形状或坐标问题，极易误诊。

**打包规则**（`art` 是 `#`/`.` 组成的字符串）：

```python
b0 = sum(1 << k for k in range(8)      if art[k]   == '#')
b1 = sum(1 << k for k in range(8, w)   if art[k]   == '#')
```

等价于「按 MSB 打包后把每个字节 bit-reverse」。等价性自己验，别背结论。

**改点阵的流程**（别手写十六进制）：

1. 用 ASCII art 写形状（`#` / `.`，宽度 = `FOOT_W`）
2. 脚本从 art 生成十六进制**和行尾注释**——注释由 art 派生，不要手写，
   否则会和字节对不上
3. **反向解码一遍**，确认十六进制读回来等于 art（往返校验）
4. 有动画的话跑一遍逐档扫描：进度 0/5/…/100，检查点阵与羽毛无重叠、
   无越界（x 在 0~127、y 在 0~63）

`ICON_FOOT` 那 40 多行注释就是按第 2 条生成的。

### 2. I2C 地址探测会给出假 NACK —— 不要探测

实测这套硬件上 `Wire.endTransmission()` 返回 false NACK，探测说 0x3C 无应答，
但 U8g2 照样能往同一块屏幕写。之前信了这个假阴性自动切到不存在的 0x3D，
结果全黑。

**地址写死 0x3C，不做探测。** 屏幕在不在以「画面上不上得来」为准。
换 0x3D 模块时打开 `u8g2.setI2CAddress(0x3D << 1)`（收的是左移一位后的值）。

### 3. Arduino 自动原型 + 后置类型 = 编译错

Arduino 会给 `.ino` 里的函数自动生成原型并插到文件靠前的位置。若某个函数的
**参数或返回类型是本文件后面才定义的**，生成的原型会报
`error: 'X' has not been declared`。

**现在的正解是 `app.h`**：所有共享类型放那一个头文件，并在 `espc6-lock.ino`
的**最顶部**第一个 include——原型插在「第一条 include 之后」，类型在那个点
已经声明完了，这个坑就不存在了。

`UNLOCK_TIMEOUT` / `UNLOCK_MANUAL` 至今仍是 `#define` 而不是 `enum`，那是
单文件时代的遗留对策，改成 enum 放进 `app.h` 也可以，只是没坏就别动。
真正还在生效的是「多文件」那三条硬规则，别只记这一条。

### 4. 中文必须 `drawUTF8()`

`drawStr()` 画中文会乱码。本文件也必须存为 **UTF-8**（Arduino IDE 默认如此）。

已验证存在的中文字体（在 `U8g2/src/clib/u8g2.h` 里搜 `wqy`，字体名都声明在那儿）：

| 名字 | 大小 |
|---|---|
| `u8g2_font_wqy12_t_chinese1` | 9 KB 常用字，最省 Flash |
| `u8g2_font_wqy12_t_chinese3` | 14 KB 收字更多 |
| `u8g2_font_wqy12_t_gb2312` | 198 KB 完整 GB2312 ← 当前用的 |

屏幕上一共二十来个汉字，用 gb2312 属于大炮打蚊子，但 4MB Flash 放得下，
不值得为省空间去赌某个字不在精简字库里。

注意 U8g2 的头文件在 `src/clib/` 而不是 `src/`，且**没有** `u8g2_fonts.h` 这个文件。

### 5. 大数字居中要量宽度

`drawBigTime()` 量实际宽度再居中，不要按字符数估算——换任何字体都不会偏。

**但要用对量宽函数**，否则「量了宽度」反而是错的。分界线是串里有没有汉字，
见坑 7。`drawBigTime()` 的 buf 是 snprintf 出来的纯 ASCII，所以它用
`getStrWidth()` 是对的，不是漏改。

### 6. 胶囊形状不能用 `drawRBox` / `drawRFrame`

想画**两端是完整半圆**的胶囊（也就是 `r = h/2`）时，不能用这两个函数。
它们把直边长度算成 `w-2r` / `h-2r`，而且**只在长度 >= 3 时才画那两条直边**
（`src/clib/u8g2_box.c` 的 `u8g2_DrawRBox` / `u8g2_DrawRFrame`）。
胶囊恰好让 `h-2r = 0`，直边整段被跳过，**两端反而各开一个缺口**。

对策：两端用 `drawCircle()` 的象限位标志自己拼半圆，中间补两条 `drawHLine()`。
四个象限是 `0x01~0x08` 的位标志（见 `u8g2.h`），可以 OR：

```c
u8g2.drawCircle(cx, cy, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);  // 左半圆
```

实心填充则用 `drawDisc()` + `drawBox()` 拼，别用 `drawRBox()`。
第 4 行的进度条就是这么画的（`drawTrackCapsule()` / `drawFillCapsule()`）。

### 7. 量中文宽度必须 `getUTF8Width()`，`getStrWidth()` 给的是垃圾值

**凡是配 `drawUTF8()` 用的宽度，一律 `getUTF8Width()`。**
`getStrWidth()` 是 ASCII-only 的：

```c
// u8g2_font.c:1524
u8g2_uint_t u8g2_GetStrWidth(u8g2_t *u8g2, const char *s) {
  u8g2->u8x8.next_cb = u8x8_ascii_next;
  return u8g2_string_width(u8g2, s);
}
// u8g2_font.c:1548
u8g2_uint_t u8g2_GetUTF8Width(u8g2_t *u8g2, const char *str) {
  u8g2->u8x8.next_cb = u8x8_utf8_next;
  return u8g2_string_width(u8g2, str);
}
```

而 `u8x8_ascii_next()`（`u8x8_8x8.c`）是**原样返回每个字节**：

```c
uint16_t u8x8_ascii_next(u8x8_t *u8x8, uint8_t b) {
  if ( b == 0 || b == '\n' ) return 0x0ffff;
  return b;                      // ← 字节即字形编码
}
```

于是 UTF-8 的「定时锁」（9 字节）被当成 9 个独立字形编码 0xE5/0xAE/0x9A/…
去查表，全都查不到。**查不到时 `u8g2_GetGlyphHorizontalProperties()` 直接
return，压根不写它的出参**，`u8g2_string_width` 就拿着一堆**未初始化的栈变量**
继续累加——不只是数值错，是 UB，每次都可能不一样。

实测（`tools/fontcheck`，同一份 U8g2 源码在主机上编的）：

| 串 | `getStrWidth` | `getUTF8Width` |
|---|---|---|
| `"字"` | **0** | 11 |
| `"定时锁"` | **11** | 35 |
| `"KHW 定时锁"` | **40** | 64 |

按 11 给「定时锁」在 42px 格子里居中 → 起点 x+15，实际墨迹 35px → **压到右
邻格**。这类错在屏幕上不显眼（就是"标签有点偏"），但网格铺满时一眼就看出
串格了。

已按此改过的调用点：`deskDraw()` 的标签、`drawSplash()` 的两行、
`setup()` 里那条 `[font]` 探针（探针尤其危险——它若打 0，会让人以为
「汉字宽是 0，版面很宽松」，一个报错的探针比没有探针更坏）。

## 多文件

`.ino` 的拼接规则：**主文件（与文件夹同名）排在最前，其余按字母序**，
全部合成**一个编译单元**。本项目的实际顺序：

```
espc6-lock.ino → app_desktop.ino → app_lock.ino → app_net.ino
              → app_settings.ino → icons.ino → net_lock.ino → ui_kit.ino
```

（`app_net.ino` 是「网络」应用，`net_lock.ino` 还没建、将来是 BLE/WiFi 的
**服务层**。两个名字像，别搞混：`app_*` 是应用，`net_*` 是服务。）

后果是：宏和 `static` 名字**只对文本上排在它后面的文件可见**。主 `.ino` 排在
最前，所以它**看不见**任何后面文件里的东西，除非在 `app.h` 里声明。

三条硬规则：

1. **共享类型只放 `app.h`，并在主 `.ino` 最顶部第一个 include。**（见坑 3）
2. **函数签名只用内建类型或 `app.h` 里的类型。** 这样即使 arduino-cli 的原型
   插入点算错也伤不到我们——所以 `appOpen()` 收的是下标 `uint8_t`，不是
   `const App *`。
3. **所有 `static` 函数/变量必须带应用前缀**（`lock*` / `desk*` / `net*` /
   `set*`）。共用一个命名空间，两个文件各写一个 `static void enter()` 会直接
   重定义报错。**这是强制要求，不是风格。**

另外两条容易忘的：

- 常量数组要在 `app.h` 里 `extern` 声明 + 在 `.ino` 里**不带 `static`** 定义。
  写成 `static` 会得到 `'X' was declared 'extern' and later 'static'`。
  不带 `static` 的 const 数组进 `.rodata`（Flash），不占 RAM。
- `.ino` 而不是 `.cpp` 是有意的：`.cpp` 会把 `u8g2` 和配置区的宏挡在外面。
- **别在 sketch 目录下建子目录放 `.c/.cpp`**（除非叫 `src/`）。Arduino 会把
  某些子目录当成独立 sketch。`tools/fontcheck/` 目前安全——已验证它不会被
  编进固件，多出来的 `main()` 没造成重复定义。

## 几何验证工具

助手**看不到屏幕**，所以版面的每一步都要先用工具量出来。两个都在 `tools/`：

| 工具 | 作用 |
|---|---|
| `python tools/desktop_sim.py` | 桌面几何自检（越界 / 图标与标签重叠 / 标签装不装得下 / **状态栏三项挤不挤**）。辅助模式：`gen` 生成点阵十六进制、`full` 打印整屏 ASCII、`bar` 并排看状态栏的几种取值 |
| `bash tools/fontcheck/run.sh` | **真字模**量宽与墨迹盒。在主机上编译 U8g2 自己的 `u8g2_font.c`，调真正的 `u8g2_GetUTF8Width()` / `DrawUTF8()` |

`fontcheck` 为什么不是 Python 复刻：复刻一个字形位解码器再去「验证」版面，
等于用一把可能错的尺子量东西——尺子错了，「量出来刚好合适」比不量更危险。
这里用的是和固件**逐字节同一份**源码，只换编译器。

已实测的字模数字（`u8g2_font_wqy12_t_gb2312`）：

```
ascent=8  descent=-2  max_char_height=13（整个字体最高的字形，别拿它留余量）
汉字墨迹   相对基线 -10..0   高 11   单字宽 11，步进约 12
ASCII 大写/数字  相对基线 -8..-1   高 8
带下伸部的字母(p/g)  到 +1
```

## 代码约定

- **注释用中文**，且写**为什么**不写**是什么**。凡是「这里看着可以简化但不行」
  的地方，把不能简化的原因写清楚（例：`BTN_REPEAT_MS` 固定不加速、
  连发步进与间隔只能有一个因子变；`adjustKey` 单击为什么要挂起一拍）。
- 反直觉的结论要把**证据**钉在注释里（`ICON_FOOT` 那段直接引了 U8g2 源码）。
- 配置参数集中在文件顶部「配置区」，需要调的数值不要散落在逻辑里。
- 串口日志是调试的主要手段（看不到屏幕时只能靠它）。心跳日志别删。

## Git 约定

- **提交信息用中文**，首行概括、正文写清动机与影响。
- **署名只有用户自己**（`Sumonge <1491989009@qq.com>`），
  **不要加 `Co-Authored-By: Claude` 之类的协作者行**。用户明确要求过。
- 分支 `main`，remote `origin`（SSH）。提交前先编译确认能过。
- `.gitattributes` 把换行钉成 LF——本机 `core.autocrlf=true`，不钉会有
  「文件没动却显示已修改」的假改动。

## 尚未验证

- **`SERVO_US_UNLOCK` / `SERVO_US_LOCK`（1000 / 2000 µs）从没在实机上标定过**，
  是按常见舵机给的估值。装上拨片后必须实测行程来调。
- ~~下面这些形状全是算出来的，没人看过~~ —— 2026-09-22 已作废：屏幕点亮、
  显示正常，脚丫点阵 / 羽毛 / 四条 16px 分段的版面用户均已看过。
  2026-09-22 用户手动烧录后实测通过：胶囊进度条 + 开锁动画均功能正常。
  **改 UI 仍要照上面的模拟器流程先验一遍**——实机验证的是「已烧进去的那版」，
  不是「你正在写的那版」。
- 待机时第 4 段是空的（用户标记「待定」）。
- ~~桌面改造还没烧过机~~ —— 2026-09-22 已作废：拆分 + 桌面 + 设置应用那一版
  用户烧进去看过，屏幕正常。**状态栏 + 一行三格 + 网络应用 + 设备名这一版
  还没烧过**，烧上去要用户看过才算过。设备名的 NVS 读写尤其要实机确认——
  主机上验的只是拼名字那段逻辑，`Preferences` 部分一次都没跑过。
- 网络应用（`app_net.ino`）只有设备名是真数据，「无线 / 配网」两行是占位，
  显示的「未接入」是实话不是 bug。
