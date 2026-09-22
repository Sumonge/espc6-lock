/*
 * 真字模量宽 —— 在**主机上**编译 U8g2 自己的 u8g2_font.c，直接调
 * u8g2_GetStrWidth()。不是Python复刻。
 * 理由：助手看不到屏幕，版面估算只能靠数字；而复刻一个位解码器再去"验证"
 * 版面，等于用可能错的尺子量东西。这里用的是和固件**逐字节相同**的那份代码。
 */
#include <stdio.h>
#include <string.h>
#include "u8g2.h"

static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr; return 1;
}
static uint8_t gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr; return 1;
}

static u8g2_t u8g2;

static void row(const char *fontname, const uint8_t *font, const char *s) {
  u8g2_SetFont(&u8g2, font);
  int w = (int)u8g2_GetUTF8Width(&u8g2, s);
  printf("%-34s asc=%2d desc=%2d maxh=%2d  w=%3d  %s\n",
         fontname, (int)u8g2_GetAscent(&u8g2), (int)u8g2_GetDescent(&u8g2),
         (int)u8g2_GetMaxCharHeight(&u8g2), w, s);
}

int main(void) {
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, byte_cb, gpio_cb);
  u8g2_InitDisplay(&u8g2);

  printf("== 主字体 u8g2_font_wqy12_t_gb2312 ==\n");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "字");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "0");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "KHW 定时锁");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "v1.0  Sep 22 2026");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "定时锁");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "设置");

  printf("\n== 桌面标签（1x3 网格，三格各 42px）==\n");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "定时");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "网络");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "定时锁");

  printf("\n== 网络页（drawBand 从 x=2 起，一行最多 126px）==\n");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "设备  LM-4K7-X9Q");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "设备  LM-WWW-999");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "名称  LM-4K7-X9Q");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "无线  未接入");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "配网  未接入");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "LM-4K7-X9Q");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "LM-WWW-WWW");

  printf("\n== 状态栏（y0..15，左 2px 起）==\n");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "已锁");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "开锁");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "已锁 00:00");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "开锁 00:00");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "关于本机 v1.0");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "ESP32-C6  500KB");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "编译 Sep 22 2026");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "运行 00:12:34");
  row("wqy12_gb2312", u8g2_font_wqy12_t_gb2312, "蓝牙");

  printf("\n== 对比：wqy12_t_chinese1 / chinese3（CLAUDE.md 里另两个候选）==\n");
  row("wqy12_chinese1", u8g2_font_wqy12_t_chinese1, "字");
  row("wqy12_chinese1", u8g2_font_wqy12_t_chinese1, "0");
  row("wqy12_chinese3", u8g2_font_wqy12_t_chinese3, "字");

  printf("\n== 大字号 logisoso24_tn ==\n");
  row("logisoso24_tn", u8g2_font_logisoso24_tn, "00:00");
  row("logisoso24_tn", u8g2_font_logisoso24_tn, "99:59");

  printf("\n== ASCII 单字符宽度表（wqy12_gb2312）==\n");
  u8g2_SetFont(&u8g2, u8g2_font_wqy12_t_gb2312);
  for (int c = 32; c < 127; c++) {
    char b[2] = { (char)c, 0 };
    printf("%c=%d ", c, (int)u8g2_GetUTF8Width(&u8g2, b));
    if ((c - 31) % 12 == 0) printf("\n");
  }
  printf("\n");
  return 0;
}
