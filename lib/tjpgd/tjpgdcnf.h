/*----------------------------------------------------------------------------/
/  TJpgDec configuration file — 本工程 (串口图像检测系统) 使用
/----------------------------------------------------------------------------*/

#ifndef TJPGDCNF_H_
#define TJPGDCNF_H_

/* JD_SZBUF: 输入流每次读取的字节数.  512 是官方默认, 兼顾速度和 RAM. */
#define JD_SZBUF            512

/* JD_FORMAT:
 *   0 = RGB888 (24bit, 一像素 3 字节)
 *   1 = RGB565 (16bit, 一像素 2 字节)  ← LCD 用 RGB565, 选它
 *   2 = 灰度 8bit                       — 部分老版本不支持, 出错就退回 1
 */
#define JD_FORMAT           1

/* JD_USE_SCALE:
 *   1 = 支持 1/2, 1/4, 1/8 缩小.  本题不缩小, 但开着不影响, tjpgd 更方便调试. */
#define JD_USE_SCALE        1

/* JD_TBLCLIP:
 *   1 = 用查表加快去饱和 (色度 clamp), 稍占 flash.  F103 flash 512KB, 开. */
#define JD_TBLCLIP          1

/* JD_FASTDECODE:
 *   0 = 慢, 少 RAM (推荐 F103, 帧率够用)
 *   1 = 快, 大约多 320B RAM
 *   2 = 更快, 但多约 6KB RAM  — F103 SRAM 64KB, 不给这个
 * 我们先给 1, 兼顾速度和 RAM. 若不够可降 0. */
#define JD_FASTDECODE       1

#endif /* TJPGDCNF_H_ */
