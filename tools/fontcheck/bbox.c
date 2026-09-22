/*
 * 真墨迹包围盒 —— 把字**真画进** u8g2 的 1024 字节缓冲区，再扫回来。
 * 这是桌面版面"图标底边与标签顶边会不会撞上"的唯一硬证据：
 * max_char_height 是**整个字体里最高那个字形**，不代表"定时锁"这三个字。
 */
#include <stdio.h>
#include "u8g2.h"

static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr; return 1;
}
static uint8_t gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr; return 1;
}

static u8g2_t u8g2;

/* 画到缓冲区再扫回来。返回 0 = 没有墨迹。base = 相对基线的行偏移 */
static int bbox(const uint8_t *font, const char *s, int x, int baseline,
                int *x0, int *y0, int *x1, int *y1) {
  u8g2_ClearBuffer(&u8g2);
  u8g2_SetFont(&u8g2, font);
  u8g2_DrawUTF8(&u8g2, x, baseline, s);

  const uint8_t *buf = u8g2_GetBufferPtr(&u8g2);
  int found = 0;
  *x0 = *y0 = 9999; *x1 = *y1 = -9999;
  for (int yy = 0; yy < 64; yy++)
    for (int xx = 0; xx < 128; xx++)
      if (buf[(yy >> 3) * 128 + xx] & (1 << (yy & 7))) {
        if (xx < *x0) *x0 = xx;
        if (xx > *x1) *x1 = xx;
        if (yy < *y0) *y0 = yy;
        if (yy > *y1) *y1 = yy;
        found = 1;
      }
  return found;
}

static void report(const char *tag, const char *s, int baseline) {
  int x0, y0, x1, y1;
  if (!bbox(u8g2_font_wqy12_t_gb2312, s, 0, baseline, &x0, &y0, &x1, &y1)) {
    printf("%-22s %-16s 无墨迹\n", tag, s);
    return;
  }
  printf("%-22s %-16s 墨迹 x%d..%d (宽%d)  y%d..%d (高%d)  "
         "相对基线 %+d..%+d\n",
         tag, s, x0, x1, x1 - x0 + 1, y0, y1, y1 - y0 + 1, y0 - baseline, y1 - baseline);
}

int main(void) {
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, byte_cb, gpio_cb);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFontMode(&u8g2, 1);

  const int base = 40;   /* 随便挑一个不贴边的高度 */
  printf("=== 汉字：真墨迹相对基线的范围（wqy12_gb2312）===\n");
  report("汉字单字", "字", base);
  report("汉字基准", "国", base);
  report("桌面标签1", "定时锁", base);
  report("桌面标签2", "设置", base);
  report("关于页标题", "关于本机 v1.0", base);
  report("关于页第2行", "ESP32-C6  500KB", base);
  report("关于页第3行", "编译 Sep 22 2026", base);
  report("关于页第4行", "运行 00:12:34", base);
  report("启动标题", "KHW 定时锁", base);

  printf("\n=== ASCII 对照（应该落在 ascent=8 / descent=-2 内）===\n");
  report("ASCII 大写", "AHK", base);
  report("ASCII 小写", "gpq", base);
  report("ASCII 数字", "0123456789", base);
  return 0;
}
