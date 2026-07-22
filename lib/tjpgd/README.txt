TJpgDec 集成说明
================

本目录需要放入 ChaN 官方的 TJpgDec 源码 (纯 ANSI C, 无平台依赖).

一、下载
--------
访问:  http://elm-chan.org/fsw/tjpgd/00index.html
下载最新版 (tjpgd3.zip, 目前 R0.03), 解压后把两个文件放到本目录:

  lib/tjpgd/tjpgd.c
  lib/tjpgd/tjpgd.h
  lib/tjpgd/tjpgdcnf.h   ← 本工程已提供, 不要用官方那份覆盖

最终目录结构:
  lib/
    tjpgd/
      tjpgd.c        (官方)
      tjpgd.h        (官方)
      tjpgdcnf.h     (本工程定制: JD_FORMAT=1 → RGB565)

二、配置
--------
tjpgdcnf.h 里已经把关键宏调好:
  JD_FORMAT     = 1   → 输出 RGB565, 正好对应 LCD 显存
  JD_USE_SCALE  = 1   → jd_decomp 支持 scale 参数 (我们传 0 = 1:1)
  JD_TBLCLIP    = 1   → 查表 clamp, 加速
  JD_FASTDECODE = 1   → 中档速度/RAM 权衡

三、PlatformIO 会自动发现
------------------------
platform = ststm32 的 lib_deps 会扫 lib/ 下每个子目录, 无需手动改
platformio.ini. 编译时 tjpgd.c 会被自动纳入.

四、如果编译报错找不到 stdint 类型
----------------------------------
tjpgd.h 顶部可能有 typedef 用了 UINT/BYTE 等 (老风格).
新版 R0.03 已用 stdint. 如果你下的是老版本, 打开 tjpgd.h 确认有:
    #include <stdint.h>
    typedef uint8_t  BYTE;
    typedef uint16_t WORD;
    typedef uint32_t DWORD;
    typedef unsigned int UINT;
或者直接把这几行加上.

五、验证
--------
在 main.c 里加:
    LCD_Init();
    extern const uint8_t test_jpg[];       // 一张小 jpg 作为测试
    extern const uint32_t test_jpg_len;
    LCD_DrawJpeg(20, 20, test_jpg, test_jpg_len);
能显示 → 集成成功.
