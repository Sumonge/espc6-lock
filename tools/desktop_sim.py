# -*- coding: utf-8 -*-
"""
桌面（状态栏 + 1x3 图标行）的逐像素模拟。

和 tools/ 下别的脚本一样，这是**助手看不到屏幕**时的唯一证据。改动桌面
几何之前先跑一遍，别直接烧。

版面（用户 2026-09-22 拍板）：
    y0..y15    状态栏：锁状态 / 信号 / 电量
    y16..y47   图标行，三个格子各 42x32，三等分
    y48..y63   **留空**

用法：
    python tools/desktop_sim.py            # 三种选中高亮并排对比 + 几何自检
    python tools/desktop_sim.py full       # 整屏预览
    python tools/desktop_sim.py bar        # 状态栏的几种取值并排对比
    python tools/desktop_sim.py gen        # 生成 icons.ino 用的 C 数组

字形不是真的：汉字按"12px 方块、墨迹占满整个方块、基线在方块底边"近似，
只用来验**位置和重叠**（而且是按最坏情况验），不验字形。真实字体是
u8g2_font_wqy12_t_gb2312，**宽度用 tools/fontcheck 实测**（见 LABEL_TRUE_W），
上下沿要上实机确认一次。
"""
import sys

W, H = 128, 64

# ------------------------------------------------------------------
#  图标：16x16 点阵，ASCII art（'#' 亮，'.' 空）
#  改形状就改这里，然后跑 `python tools/desktop_sim.py gen` 拿 C 数组。
# ------------------------------------------------------------------

ART_LOCK = """
....########....
...##......##...
...##......##...
...##......##...
...##......##...
...##......##...
...##......##...
................
.##############.
.##############.
.#####....#####.
.#####....#####.
.######..######.
.######..######.
.##############.
.##############.
""".strip('\n').split('\n')

ART_GEAR = """
......####......
......####......
......####......
...##########...
..############..
..###......###..
#####......#####
#####......#####
#####......#####
#####......#####
..###......###..
..############..
...##########...
......####......
......####......
......####......
""".strip('\n').split('\n')


def _byte(chunk):
    """
    8 个字符 → 一个字节，**bit0 是最左边那个像素**。

    这是本项目最贵的一个坑（CLAUDE.md 坑 1）：U8g2 的 drawXBMP 从 bit0
    起步往左移，所以每个字节的 bit0 才是这一行最左边的像素。

    ⚠️ 移位量必须是**字节内的位号**（enumerate 从 0 起），不是整行的绝对
    列号。写成 `1 << k for k in range(8,16)` 会让第二个字节置到第 8~15 位，
    在 C 里存进 uint8_t 时被截断成 0 —— 整行右半边**静默消失**，看起来像
    形状问题。切片的写法让这个错误不可能发生，别改成 range(8,16)。
    """
    return sum(1 << k for k, ch in enumerate(chunk) if ch == '#')


def pack(art):
    """ASCII art → U8g2 的点阵字节。自带往返校验，出错在打包处就炸，不留到渲染。"""
    out = []
    for row in art:
        assert len(row) == 16, "每行必须恰好 16 个字符，这行是 %d：%r" % (len(row), row)
        out += [_byte(row[0:8]), _byte(row[8:16])]
    assert unpack(out) == art, "往返校验失败：打包结果读回来和 art 对不上"
    return out


def unpack(data, w=16, h=16):
    """字节 → ASCII art。**反向解码**，用来做往返校验 —— 打包完必须能读回来。"""
    rows = []
    for y in range(h):
        b0, b1 = data[2 * y], data[2 * y + 1]
        rows.append(''.join('#' if (b0 >> k) & 1 else '.' for k in range(8)) +
                    ''.join('#' if (b1 >> k) & 1 else '.' for k in range(8)))
    return rows


def gen_c_array(name, art):
    """
    生成 C 数组，行尾注释由 art 派生（不许手写，否则会和字节对不上）。

    **不带 static**：这些数组要在 app.h 里 extern 声明给主 .ino 用，带 static
    会得到 `'X' was declared 'extern' and later 'static'`（CLAUDE.md「多文件」）。
    """
    data = pack(art)
    assert unpack(data) == art, "%s 往返校验失败" % name     # 反向解码必须等于原 art
    lines = ["const uint8_t %s[ICON_BYTES] = {" % name]
    for y in range(16):
        lines.append("  0x%02X, 0x%02X,  //  %s" % (data[2 * y], data[2 * y + 1], art[y]))
    lines.append("};")
    return '\n'.join(lines)


ART_NET = """
......###.......
....#######.....
...#.##.##.#....
..#.##...##.#...
.#..#.....#..#..
.#..#.....#..#..
.#..#.....#..#..
.#############..
.#..#.....#..#..
.#..#.....#..#..
.#..#.....#..#..
..#.##...##.#...
...#.##.##.#....
....#######.....
......###.......
................
""".strip('\n').split('\n')

ICON_LOCK = pack(ART_LOCK)
ICON_GEAR = pack(ART_GEAR)
ICON_NET  = pack(ART_NET)


# ------------------------------------------------------------------
#  汉字的真实墨迹尺寸 —— 实测，不是估算
#
#  来源：tools/fontcheck/bbox.c，在**主机上**编译 U8g2 自己的 u8g2_font.c
#  （与固件逐字节同一份源码），把字真画进 u8g2 的 1024 字节缓冲区，再扫回来
#  求包围盒。`bash tools/fontcheck/run.sh` 可复现。
#
#     汉字（定时锁 / 设置 / 关于本机…）   相对基线 -10..0   高 11
#     ASCII 大写 / 数字                  相对基线 -8..-1   高 8
#     带下伸部的字母（p、g）              到 +1
#
#  注意 u8g2_GetMaxCharHeight() 报的是 13 —— 那是**整个字体里最高那个字形**，
#  不是常用字。按 13 去留版面余量会白吃一行，按 11 才是对的。
# ------------------------------------------------------------------
CJK_INK_H = 11         # 汉字墨迹高度（基线往上 10 行 + 基线那 1 行）

# 标签的真实像素宽度。key 是应用名，与 app 表里的 name 一字不差。
# 用 GetUTF8Width 量的 —— 见 CLAUDE.md 坑 7：用 GetStrWidth 量中文会得到
# 垃圾值（"定时锁" 真宽 35，GetStrWidth 给 11），照它居中会把标签推出格子。
LABEL_TRUE_W = {
    "定时":   23,
    "网络":   24,
    "设置":   23,
    "定时锁": 35,
    "已锁":   24,   # 状态栏，不是桌面标签，但走同一个近似
    "开锁":   23,
}


def label_width(s):
    """标签墨迹宽度。表里有的用实测值，没有的按 12px/字 近似。"""
    return LABEL_TRUE_W.get(s, len(s) * 12)


# ------------------------------------------------------------------
#  画布（照 U8g2 的算法）
# ------------------------------------------------------------------
class Canvas:
    def __init__(self):
        self.b = [[0] * W for _ in range(H)]
        self.color = 1

    def px(self, x, y):
        if 0 <= x < W and 0 <= y < H:
            self.b[y][x] = self.color

    def hline(self, x, y, w):
        for i in range(w):
            self.px(x + i, y)

    def vline(self, x, y, h):
        for i in range(h):
            self.px(x, y + i)

    def box(self, x, y, w, h):
        for j in range(h):
            self.hline(x, y + j, w)

    def circle(self, x0, y0, rad, opt=15):
        UR, UL, LL, LR = 1, 2, 4, 8

        def sec(x, y):
            if opt & UR: self.px(x0 + x, y0 - y); self.px(x0 + y, y0 - x)
            if opt & UL: self.px(x0 - x, y0 - y); self.px(x0 - y, y0 - x)
            if opt & LR: self.px(x0 + x, y0 + y); self.px(x0 + y, y0 + x)
            if opt & LL: self.px(x0 - x, y0 + y); self.px(x0 - y, y0 + x)

        f, ddx, ddy, x, y = 1 - rad, 1, -2 * rad, 0, rad
        sec(x, y)
        while x < y:
            if f >= 0:
                y -= 1; ddy += 2; f += ddy
            x += 1; ddx += 2; f += ddx
            sec(x, y)

    def rframe(self, x, y, w, h, r):
        """
        u8g2_DrawRFrame 的等价实现。

        ⚠️ 含它那个「直边长度 >= 3 才画」的判断（u8g2_box.c）。照抄这个判断
        是为了让模拟器和真机会**同样地画错** —— 胶囊形状（r = h/2）下直边整段
        被跳过、两端反而各开一个缺口（CLAUDE.md 坑 6）。这个 quirk 要能从
        模拟器里看见，不能被"修好"。
        """
        xl, yu = x + r, y + r
        xr, yl = x + w - r - 1, y + h - r - 1
        self.circle(xl, yu, r, 2); self.circle(xr, yu, r, 1)   # UL, UR
        self.circle(xl, yl, r, 4); self.circle(xr, yl, r, 8)   # LL, LR
        ww, hh = w - 2 * r, h - 2 * r
        xl += 1; yu += 1
        if ww >= 3:
            ww -= 2; h -= 1
            self.hline(xl, y, ww); self.hline(xl, y + h, ww)
        if hh >= 3:
            hh -= 2; w -= 1
            self.vline(x, yu, hh); self.vline(x + w, yu, hh)

    def frame(self, x, y, w, h):
        """
        u8g2_DrawFrame 的等价实现：1px 直角空心框。

        状态栏的信号格和电池壳用这个，**不用 rframe** —— 圆角版在
        w-2r < 3 时会跳过直边（CLAUDE.md 坑 6），而这里最小的一格只有
        4x4，恰好踩中。直角版没有这个判断，多大都画得对。
        """
        self.hline(x, y, w); self.hline(x, y + h - 1, w)
        self.vline(x, y, h); self.vline(x + w - 1, y, h)

    def xbm(self, x, y, w, h, data):
        """照 u8g2_DrawXBMP：每行只数 w 位，从 bit0 起步。"""
        for j in range(h):
            b0, b1 = data[2 * j], data[2 * j + 1]
            for k in range(w):
                byte = b0 if k < 8 else b1
                if (byte >> (k % 8)) & 1:
                    self.px(x + k, y + j)

    def label(self, x, baseline, s, cjk_w=12, cjk_h=CJK_INK_H, solid=False):
        """
        汉字用方块近似：**基线在方块底边**，即墨迹占 [baseline-10, baseline]。

        高度 11 不是估的：tools/fontcheck 把字真画进 u8g2 缓冲区再扫回来，
        实测汉字墨迹相对基线是 -10..0（高 11），ASCII 是 -8..-1，带下伸部
        的字母（如 "编译 Sep..." 里的 p）到 +1。**max_char_height=13 是整个
        字体里最高那个字形，不代表常用字** —— 按 13 留余量会白白吃掉一行。

        solid=False 画空框（表示"字在这儿"），solid=True 填实（反白时当作
        挖空的字）。用当前 color 画，所以能用来挖。
        """
        for i, ch in enumerate(s):
            bx = x + i * cjk_w
            if solid:
                self.box(bx + 1, baseline - cjk_h + 2, cjk_w - 2, cjk_h - 2)
            else:
                top, bot = baseline - cjk_h + 1, baseline
                self.hline(bx, top, cjk_w); self.hline(bx, bot, cjk_w)
                self.vline(bx, top, cjk_h); self.vline(bx + cjk_w - 1, top, cjk_h)


# ------------------------------------------------------------------
#  版面 —— 状态栏 16px + 图标行 32px（1 行 x 3 列），第 4 段留空
#
#  这是用户 2026-09-22 拍板的方案（当时给了 A/B/C 三种，选了 B）。
#
#      y0..y15    状态栏：锁状态 / 信号 / 电量
#      y16..y47   图标行，三个格子各 42x32
#      y48..y63   **留空**
#
#  ⚠️ 1x3 的网格意味着桌面**最多只放得下 3 个应用**。第 4 个应用必须改版面
#  （翻页，或者回到两行）—— 不是加个循环下标就完事。别忘。
#
#  格子内部还是 32px 那套（和两行版完全一样，只是整体下移 16px）：
#      y+0        选中框上边线
#      y+1..y+16  图标（16px）
#      y+17,y+18  空行  <- 图标与标签之间的呼吸缝，实测 2 行
#      y+19..y+29 标签墨迹（11px，基线 y+29）
#      y+30,y+31  空行 + 选中框下边线
# ------------------------------------------------------------------
BAND_H = 16            # 屏幕 4 等分，每段 16px
CELL_W, CELL_H = 42, 32
COLS, ROWS = 3, 1
CELL_DX = 43           # 列距 = CELL_W + 1，列间留 1px 缝
CELL_Y0 = BAND_H       # 图标行顶边 = 状态栏下方

ICON_W = ICON_H = 16
ICON_X_OFF = (CELL_W - ICON_W) // 2    # 13，水平居中
ICON_Y_OFF = 1
LABEL_Y_OFF = 29       # 标签基线（相对格子顶边）
LABEL_W = 12           # 单字步进（近似值，只在 label_width 查不到时兜底）
LABEL_H = CJK_INK_H    # 标签墨迹高 11（实测）

APPS = [
    ("定时",   ICON_LOCK),
    ("网络",   ICON_NET),
    ("设置",   ICON_GEAR),
]

STYLES = ('frame', 'ring', 'invert')

# ------------------------------------------------------------------
#  状态栏（y0..y15）
#
#  三项：锁状态（左，**今天是真值**）+ 信号（中右，占位）+ 电量（右，占位）。
#
#  ⚠️ 信号要等 Step 7 上了 WiFi 才有；电量要等 PCB —— 面包板这一版上
#  **根本没有空闲的 ADC 脚**：C6 的 ADC 只有 GPIO0~GPIO6，而 GPIO0/1/2 是
#  K1/K2/K3、GPIO3 是模块内部射频开关、GPIO4/5/6 没引到排针上。
#  PCB 版已经把 K3 挪到 D6、把 GPIO2 让给电池分压（hardware/DESIGN.md §5）。
#
#  所以两项都按"读不到"画：**空心**。不是"信号为零"也不是"电池没电"——
#  画成实心会让人以为电量真是 0，那是主动误导，比不画更坏。
#
#  基线 y=12：汉字墨迹相对基线 -10..0，正好落在 y2..y12，上留 2 行下留 3 行。
#  实测宽度：「已锁」24、「开锁」23（tools/fontcheck），所以左组占 x2..x25。
# ------------------------------------------------------------------
BAR_BASE   = 12        # 状态栏文字的基线
BAR_LOCK_X = 2         # 锁状态文字左端

SIG_X       = 84       # 信号组左端
SIG_BAR_W   = 4        # 每根柱子宽
SIG_BAR_DX  = 5        # 步进 = 宽 + 1px 缝
SIG_BASE_Y  = 13       # 柱子底边（比文字基线低 1 行，视觉上才和汉字对齐）
SIG_BAR_MIN_H = 4      # 最矮那根的高度   ┐ 这两个宏和 app_desktop.ino 的
SIG_BAR_STEP  = 3      # 每根递增的高度   ┘ 配置区一字不差，改要一起改
SIG_HEIGHTS = tuple(SIG_BAR_MIN_H + i * SIG_BAR_STEP for i in range(4))

BATT_X     = 111       # 电池壳左端
BATT_W, BATT_H = 13, 9
BATT_Y     = 4
BATT_NUB_W, BATT_NUB_H = 2, 3

LOCK_TEXTS = {True: "已锁", False: "开锁"}


def draw_status_bar(cv, locked, sig=0, batt=-1):
    """
    locked  真值，来自锁应用（app_lock.ino 的 lockIsRunning()）
    sig     0..4 = 亮起几格；<=0 = 读不到 → 全空心
    batt    0..100；<0 = 读不到 → 只画外壳
    """
    cv.color = 1
    cv.label(BAR_LOCK_X, BAR_BASE, LOCK_TEXTS[bool(locked)], cjk_w=LABEL_W)

    # 信号：4 根从矮到高、底边对齐的柱子。**直角 frame 不是 rframe** ——
    # 最小那格只有 4x4，rframe 会跳过直边（坑 6）。
    for i, hgt in enumerate(SIG_HEIGHTS):
        bx = SIG_X + i * SIG_BAR_DX
        by = SIG_BASE_Y - hgt + 1
        if sig > i:
            cv.box(bx, by, SIG_BAR_W, hgt)
        else:
            cv.frame(bx, by, SIG_BAR_W, hgt)

    # 电池：外壳 + 右侧正极凸点。外壳恒画（那是"这里有个电池"），
    # 只有拿到读数才按比例填内胆。
    cv.frame(BATT_X, BATT_Y, BATT_W, BATT_H)
    cv.box(BATT_X + BATT_W, BATT_Y + (BATT_H - BATT_NUB_H) // 2, BATT_NUB_W, BATT_NUB_H)
    if batt >= 0:
        inner = max(1, (BATT_W - 4) * min(batt, 100) // 100)
        cv.box(BATT_X + 2, BATT_Y + 2, inner, BATT_H - 4)


def cell_origin(i):
    return (i % COLS) * CELL_DX, CELL_Y0 + (i // COLS) * CELL_H


def draw_cell(cv, i, name, icon, selected, style):
    x, y = cell_origin(i)
    ix, iy = x + ICON_X_OFF, y + ICON_Y_OFF
    lw = label_width(name)
    lx = x + (CELL_W - lw) // 2
    ly = y + LABEL_Y_OFF

    cv.color = 1

    if selected and style == 'frame':
        cv.rframe(x, y, CELL_W, CELL_H, 3)
    elif selected and style == 'ring':
        cv.rframe(ix - 1, iy - 1, ICON_W + 2, ICON_H + 2, 2)

    # 图标先画。反白标签是"先填实底、再把字挖空"，挖空的颜色会连带擦掉
    # 任何先画的东西，所以顺序不能反。
    cv.xbm(ix, iy, ICON_W, ICON_H, icon)

    if selected and style == 'invert':
        cv.box(lx - 2, ly - LABEL_H, lw + 4, LABEL_H + 2)   # 底色填实 y+17..y+30
        cv.color = 0                                        # 反白：字挖空
        cv.label(lx, ly, name, solid=True)
    else:
        cv.label(lx, ly, name)

    cv.color = 1


def render(sel=0, style='frame', locked=False, sig=0, batt=-1):
    cv = Canvas()
    draw_status_bar(cv, locked, sig, batt)
    for i, (name, icon) in enumerate(APPS):
        draw_cell(cv, i, name, icon, i == sel, style)
    return cv


# ------------------------------------------------------------------
#  输出
# ------------------------------------------------------------------
def show(cv, x0, y0, x1, y1, title):
    print(title)
    print('   +' + '-' * (x1 - x0 + 1) + '+')
    for y in range(y0, y1 + 1):
        print('   |' + ''.join('#' if cv.b[y][x] else '.' for x in range(x0, x1 + 1)) + '|')
    print('   +' + '-' * (x1 - x0 + 1) + '+')
    print()


def ink_pixels(cv):
    return [(x, y) for y in range(H) for x in range(W) if cv.b[y][x]]


def bbox(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def overlaps(a, b):
    """
    两个行区间 (顶, 底) 是否相交。

    只查行不查列：同一行里左右分开的东西（比如选中框的左右两条竖线）本来
    就该和内容同排，按矩形相交判会误报。
    """
    return not (a[1] < b[0] or b[1] < a[0])


def validate_status_bar():
    """
    状态栏自检：三项各自的行区间必须落在 y0..y15 里，且互不重叠。

    和格子自检同一个道理 —— 状态栏只有 16px，而信号柱最高 13px（y1..y13）、
    电池壳 y4..y12，挤出去就压到下面图标行的框线上了。
    """
    bad = 0

    lock_rows = (BAR_BASE - CJK_INK_H + 1, BAR_BASE)          # y2..y12
    sig_rows = (min(SIG_BASE_Y - h + 1 for h in SIG_HEIGHTS), SIG_BASE_Y)
    batt_rows = (BATT_Y, BATT_Y + BATT_H - 1)

    lock_x = (BAR_LOCK_X, BAR_LOCK_X + label_width("已锁") - 1)
    sig_x = (SIG_X, SIG_X + SIG_BAR_DX * (len(SIG_HEIGHTS) - 1) + SIG_BAR_W - 1)
    batt_x = (BATT_X, BATT_X + BATT_W + BATT_NUB_W - 1)

    for nm, rows, cols in (('锁状态', lock_rows, lock_x),
                           ('信号',   sig_rows,  sig_x),
                           ('电量',   batt_rows, batt_x)):
        if rows[0] < 0 or rows[1] >= BAND_H:
            print("  !! 状态栏「%s」纵向 y%d..%d 超出 y0..%d（会压到图标行）"
                  % (nm, rows[0], rows[1], BAND_H - 1))
            bad += 1
        if cols[0] < 0 or cols[1] >= W:
            print("  !! 状态栏「%s」横向 x%d..%d 超出 x0..%d"
                  % (nm, cols[0], cols[1], W - 1))
            bad += 1
        print("  [状态栏] %-4s x%3d..%3d  y%2d..%2d"
              % (nm, cols[0], cols[1], rows[0], rows[1]))

    # 三项都在同一行区里左右排开，所以只查横向不相交
    groups = (('锁状态', lock_x), ('信号', sig_x), ('电量', batt_x))
    for i in range(len(groups) - 1):
        an, ac = groups[i]; bn, bc = groups[i + 1]
        if ac[1] >= bc[0]:
            print("  !! 状态栏「%s」x%d..%d 与「%s」x%d..%d 横向相交"
                  % (an, ac[0], ac[1], bn, bc[0], bc[1]))
            bad += 1

    # 图标行的框线在 y16，状态栏最下面那点必须还在 y15 里
    lowest = max(lock_rows[1], sig_rows[1], batt_rows[1])
    if lowest >= CELL_Y0:
        print("  !! 状态栏最低墨点在 y%d，碰到图标行顶边 y%d"
              % (lowest, CELL_Y0))
        bad += 1
    return bad


def validate():
    """
    几何自检。三件事：
      1. 所有墨点必须落在自己格子里（越界会串到隔壁应用上）
      2. 图标底边和标签顶边不能重叠
      3. 选中框的上下边线不能压到图标或标签
    """
    bad = validate_status_bar()
    for style in STYLES:
        for i, (name, icon) in enumerate(APPS):
            x, y = cell_origin(i)
            ix, iy = x + ICON_X_OFF, y + ICON_Y_OFF
            lx = x + (CELL_W - label_width(name)) // 2

            # --- 1. 越界 ---
            cv = Canvas()
            draw_cell(cv, i, name, icon, True, style)
            for (xx, yy) in ink_pixels(cv):
                if not (x <= xx < x + CELL_W and y <= yy < y + CELL_H):
                    print("  !! [%s] 应用 %d (%s) 墨点越界：(%d,%d) 格子 x%d..%d y%d..%d"
                          % (style, i, name, xx, yy, x, x + CELL_W - 1, y, y + CELL_H - 1))
                    bad += 1

            # --- 1b. 标签横向必须装得下 ---
            # 这一条是给"以后加应用"用的：格子只有 42px，标签实测 35px，
            # 四个汉字就 47px 装不下了。上面那条越界检查抓不到这种情况 ——
            # 模拟器按**同一个**宽度画框，算错起点时框跟着一起偏，自检自洽。
            lw = label_width(name)
            if lw > CELL_W:
                print("  !! %s 标签宽 %dpx 装不进 %dpx 的格子（真机上是压到邻格）"
                      % (name, lw, CELL_W))
                bad += 1

            # --- 2/3. 行区间 ---
            icon_rows = (iy, iy + ICON_H - 1)
            label_rows = (y + LABEL_Y_OFF - LABEL_H + 1, y + LABEL_Y_OFF)
            if overlaps(icon_rows, label_rows):
                print("  !! [%s] %s 图标 y%d..%d 与标签 y%d..%d 重叠"
                      % (style, name, icon_rows[0], icon_rows[1], label_rows[0], label_rows[1]))
                bad += 1
            if style == 'frame':
                top_line, bot_line = (y, y), (y + CELL_H - 1, y + CELL_H - 1)
                for nm, box in (('图标', icon_rows), ('标签', label_rows)):
                    if overlaps(box, top_line) or overlaps(box, bot_line):
                        print("  !! [frame] %s 的 %s y%d..%d 压到框线"
                              % (name, nm, box[0], box[1]))
                        bad += 1

            print("  [%-6s] %-4s 格子 x%d..%d y%d..%d | 图标 y%d..%d | 标签 y%d..%d | 图标-标签缝 %d 行"
                  % (style, name, x, x + CELL_W - 1, y, y + CELL_H - 1,
                     icon_rows[0], icon_rows[1], label_rows[0], label_rows[1],
                     label_rows[0] - icon_rows[1] - 1))
    return bad


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'compare'

    if cmd == 'gen':
        for nm, art in (('ICON_LOCK', ART_LOCK),
                        ('ICON_NET',  ART_NET),
                        ('ICON_GEAR', ART_GEAR)):
            print(gen_c_array(nm, art))
            print()
        sys.exit(0)

    if cmd == 'full':
        for style in STYLES:
            cv = render(1, style)
            print("=" * 70)
            print("整屏 · 选中第 2 格 · 高亮样式 = %s" % style)
            print("=" * 70)
            for y in range(H):
                print(''.join('#' if cv.b[y][x] else '.' for x in range(W)))
            print()
        sys.exit(0)

    if cmd == 'bar':
        # 状态栏四种取值。前两种是**今天真机上会出现的**（信号和电量都读不到），
        # 后两种是 Step 7 之后的预期样子，留着当目标参考。
        cases = [(False, 0, -1, '开锁 · 信号电量读不到（今天就是这样）'),
                 (True,  0, -1, '已锁 · 信号电量读不到'),
                 (False, 2, 45, '开锁 · 信号 2 格 · 电量 45%'),
                 (True,  4, 100, '已锁 · 信号满格 · 电量满')]
        for locked, sig, batt, desc in cases:
            cv = Canvas()
            draw_status_bar(cv, locked, sig, batt)
            show(cv, 0, 0, 127, 15, desc)
        sys.exit(0)

    print("=" * 66)
    print(" 三种选中高亮，都画在左上角那一格（其它格不画，专心比样式）")
    print(" 方块 = 汉字占的位置，不是真字形")
    print("=" * 66)
    print()
    for style, desc in (('frame',  'A 整格圆角框 —— 框住整格（42x32 的边线）'),
                        ('ring',   'B 图标聚焦环 —— 只框住图标（18x18）'),
                        ('invert', 'C 标签反白 —— 标签底色填实、字挖空')):
        cv = render(0, style)
        show(cv, 0, 0, 44, 33, desc)
    print("=" * 66)
    print(" 几何自检")
    print("=" * 66)
    bad = validate()
    print()
    print("越界/重叠：%d 处" % bad)
