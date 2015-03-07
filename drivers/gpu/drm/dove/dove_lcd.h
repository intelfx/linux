/*
 * LCD controller registers of Marvell DOVE
 *
 * Copyright (C) 2013
 *   Jean-Francois Moine <moinejf@free.fr>
 *   Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DOVE_LCD_H_
#define	_DOVE_LCD_H_

/* ------------< LCD register >------------ */

/* Video Frame 0&1 start address registers */
#define	LCD_TV_CONTROL1			0x0084
#define   VSYNC_L_OFFSET(o)			((o) << 20)
#define   VSYNC_L_OFFSET_MASK			(0xfff << 20)
#define   HWC32_ENABLE				BIT(13)
#define   VSYNC_OFFSET_EN			BIT(12)
#define   VSYNC_H_OFFSET(o)			(o)
#define   VSYNC_H_OFFSET_MASK			0xfff

/* Video Frame 0&1 start address registers */
#define	LCD_SPU_DMA_START_ADDR_Y0	0x00c0
#define	LCD_SPU_DMA_START_ADDR_U0	0x00c4
#define	LCD_SPU_DMA_START_ADDR_V0	0x00c8
#define LCD_CFG_DMA_START_ADDR_0	0x00cc /* Cmd address */
#define	LCD_SPU_DMA_START_ADDR_Y1	0x00d0
#define	LCD_SPU_DMA_START_ADDR_U1	0x00d4
#define	LCD_SPU_DMA_START_ADDR_V1	0x00d8
#define LCD_CFG_DMA_START_ADDR_1	0x00dc /* Cmd address */

/* YC & UV Pitch */
#define LCD_SPU_DMA_PITCH_YC		0x00e0
#define   LCD_Y_C(y, c)				(((c) << 16) | (y))
#define LCD_SPU_DMA_PITCH_UV		0x00e4
#define   LCD_U_V(u, v)				(((v) << 16) | (u))

/* Video Starting Point on Screen Register */
#define LCD_SPUT_DMA_OVSA_HPXL_VLN	0x00e8

/* Video Size Register */
#define LCD_SPU_DMA_HPXL_VLN		0x00ec

/* Video Size After zooming Register */
#define LCD_SPU_DZM_HPXL_VLN		0x00f0

/* Graphic Frame 0&1 Starting Address Register */
#define LCD_CFG_GRA_START_ADDR0		0x00f4
#define LCD_CFG_GRA_START_ADDR1		0x00f8

/* Graphic Frame Pitch */
#define LCD_CFG_GRA_PITCH		0x00fc

/* Graphic Starting Point on Screen Register */
#define LCD_SPU_GRA_OVSA_HPXL_VLN	0x0100

/* Graphic Size Register */
#define LCD_SPU_GRA_HPXL_VLN		0x0104

/* Graphic Size after Zooming Register */
#define LCD_SPU_GZM_HPXL_VLN		0x0108

/* HW Cursor Starting Point on Screen Register */
#define LCD_SPU_HWC_OVSA_HPXL_VLN	0x010c

/* HW Cursor Size */
#define LCD_SPU_HWC_HPXL_VLN		0x0110

/* Total Screen Size Register */
#define LCD_SPUT_V_H_TOTAL		0x0114

/* Total Screen Active Size Register */
#define LCD_SPU_V_H_ACTIVE		0x0118
#define   LCD_H_V(h, v)				(((v) << 16) | (h))
#define   H_LCD(x)				((x) & 0xffff)
#define   V_LCD(x)				(((x) >> 16) & 0xffff)

/* Screen H&V Porch Register */
#define LCD_SPU_H_PORCH			0x011c
#define LCD_SPU_V_PORCH			0x0120
#define   LCD_F_B(f, b)				(((b) << 16) | (f))
#define   F_LCD(x)				((x) & 0xffff)
#define   B_LCD(x)				(((x) >> 16) & 0xffff)

/* Screen Blank Color Register */
#define LCD_SPU_BLANKCOLOR		0x0124

/* HW Cursor Color 1&2 Register */
#define LCD_SPU_ALPHA_COLOR1		0x0128
#define   HWC32_CFG_ALPHA(alpha)		((alpha) << 24)
#define LCD_SPU_ALPHA_COLOR2		0x012c
#define   COLOR_MASK				0x00ffffff
#define   COLOR_RGB(r, g, b) (((b) << 16) | ((g) << 8) | (r))

/* Video YUV Color Key Control */
#define LCD_SPU_COLORKEY_Y		0x0130
#define   CFG_CKEY_Y2(y2)			((y2) << 24)
#define   CFG_CKEY_Y2_MASK			0xff000000
#define   CFG_CKEY_Y1(y1)			((y1) << 16)
#define   CFG_CKEY_Y1_MASK			0x00ff0000
#define   CFG_CKEY_Y(y)				((y) << 8)
#define   CFG_CKEY_Y_MASK			0x0000ff00
#define   CFG_ALPHA_Y(y)			(y)
#define   CFG_ALPHA_Y_MASK			0x000000ff
#define LCD_SPU_COLORKEY_U		0x0134
#define   CFG_CKEY_U2(u2)			((u2) << 24)
#define   CFG_CKEY_U2_MASK			0xff000000
#define   CFG_CKEY_U1(u1)			((u1) << 16)
#define   CFG_CKEY_U1_MASK			0x00ff0000
#define   CFG_CKEY_U(u)				((u) << 8)
#define   CFG_CKEY_U_MASK			0x0000ff00
#define   CFG_ALPHA_U(u)			(u)
#define   CFG_ALPHA_U_MASK			0x000000ff
#define LCD_SPU_COLORKEY_V		0x0138
#define   CFG_CKEY_V2(v2)			((v2) << 24)
#define   CFG_CKEY_V2_MASK			0xff000000
#define   CFG_CKEY_V1(v1)			((v1) << 16)
#define   CFG_CKEY_V1_MASK			0x00ff0000
#define   CFG_CKEY_V(v)				((v) << 8)
#define   CFG_CKEY_V_MASK			0x0000ff00
#define   CFG_ALPHA_V(v)			(v)
#define   CFG_ALPHA_V_MASK			0x000000ff

/* LCD General Configuration Register */
#define LCD_CFG_RDREG4F			0x013c
#define   LCD_SRAM_WAIT				BIT(11)
#define   DMA_WATERMARK_MASK			0xff
#define   DMA_WATERMARK(m)			(m)

/* SPI Read Data Register */
#define LCD_SPU_SPI_RXDATA		0x0140

/* Smart Panel Read Data Register */
#define LCD_SPU_ISA_RSDATA		0x0144
#define   ISA_RXDATA_16BIT_1_DATA_MASK		0x000000ff
#define   ISA_RXDATA_16BIT_2_DATA_MASK		0x0000ff00
#define   ISA_RXDATA_16BIT_3_DATA_MASK		0x00ff0000
#define   ISA_RXDATA_16BIT_4_DATA_MASK		0xff000000
#define   ISA_RXDATA_32BIT_1_DATA_MASK		0x00ffffff

/* HWC SRAM Read Data Register */
#define LCD_SPU_HWC_RDDAT		0x0158

/* Gamma Table SRAM Read Data Register */
#define LCD_SPU_GAMMA_RDDAT		0x015c
#define   GAMMA_RDDAT_MASK			0x000000ff

/* Palette Table SRAM Read Data Register */
#define LCD_SPU_PALETTE_RDDAT		0x0160
#define   PALETTE_RDDAT_MASK			0x00ffffff

/* I/O Pads Input Read Only Register */
#define LCD_SPU_IOPAD_IN		0x0178
#define   IOPAD_IN_MASK				0x0fffffff

/* Reserved Read Only Registers */
#define LCD_CFG_RDREG5F			0x017c
#define   IRE_FRAME_CNT_MASK			0x000000c0
#define   IPE_FRAME_CNT_MASK			0x00000030
#define   GRA_FRAME_CNT_MASK			0x0000000c	/* Graphic */
#define   DMA_FRAME_CNT_MASK			0x00000003	/* Video */

/* SPI Control Register. */
#define LCD_SPU_SPI_CTRL		0x0180
#define   CFG_SCLKCNT(div)			((div) << 24)
#define   CFG_SCLKCNT_MASK			0xff000000
#define   CFG_RXBITS(rx)			((rx) << 16)
#define   CFG_RXBITS_MASK			0x00ff0000
#define   CFG_TXBITS(tx)			((tx) << 8)
#define   CFG_TXBITS_MASK			0x0000ff00
#define   CFG_CLKINV				BIT(7)
#define   CFG_KEEPXFER				BIT(6)
#define   CFG_RXBITSTO0				BIT(5)
#define   CFG_TXBITSTO0				BIT(4)
#define   CFG_SPI_ENA				BIT(3)
#define   CFG_SPI_SEL				BIT(2)
#define   CFG_SPI_3W4WB				BIT(1)
#define   CFG_SPI_START				BIT(0)

/* SPI Tx Data Register */
#define LCD_SPU_SPI_TXDATA		0x0184

/*
 *  1. Smart Pannel 8-bit Bus Control Register.
 *  2. AHB Slave Path Data Port Register
 */
#define LCD_SPU_SMPN_CTRL		0x0188

/* DMA Control 0 Register */
#define LCD_SPU_DMA_CTRL0		0x0190
#define   CFG_NOBLENDING			BIT(31)
#define   CFG_GAMMA_ENA				BIT(30)
#define   CFG_CBSH_ENA				BIT(29)
#define   CFG_PALETTE_ENA			BIT(28)
#define   CFG_ARBFAST_ENA			BIT(27)
#define   CFG_HWC_1BITMOD			BIT(26)
#define   CFG_HWC_1BITENA			BIT(25)
#define   CFG_HWC_ENA				BIT(24)
#define   CFG_DMAFORMAT(dmaformat)		((dmaformat) << 20)
#define   CFG_DMAFORMAT_MASK			0x00f00000
#define   CFG_GRAFORMAT(graformat)		((graformat) << 16)
#define   CFG_GRAFORMAT_MASK			0x000f0000
/* for graphic part */
#define   CFG_GRA_FTOGGLE			BIT(15)
#define   CFG_GRA_HSMOOTH			BIT(14)
#define   CFG_GRA_TSTMODE			BIT(13)
#define   CFG_GRA_SWAPRB			BIT(12)
#define   CFG_GRA_SWAPUV			BIT(11)
#define   CFG_GRA_SWAPYU			BIT(10)
#define   CFG_YUV2RGB_GRA			BIT(9)
#define   CFG_GRA_ENA				BIT(8)
/* for video part */
#define   CFG_DMA_FTOGGLE			BIT(7)
#define   CFG_DMA_HSMOOTH			BIT(6)
#define   CFG_DMA_TSTMODE			BIT(5)
#define   CFG_DMA_SWAPRB			BIT(4)
#define   CFG_DMA_SWAPUV			BIT(3)
#define   CFG_DMA_SWAPYU			BIT(2)
#define   CFG_DMA_SWAP_MASK			0x0000001c
#define   CFG_YUV2RGB_DMA			BIT(1)
#define   CFG_DMA_ENA				BIT(0)

/* DMA Control 1 Register */
#define LCD_SPU_DMA_CTRL1		0x0194
#define   CFG_FRAME_TRIG			BIT(31)
#define   CFG_VSYNC_TRIG(trig)			((trig) << 28)
#define   CFG_VSYNC_TRIG_MASK			0x70000000
#define   CFG_VSYNC_INV				BIT(27)
#define   CFG_COLOR_KEY_MODE(cmode)		((cmode) << 24)
#define   CFG_COLOR_KEY_MASK			0x07000000
#define   CFG_CARRY				BIT(23)
#define   CFG_GATED_ENA				BIT(21)
#define   CFG_PWRDN_ENA				BIT(20)
#define   CFG_DSCALE(dscale)			((dscale) << 18)
#define   CFG_DSCALE_MASK			0x000c0000
#define   CFG_ALPHA_MODE(amode)			((amode) << 16)
#define   CFG_ALPHA_MODE_MASK			0x00030000
#define   CFG_ALPHA(alpha)			((alpha) << 8)
#define   CFG_ALPHA_MASK			0x0000ff00
#define   CFG_PXLCMD(pxlcmd)			(pxlcmd)
#define   CFG_PXLCMD_MASK			0x000000ff

/* SRAM Control Register */
#define LCD_SPU_SRAM_CTRL		0x0198
#define   CFG_SRAM_INIT_WR_RD(mode)		((mode) << 14)
#define   CFG_SRAM_INIT_WR_RD_MASK		0x0000c000
#define   CFG_SRAM_ADDR_LCDID(id)		((id) << 8)
#define   CFG_SRAM_ADDR_LCDID_MASK		0x00000f00
#define   CFG_SRAM_ADDR(addr)			(addr)
#define   CFG_SRAM_ADDR_MASK			0x000000ff

/* SRAM Write Data Register */
#define LCD_SPU_SRAM_WRDAT		0x019c

/* SRAM RTC/WTC Control Register */
#define LCD_SPU_SRAM_PARA0		0x01a0

/* SRAM Power Down Control Register */
#define LCD_SPU_SRAM_PARA1		0x01a4
#define   CFG_CSB_256x32			BIT(15)		/* HWC */
#define   CFG_CSB_256x24			BIT(14)		/* Palette */
#define   CFG_CSB_256x8				BIT(13)		/* Gamma */
#define   CFG_PDWN256x32			BIT(7)		/* HWC */
#define   CFG_PDWN256x24			BIT(6)		/* Palette */
#define   CFG_PDWN256x8				BIT(5)		/* Gamma */
#define   CFG_PDWN32x32				BIT(3)
#define   CFG_PDWN16x66				BIT(2)
#define   CFG_PDWN32x66				BIT(1)
#define   CFG_PDWN64x66				BIT(0)

/* Smart or Dumb Panel Clock Divider */
#define LCD_CFG_SCLK_DIV		0x01a8
#define   SET_SCLK(src, div, frac) (((src) << 30) | ((frac) << 16 ) | (div))

/* Video Contrast Register */
#define LCD_SPU_CONTRAST		0x01ac
#define   CFG_BRIGHTNESS(bright)		((bright) << 16)
#define   CFG_BRIGHTNESS_MASK			0xffff0000
#define   CFG_CONTRAST(contrast)		(contrast)
#define   CFG_CONTRAST_MASK			0x0000ffff

/* Video Saturation Register */
#define LCD_SPU_SATURATION		0x01b0
#define   CFG_C_MULTS(mult)			((mult) << 16)
#define   CFG_C_MULTS_MASK			0xffff0000
#define   CFG_SATURATION(sat)			(sat)
#define   CFG_SATURATION_MASK			0x0000ffff

/* Video Hue Adjust Register */
#define LCD_SPU_CBSH_HUE		0x01b4
#define   CFG_SIN0(sin0)			((sin0) << 16)
#define   CFG_SIN0_MASK				0xffff0000
#define   CFG_COS0(con0)			(con0)
#define   CFG_COS0_MASK				0x0000ffff

/* Dump LCD Panel Control Register */
#define LCD_SPU_DUMB_CTRL		0x01b8
#define   CFG_DUMBMODE(mode)			((mode) << 28)
#define   CFG_DUMBMODE_MASK			0xf0000000
#define   CFG_LCDGPIO_O(data)			((data) << 20)
#define   CFG_LCDGPIO_O_MASK			0x0ff00000
#define   CFG_LCDGPIO_ENA(gpio)			((gpio) << 12)
#define   CFG_LCDGPIO_ENA_MASK			0x000ff000
#define   CFG_BIAS_OUT				BIT(8)
#define   CFG_REVERSE_RGB			BIT(7)
#define   CFG_INV_COMPBLANK			BIT(6)
#define   CFG_INV_COMPSYNC			BIT(5)
#define   CFG_INV_HENA				BIT(4)
#define   CFG_INV_VSYNC				BIT(3)
#define   CFG_INV_HSYNC				BIT(2)
#define   CFG_INV_PCLK				BIT(1)
#define   CFG_DUMB_ENA				BIT(0)

/* LCD I/O Pads Control Register */
#define SPU_IOPAD_CONTROL		0x01bc
#define   CFG_VSC_LINEAR(vm)			((vm) << 18)	/* gfx */
#define   CFG_VSC_LINEAR_MASK			0x000c0000
#define   CFG_GRA_VM_ENA			BIT(15)		/* gfx */
#define   CFG_DMA_VM_ENA			BIT(14)		/* video */
#define   CFG_CMD_VM_ENA			BIT(13)
#define   CFG_CSC(csc)				((csc) << 8)
#define   CFG_CSC_MASK				0x00000300
#define   CFG_AXICTRL(axi)			((axi) << 4)
#define   CFG_AXICTRL_MASK			0x000000f0
#define   CFG_IOPADMODE(iopad)			(iopad)
#define   CFG_IOPADMODE_MASK			0x0000000f

/* LCD Interrupt Control Register */
#define SPU_IRQ_ENA			0x1c0
/* LCD Interrupt Status Register */
#define SPU_IRQ_ISR			0x1c4
#define   IRQ_DMA_FRAME0			BIT(31)
#define   IRQ_DMA_FRAME1			BIT(30)
#define   IRQ_DMA_FIFO_UNDERFLOW		BIT(29)
#define   IRQ_GRA_FRAME0			BIT(27)
#define   IRQ_GRA_FRAME1			BIT(26)
#define   IRQ_GRA_FIFO_UNDERFLOW		BIT(25)
#define   IRQ_SMART_VSYNC			BIT(23)
#define   IRQ_DUMB_FRAME_DONE			BIT(22)
#define   IRQ_SMART_FRAME_DONE			BIT(21)
#define   IRQ_HWCURSOR_FRAME_DONE		BIT(20)
#define   IRQ_AHB_CMD_EMPTY			BIT(19)
#define   IRQ_SPI_TRANSFER_DONE			BIT(18)
#define   IRQ_POWERDOWN				BIT(17)
#define   IRQ_AXI_ERROR				BIT(16)
/* read only status */
#define   STA_DMA_FRAME0			BIT(15)
#define   STA_DMA_FRAME1			BIT(14)
#define   STA_DMA_FRAME_COUNT(x) (((x) & (BIT(13) | BIT(12))) >> 12)
#define   STA_GRA_FRAME0			BIT(11)
#define   STA_GRA_FRAME1			BIT(10)
#define   STA_GRA_FRAME_COUNT(x) (((x) & (BIT(9) | BIT(8))) >> 8)
#define   STA_SMART_VSYNC			BIT(7)
#define   STA_DUMB_FRAME_DONE			BIT(6)
#define   STA_SMART_FRAME_DONE			BIT(5)
#define   STA_HWCURSOR_FRAME_DONE		BIT(4)
#define   STA_AHB_CMD_EMPTY			BIT(3)
#define   STA_DMA_FIFO_EMPTY			BIT(2)
#define   STA_GRA_FIFO_EMPTY			BIT(1)
#define   STA_POWERDOWN				BIT(0)

#define IRQ_DMA_FRAME_DONE			(IRQ_DMA_FRAME0 | IRQ_DMA_FRAME1)
#define IRQ_GRA_FRAME_DONE \
			(IRQ_GRA_FRAME0 | IRQ_GRA_FRAME1 | IRQ_SMART_VSYNC)

/*
 * defined Video Memory Color format for DMA control 0 register
 * DMA0 bit[23:20]
 */
#define VMODE_RGB565		0x0
#define VMODE_RGB1555		0x1
#define VMODE_RGB888PACKED	0x2
#define VMODE_RGB888UNPACKED	0x3
#define VMODE_RGBA888		0x4
#define VMODE_YUV422PACKED	0x5
#define VMODE_YUV422PLANAR	0x6
#define VMODE_YUV420PLANAR	0x7
#define VMODE_SMPNCMD		0x8
#define VMODE_PALETTE4BIT	0x9
#define VMODE_PALETTE8BIT	0xa
#define VMODE_RESERVED		0xb

/*
 * defined Graphic Memory Color format for DMA control 0 register
 * DMA0 bit[19:16]
 */
#define GMODE_RGB565		0x0
#define GMODE_RGB1555		0x1
#define GMODE_RGB888PACKED	0x2
#define GMODE_RGB888UNPACKED	0x3
#define GMODE_RGBA888		0x4
#define GMODE_YUV422PACKED	0x5
#define GMODE_YUV422PLANAR	0x6
#define GMODE_YUV420PLANAR	0x7
#define GMODE_SMPNCMD		0x8
#define GMODE_PALETTE4BIT	0x9
#define GMODE_PALETTE8BIT	0xa
#define GMODE_RESERVED		0xb

/*
 * define for DMA control 1 register
 */
#define DMA1_FRAME_TRIG		31	/* bit location */
#define DMA1_VSYNC_MODE		28
#define DMA1_VSYNC_INV		27
#define DMA1_CKEY		24
#define DMA1_CARRY		23
#define DMA1_LNBUF_ENA		22
#define DMA1_GATED_ENA		21
#define DMA1_PWRDN_ENA		20
#define DMA1_DSCALE		18
#define DMA1_ALPHA_MODE		16
#define DMA1_ALPHA		 8
#define DMA1_PXLCMD		 0

/*
 * defined for Configure Dumb Mode
 * DUMB LCD Panel bit[31:28]
 */
#define DUMB16_RGB565_0		0x0
#define DUMB16_RGB565_1		0x1
#define DUMB18_RGB666_0		0x2
#define DUMB18_RGB666_1		0x3
#define DUMB12_RGB444_0		0x4
#define DUMB12_RGB444_1		0x5
#define DUMB24_RGB888_0		0x6
#define DUMB_BLANK		0x7

/*
 * defined for Configure I/O Pin Allocation Mode
 * LCD LCD I/O Pads control register bit[3:0]
 */
#define IOPAD_DUMB24		0x0
#define IOPAD_DUMB18SPI		0x1
#define IOPAD_DUMB18GPIO	0x2
#define IOPAD_DUMB16SPI		0x3
#define IOPAD_DUMB16GPIO	0x4
#define IOPAD_DUMB12		0x5
#define IOPAD_SMART18SPI	0x6
#define IOPAD_SMART16SPI	0x7
#define IOPAD_SMART8BOTH	0x8

/*
 * clock source SCLK_Source bit[31:30]
 */
#define SCLK_SRC_AXI 0
#define SCLK_SRC_EXTCLK0 1
#define SCLK_SRC_PLLDIV 2
#define SCLK_SRC_EXTCLK1 3

/*
 * defined Dumb Panel Clock Divider register
 * SCLK_Source bit[31]
 */
#define AXI_BUS_SEL		0x80000000	/* 0: PLL clock select*/
#define CCD_CLK_SEL		0x40000000
#define DCON_CLK_SEL		0x20000000
#define IDLE_CLK_INT_DIV	0x1		/* idle Integer Divider */
#define DIS_CLK_INT_DIV		0x0		/* Disable Integer Divider */

/* SRAM ID */
#define SRAMID_GAMMA_YR		0x0
#define SRAMID_GAMMA_UG		0x1
#define SRAMID_GAMMA_VB		0x2
#define SRAMID_PALETTE		0x3
#define SRAMID_HWC32_RAM1	0xc
#define SRAMID_HWC32_RAM2	0xd
#define SRAMID_HWC32_RAM3	0xe
#define SRAMID_HWC		0xf

/* SRAM INIT Read/Write */
#define SRAMID_INIT_READ	0x0
#define SRAMID_INIT_WRITE	0x2
#define SRAMID_INIT_DEFAULT	0x3

/*
 * defined VSYNC selection mode for DMA control 1 register
 * DMA1 bit[30:28]
 */
#define VMODE_SMPN		0x0
#define VMODE_SMPNIRQ		0x1
#define VMODE_DUMB		0x2
#define VMODE_IPE		0x3
#define VMODE_IRE		0x4

/*
 * defined Configure Alpha and Alpha mode for DMA control 1 register
 * DMA1 bit[15:08](alpha) / bit[17:16](alpha mode)
 */
/* ALPHA mode */
#define MODE_ALPHA_DMA		0xa0
#define MODE_ALPHA_GRA		0x1
#define MODE_ALPHA_CFG		0x2

/* alpha value */
#define ALPHA_NOGRAPHIC		0xff	/* all video, no graphic */
#define ALPHA_NOVIDEO		0x00	/* all graphic, no video */
#define ALPHA_GRAPHnVIDEO	0x0f	/* Selects graphic & video */

/*
 * defined Pixel Command for DMA control 1 register
 * DMA1 bit[07:00]
 */
#define PIXEL_CMD		0x81

#endif /* _DOVE_LCD_H_ */
