/*----------------------------------------------------------------------------/
/  TJpgDec configuration file — 本工程 (串口图像检测系统) 使用
/----------------------------------------------------------------------------*/

#ifndef TJPGDCNF_H_
#define TJPGDCNF_H_

/* JD_SZBUF: 输入流每次读取的字节数.  512 是官方默认, 兼顾速度和 RAM. */
#define JD_SZBUF            512

/* JD_FORMAT:
 *   0 = RGB888 (24bit, 一像素 3 字节)
 *   1 = RGB565 (16bit, 一像素 2 字节)
 *   2 = 灰度 8bit                       ← 本题输入是灰度 jpg, 用 2 才能得到
 *                                          完整 256 级灰度; 用 1 反推会丢
 *                                          到只剩 32 级, 熵值会偏低.
 *   (LCD 那边我们自己把灰度打包成 RGB565 显示, 不损失.)
 */
#define JD_FORMAT           2

/* JD_USE_SCALE:
 *   1 = 支持 1/2, 1/4, 1/8 缩小.  本题不缩小, 但开着不影响, tjpgd 更方便调试. */
#define JD_USE_SCALE        1

/* JD_TBLCLIP:
 *   0 = if/else 饱和裁剪, 慢 3~5%, 但对所有 IDCT 输出都能正确 clip
 *   1 = 查表 clip, 快, 但依赖 Clip8[(v) & 0x3FF], 当 IDCT 偶发超出
 *       [-256, 767] 时 & 0x3FF 会环绕 → 高对比边缘出现"随机小黑点"
 *   本题选 0 换稳定. */
#define JD_TBLCLIP          0

/* JD_FASTDECODE:
 *   0 = 慢, 少 RAM, 精度最好 (推荐)
 *   1 = 快, 大约多 320B RAM
 *   2 = 更快, 但多约 6KB RAM
 * 之前用 1 出现边缘黑点, 换回 0. F103 上 IDCT 一帧几十 ms, 够用. */
#define JD_FASTDECODE       0

#endif /* TJPGDCNF_H_ */
