# CLAUDE.md

XIAO ESP32C6 定时锁固件。单文件 `espc6-lock.ino`，约 650 行，无其他源文件。
面向人的说明在 `README.md`，这里只记**约定**和**踩过的坑**。

## 编译 / 烧录

```bash
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$CLI" compile --fqbn esp32:esp32:XIAO_ESP32C6 --warnings all /c/Users/14919/Desktop/espc6-lock
"$CLI" upload  -p COM4 --fqbn esp32:esp32:XIAO_ESP32C6 /c/Users/14919/Desktop/espc6-lock
```

改动后**必须编译一遍**再交付，并报出 Flash / RAM 数字。已验证的环境：

| | 版本 |
|---|---|
| esp32 core | 3.3.11 |
| U8g2 | 2.36.19（`C:\Users\14919\Documents\Arduino\libraries\U8g2`） |
| 编译基线 | 502894 字节 (38%) Flash / 17020 字节 (5%) RAM |

## 不要动的东西

用户明确要求过：**不要改显示驱动**，动画只能在现有 U8g2 调用上实现。
以下配置是硬件绑定的，改之前先问：

- 引脚：`PIN_I2C_SDA 22` / `PIN_I2C_SCL 23` / `PIN_BTN1..4 = 0,1,2,21` / `PIN_SERVO 19`
- 显示构造：`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`，第 3、4 个参数必须显式传 SCL/SDA
  （本项目 I2C 不在芯片默认脚上，留空屏幕完全不亮）
- 第 4 行（`TRACK_Y`）那条 1px 进度条，用户要求保留
- 状态语义：计时状态不持久化，上电回开锁位（fail-open）

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

本项目的对策：`UNLOCK_TIMEOUT` / `UNLOCK_MANUAL` 用 `#define` 而不是 `enum`，
就是为了让 `enterIdle(uint32_t, bool)` 的原型能正常生成。

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

`drawBigTime()` 用 `u8g2.getStrWidth()` 量实际宽度再居中，不要按字符数估算——
换任何字体都不会偏。

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
- 下面这些形状全是**算出来的，没人看过**：脚丫点阵、羽毛、四条 16px 分段的
  版面布局。真机上如果糊成一团或位置偏移，属正常，按实际观感调。
- 待机时第 4 段是空的（用户标记「待定」）。
