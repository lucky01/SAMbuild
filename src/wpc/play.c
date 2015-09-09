/************************************************************************************************
 Playmatic (Spain)
 -----------------
  (done by Gaston over the course of a lifetime)

 Playmatic is a nightmare to maintain! Plain and simple.

 They did use at least four, if not five different hardware generations over their years,
 and they're not interchangeable at any point or time.
 The earliest games (like "Chance" from 1978) used a rough 400 kHz clock for the ancient
 CDP 1802 CPU (generated by an R/C combo), and had the IRQ hard-wired to zero cross detection.
 The 2nd generation used a clock chip of 2.95 MHz and an IRQ of about 360 Hz.
 The 3rd generation used the same values, but completely re-wired all output circuits!
 The 4th generation used a generic 3.58 MHz NTSC quartz.

 What's more: the systems are terribly nitpicky about their timing signals!
 Gen. 1 won't work right unless the distribution of ASSERT and CLEAR IRQ states is correct,
 gen. 2 and up need the IRQ line unset by a port write and is also interacting there by Q output,
 and it actually matters whether the EF flags start out with HI or LO levels too!
 Whenever you think you got it right, another issue pops up somewhere else... :)

 Sound started out with 4 simple tones (with fading option), and evolved through a CPU-driven
 oscillator circuit on to complete sound boards with another 1802 CPU.

 Hardware:
 ---------
  CPU:     CDP1802 @ various frequencies (various IRQ freq's as well)
  DISPLAY: gen.1: six-digit panels, some digits shared between players
           gen.2: 5 rows of 7-segment LED panels, direct segment access for alpha digits
  SOUND:   gen.1: discrete (4 tones, like Zaccaria's 1311)
           gen.2: simple tone generator with frequency divider fed by CPU clock
           ZIRA:  like gen.2, plus an additional COP402 @ 2 MHz with 1 x AY8910 sound chip
           gen.3: like gen.2, plus an additional CDP1802 @ 2.95 MHz with 1 x TMS5220 speech chip
           gen.4: CDP1802 @ NTSC clock with 2 x AY8910 @ NTSC/2
 ************************************************************************************************/

#include "driver.h"
#include "core.h"
#include "play.h"
#include "sndbrd.h"
#include "cpu/cdp1802/cdp1802.h"

enum { DISPCOL=1, DISPLAY, SOUND, SWITCH, DIAG, LAMPCOL, LAMP };

/*----------------
/  Local variables
/-----------------*/
static struct {
  int    vblankCount;
  UINT32 solenoids;
  UINT8  sndCmd;
  int    enRl; // Out 3 Bit 4
  int    enDy; // Out 3 Bit 5
  int    enSn; // Out 3 Bit 6
  int    enX; // Out 3 Bit 7
  int    ef[5];
  int    q;
  int    sc;
  int    lampCol;
  int    digitSel;
  int    panelSel;
  int    cpuType;
  int    resetDone;
} locals;

static INTERRUPT_GEN(PLAYMATIC_irq1) {
  locals.vblankCount = (locals.vblankCount + 1) % 8;
  cpu_set_irq_line(PLAYMATIC_CPU, CDP1802_INPUT_LINE_INT, !locals.vblankCount ? ASSERT_LINE : CLEAR_LINE);
  if (!locals.vblankCount) locals.ef[1] = !locals.ef[1];
}

static INTERRUPT_GEN(PLAYMATIC_irq2) {
  if (locals.q) {
    locals.ef[1]= 0;
  } else {
    locals.ef[1] = !locals.ef[1];
  }
}

static void PLAYMATIC_zeroCross2(int data) {
  locals.ef[3] = !locals.ef[3];
  if (!locals.ef[1]) {
    cpu_set_irq_line(PLAYMATIC_CPU, CDP1802_INPUT_LINE_INT, ASSERT_LINE);
    locals.ef[2] = 0;
  }
}

/*-------------------------------
/  copy local data to interface
/--------------------------------*/
static INTERRUPT_GEN(PLAYMATIC_vblank1) {
  /*-- lamps --*/
  memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
  /*-- solenoids --*/
  coreGlobals.solenoids = (coreGlobals.solenoids & 0x0ffff) | (locals.q << 16);

  core_updateSw(core_getSol(17));
}

static INTERRUPT_GEN(PLAYMATIC_vblank2) {
  /*-- lamps --*/
  memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
  /*-- solenoids --*/
  locals.vblankCount = (locals.vblankCount + 1) % 2;
  if (!locals.vblankCount) {
    coreGlobals.solenoids = locals.solenoids;
    locals.solenoids = 0;
  }
  core_updateSw(TRUE);
}

static SWITCH_UPDATE(PLAYMATIC1) {
  if (inports) {
    CORE_SETKEYSW(inports[CORE_COREINPORT] >> 8, 0xe0, 6);
    CORE_SETKEYSW(inports[CORE_COREINPORT], 0x3f, 7);
  }
  locals.ef[2] = core_getDip(0) & 1;
  locals.ef[3] = (core_getDip(0) >> 1) & 1;
  locals.ef[4] = (core_getDip(0) >> 2) & 1;
}

static SWITCH_UPDATE(PLAYMATIC2) {
  if (inports) {
    CORE_SETKEYSW(inports[CORE_COREINPORT], locals.cpuType < 2 ? 0xef : (locals.cpuType < 3 ? 0xf7 : 0xdf), 1);
    CORE_SETKEYSW(inports[CORE_COREINPORT] >> 8, 0xff, 0);
  }
  locals.ef[4] = !(coreGlobals.swMatrix[0] & 1);
  locals.resetDone = 1;
}

static WRITE_HANDLER(disp_w) {
  coreGlobals.segments[offset].w = data & 0x7f;
  coreGlobals.segments[48 + offset / 8].w = data & 0x80 ? core_bcd2seg7[0] : 0;
}

static int bitColToNum(int tmp)
{
  int i, data;
  i = data = 0;
  while(tmp) {
    if (tmp & 1) data = i;
    tmp = tmp >> 1;
    i++;
  }
  return data;
}

static WRITE_HANDLER(out1_n) {
  static UINT8 n2data;
  int p = locals.ef[1];
  switch (offset) {
    case 1: // match & credits displays
      coreGlobals.segments[18].w = data & 1 ? core_bcd2seg7[1] : 0;
      coreGlobals.segments[20-p].w = core_bcd2seg7[data >> 4];
      coreGlobals.segments[21].w = data & 2 ? core_bcd2seg7[0] : 0;
      coreGlobals.segments[22].w = data & 2 ? core_bcd2seg7[0] : 0;
      break;
    case 2: // display / sound data
      n2data = data;
      break;
    case 3:
      locals.digitSel = data ^ 0x0f;
      if (locals.digitSel > 1) { // score displays
        coreGlobals.segments[(locals.digitSel-2)*2].w = core_bcd2seg7[n2data >> 4];
        coreGlobals.segments[(locals.digitSel-2)*2 + 1].w = core_bcd2seg7[n2data & 0x0f];
        coreGlobals.segments[2].w |= 0x80;
        coreGlobals.segments[8].w |= 0x80;
        coreGlobals.segments[12].w |= 0x80;
        coreGlobals.segments[16].w |= 0x80;
      } else if (locals.digitSel) { // sound & player up lights
        sndbrd_0_data_w(0, n2data);
        coreGlobals.tmpLampMatrix[6] = (1 << (n2data >> 5)) >> 1;
      } else { // solenoids
        coreGlobals.solenoids = (coreGlobals.solenoids & 0x10000) | n2data;
      }
      break;
    case 4:
      coreGlobals.tmpLampMatrix[p] = data;
      break;
    case 5:
      coreGlobals.tmpLampMatrix[p+2] = data;
      break;
    case 6:
      coreGlobals.tmpLampMatrix[p+4] = data;
      break;
  }
  logerror("out: %d:%02x\n", offset, data);
}

static READ_HANDLER(in1_n) {
  UINT8 data = coreGlobals.swMatrix[offset];
  return offset > 5 ? ~data : data;
}

static WRITE_HANDLER(out2_n) {
  static int outports[4][7] =
    {{ DISPCOL, DISPLAY, SOUND, SWITCH, DIAG, LAMPCOL, LAMP },
     { DISPCOL, DISPLAY, SOUND, SWITCH, DIAG, LAMPCOL, LAMP },
     { DISPCOL, LAMPCOL, LAMP, SWITCH, DIAG, DISPLAY, SOUND },
     { DISPCOL, LAMPCOL, LAMP, SWITCH, DIAG, DISPLAY, SOUND }};
  const int out = outports[locals.cpuType][offset-1];
  UINT8 abcData, lampData;
  int enable;
  switch (out) {
    case DISPCOL:
      if (core_gameData->hw.soundBoard == SNDBRD_PLAY2) { // used for fading out the sound
        sndbrd_0_ctrl_w(0, data >> 7);
      }
      if (!(data & 0x7f))
        locals.panelSel = 0;
      else
        locals.digitSel = bitColToNum(data & 0x7f);
      break;
    case DISPLAY:
      disp_w(8 * (locals.panelSel++) + locals.digitSel, locals.sc ? data : 0);
      break;
    case SOUND:
      if (core_gameData->hw.soundBoard == SNDBRD_PLAY2) {
        sndbrd_0_data_w(0, data);
      }
      break;
    case LAMPCOL:
      locals.lampCol = data;
      break;
    case LAMP:
      abcData = locals.lampCol & 0x07;
      locals.enRl = (data >> 4) & 1;
      locals.enDy = (data >> 5) & 1;
      locals.enSn = (data >> 6) & 1;
      locals.enX = (data >> 7) & 1;
      lampData = (data & 0x0f) ^ 0x0f;
      enable = locals.cpuType < 2 ? !locals.enX : !locals.enRl;
      if (enable) {
        if (!locals.ef[3])
          coreGlobals.tmpLampMatrix[abcData] = (coreGlobals.tmpLampMatrix[abcData] & 0xf0) | lampData;
        else
          coreGlobals.tmpLampMatrix[abcData] = (coreGlobals.tmpLampMatrix[abcData] & 0x0f) | (lampData << 4);
        if ((locals.lampCol & 0x08)) {
          locals.solenoids |= 1 << abcData;
          coreGlobals.solenoids = locals.solenoids;
        }
        if ((locals.lampCol & 0x10)) {
          locals.solenoids |= 0x100 << abcData;
          coreGlobals.solenoids = locals.solenoids;
        }
      }
      if (core_gameData->hw.soundBoard == SNDBRD_PLAY3 || core_gameData->hw.soundBoard == SNDBRD_PLAYZ) {
//if (locals.sndCmd != (data & 0x70)) printf("\nc:%02x\n", data & 0x70);
        locals.sndCmd = data & 0x70;
        sndbrd_0_ctrl_w(0, locals.sndCmd);
      } else if (core_gameData->hw.soundBoard == SNDBRD_PLAY4 && !locals.enSn) {
        locals.sndCmd = locals.lampCol;
        sndbrd_0_data_w(0, locals.sndCmd);
        sndbrd_0_ctrl_w(0, locals.enSn);
      }
      cpu_set_irq_line(PLAYMATIC_CPU, CDP1802_INPUT_LINE_INT, CLEAR_LINE);
      locals.ef[2] = 1;
      break;
  }
}

static READ_HANDLER(in2_n) {
  switch (offset) {
    case SWITCH:
      if (locals.digitSel < 6)
        return coreGlobals.swMatrix[locals.digitSel+2];
      else
        printf("digitSel = %d!\n", locals.digitSel);
      break;
    case DIAG:
      if (locals.cpuType > 1) {
        if (locals.digitSel == 1)
          return coreGlobals.swMatrix[0] ^ 0x0f;
        else
          return coreGlobals.swMatrix[1] ^ 0x0f;
      } else
        return ~coreGlobals.swMatrix[1];
      break;
    default:
      logerror("unknown in_%d read\n", offset);
  }
  return 0;
}

static UINT8 in_mode(void) { return locals.resetDone ? CDP1802_MODE_RUN : CDP1802_MODE_RESET; }

static void out_q(int level) {
  locals.q = level;
  /* connected to RST1 pin of flip flop U2 on cpu types 1 and above */
  if (locals.cpuType && locals.q) {
    locals.ef[1] = 0;
  }
}

static UINT8 in_ef(void) { return locals.ef[1] | (locals.ef[2] << 1) | (locals.ef[3] << 2) | (locals.ef[4] << 3); }

static void out_sc(int data) {
  locals.sc = data & 1;
}

static CDP1802_CONFIG play1802_config =
{
  in_mode,  // MODE
  in_ef,    // EF
  out_sc,   // SC
  out_q,    // Q
  NULL,     // DMA read
  NULL      // DMA write
};

static void init_common(int cpuType) {
  memset(&locals, 0, sizeof locals);
  sndbrd_0_init(core_gameData->hw.soundBoard, 1, memory_region(REGION_CPU2), NULL, NULL);
  locals.cpuType = cpuType;
  if (cpuType) locals.ef[3] = 1; else locals.resetDone = 1;
}

static MACHINE_INIT(PLAYMATIC1) {
  init_common(0);
}

static MACHINE_INIT(PLAYMATIC2) {
  init_common(1);
}

static MACHINE_INIT(PLAYMATIC3) {
  init_common(2);
}

static MACHINE_INIT(PLAYMATIC4) {
  init_common(3);
}

static MACHINE_STOP(PLAYMATIC) {
  sndbrd_0_exit();
}

static PORT_READ_START(PLAYMATIC_readport1)
  {0x00,0x07, in1_n},
MEMORY_END

static PORT_READ_START(PLAYMATIC_readport2)
  {0x00,0x07, in2_n},
MEMORY_END

static PORT_WRITE_START(PLAYMATIC_writeport1)
  {0x00,0x07, out1_n},
MEMORY_END

static PORT_WRITE_START(PLAYMATIC_writeport2)
  {0x00,0x07, out2_n},
MEMORY_END

static MEMORY_READ_START(PLAYMATIC_readmem1)
  {0x0000,0x07ff, MRA_ROM},
  {0x0800,0x081f, MRA_RAM},
  {0x0c00,0x0c1f, MRA_RAM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem1)
  {0x0800,0x081f, MWA_RAM, &generic_nvram, &generic_nvram_size},
  {0x0c00,0x0c1f, MWA_RAM},
MEMORY_END

static MEMORY_READ_START(PLAYMATIC_readmem1a)
  {0x0000,0x09ff, MRA_ROM},
  {0x0c00,0x0c1f, MRA_RAM},
  {0x0e00,0x0e1f, MRA_RAM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem1a)
  {0x0c00,0x0c1f, MWA_RAM},
  {0x0e00,0x0e1f, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

static MEMORY_READ_START(PLAYMATIC_readmem2)
  {0x0000,0x1fff, MRA_ROM},
  {0x2000,0x20ff, MRA_RAM},
  {0xa000,0xafff, MRA_ROM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem2)
  {0x0000,0x00ff, MWA_NOP},
  {0x2000,0x20ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

static MEMORY_READ_START(PLAYMATIC_readmem3)
  {0x0000,0x7fff, MRA_ROM},
  {0x8000,0x80ff, MRA_RAM},
MEMORY_END

static MEMORY_WRITE_START(PLAYMATIC_writemem3)
  {0x0000,0x00ff, MWA_NOP},
  {0x8000,0x80ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

static int play_sw2m(int no) { return 8+(no/10)*8+(no%10-1); }
static int play_m2sw(int col, int row) { return (col-1)*10+row+1; }

MACHINE_DRIVER_START(PLAYMATIC1)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CPU_ADD_TAG("mcpu", CDP1802, 400000)
  MDRV_CPU_MEMORY(PLAYMATIC_readmem1, PLAYMATIC_writemem1)
  MDRV_CPU_PORTS(PLAYMATIC_readport1, PLAYMATIC_writeport1)
  MDRV_CPU_CONFIG(play1802_config)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq1, 800) // actually 100 Hz (zc freq.) but needs uneven distribution of HI and LO states!
  MDRV_CPU_VBLANK_INT(PLAYMATIC_vblank1, 1)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC1,NULL,PLAYMATIC)
  MDRV_SWITCH_UPDATE(PLAYMATIC1)
  MDRV_SWITCH_CONV(play_sw2m, play_m2sw)
  MDRV_DIPS(3)
  MDRV_NVRAM_HANDLER(generic_0fill)
  MDRV_IMPORT_FROM(PLAYMATICS1)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC1A)
  MDRV_IMPORT_FROM(PLAYMATIC1)
  MDRV_CPU_MODIFY("mcpu")
  MDRV_CPU_MEMORY(PLAYMATIC_readmem1a, PLAYMATIC_writemem1a)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START(PLAYMATIC2NS)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CPU_ADD_TAG("mcpu", CDP1802, 2950000)
  MDRV_CPU_MEMORY(PLAYMATIC_readmem2, PLAYMATIC_writemem2)
  MDRV_CPU_PORTS(PLAYMATIC_readport2, PLAYMATIC_writeport2)
  MDRV_CPU_CONFIG(play1802_config)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq2, 2950000/8192)
  MDRV_TIMER_ADD(PLAYMATIC_zeroCross2, 100)
  MDRV_CPU_VBLANK_INT(PLAYMATIC_vblank2, 1)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC2,NULL,PLAYMATIC)
  MDRV_SWITCH_UPDATE(PLAYMATIC2)
  MDRV_SWITCH_CONV(play_sw2m, play_m2sw)
  MDRV_NVRAM_HANDLER(generic_0fill)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC2)
  MDRV_IMPORT_FROM(PLAYMATIC2NS)
  MDRV_IMPORT_FROM(PLAYMATICS2)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC2SZ)
  MDRV_IMPORT_FROM(PLAYMATIC2NS)
  MDRV_IMPORT_FROM(PLAYMATICSZ)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC2S3)
  MDRV_IMPORT_FROM(PLAYMATIC2NS)
  MDRV_IMPORT_FROM(PLAYMATICS3)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC2S4)
  MDRV_IMPORT_FROM(PLAYMATIC2NS)
  MDRV_IMPORT_FROM(PLAYMATICS4)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START(PLAYMATIC3)
  MDRV_IMPORT_FROM(PLAYMATIC2NS)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC3,NULL,PLAYMATIC)
  MDRV_CPU_MODIFY("mcpu");
  MDRV_CPU_MEMORY(PLAYMATIC_readmem3, PLAYMATIC_writemem3)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC3S3)
  MDRV_IMPORT_FROM(PLAYMATIC3)
  MDRV_IMPORT_FROM(PLAYMATICS3)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC3S4)
  MDRV_IMPORT_FROM(PLAYMATIC3)
  MDRV_IMPORT_FROM(PLAYMATICS4)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(PLAYMATIC4)
  MDRV_IMPORT_FROM(PLAYMATIC3)
  MDRV_CORE_INIT_RESET_STOP(PLAYMATIC4,NULL,PLAYMATIC)
  MDRV_CPU_REPLACE("mcpu", CDP1802, 3579545)
  MDRV_CPU_PERIODIC_INT(PLAYMATIC_irq2, 3579545/8192)
  MDRV_IMPORT_FROM(PLAYMATICS4)
MACHINE_DRIVER_END
