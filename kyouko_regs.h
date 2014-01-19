#ifndef _KYOUKO_REGS_H
#define _KYOUKO_REGS_H

#include <linux/ioctl.h>

#define CfgSupported 0x0000
#define CfgMode 0x0004
#define CfgAccel 0x0008
#define CfgWidth 0x000C
#define CfgHeight 0x0010
#define CfgFrame 0x0018
#define CfgFlags 0x001C
#define CfgFeatures 0x0020

#define CmdReboot 0x0800
#define CmdPrimitive 0x0804
#define CmdVertex 0x0808
#define CmdSync 0x080C
#define CmdActiveBuffer 0x0814
#define CmdClear 0x0818
#define CmdDMABuffer 0x0820
#define CmdDMACount 0x0824

#define VtxPosition 0x0900
#define VtxColor 0x0910
#define VtxTexCoord 0x0930
#define VtxTransform 0x0A00

#define InFIFO 0x0F00

#define VMODE _IOW(0xCC, 0, unsigned long)
#define BIND_DMA _IOW(0xCC, 1, unsigned long *)
#define START_DMA _IOW(0xCC, 2, unsigned long *)
#define GRAPHICS_ON _IOW(0xCC, 1, unsigned long)
#define GRAPHICS_OFF _IOW(0xCC, 0, unsigned long)

#endif
