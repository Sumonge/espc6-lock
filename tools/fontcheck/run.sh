#!/usr/bin/env bash
#
# 真字模量宽 —— 在**主机上**编译 U8g2 自己的 u8g2_font.c，量真实字宽和墨迹盒。
#
# 为什么不在 Python 里复刻一个字形解码器：复刻出来再去"验证"版面，等于用
# 一把可能错的尺子量东西 —— 尺子错了，量出来"刚好合适"比不量更危险。这里
# 用的是和固件**逐字节同一份** U8g2 源码，只是换个编译器。
#
# 两个探针：
#   main.c  u8g2_GetUTF8Width()  各种串的宽度（居中 / 是否越右边界）
#   bbox.c  把字真画进 u8g2 缓冲区再扫回来，求墨迹包围盒（纵向排布够不够）
#
# 用法：bash tools/fontcheck/run.sh
#      U8G2_DIR=/别的路径 bash tools/fontcheck/run.sh
set -e

U8G2="${U8G2_DIR:-/c/Users/14919/Documents/Arduino/libraries/U8g2/src}"
cd "$(dirname "$0")"

if [ ! -f "$U8G2/clib/u8g2_font.c" ]; then
  echo "找不到 U8g2 源码：$U8G2" >&2
  echo "用 U8G2_DIR=<.../U8g2/src> 指定" >&2
  exit 1
fi

# -DU8G2_USE_LARGE_FONTS：板上由 u8g2.h 里的 ESP_PLATFORM 自动定义，
# 主机上没有那个宏，必须手动补 —— 不补的话 wqy12_t_gb2312 整个不会被编进来，
# 报的是链接错 undefined reference，看起来像路径问题，其实只差这一个宏。
for t in main bbox; do
  gcc -O1 -w -DU8G2_USE_LARGE_FONTS \
      -I"$U8G2" -I"$U8G2/clib" "$t.c" "$U8G2"/clib/*.c -o "$t.exe"
done

./main.exe
echo
./bbox.exe
