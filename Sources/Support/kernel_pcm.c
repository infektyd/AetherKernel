// Runtime V142: BCM2711 PCM/I2S clock + CS enable after GENET.
// New unused block (0xFE203000). Fail-closed CM_PCM OSC + PCM_CS EN
// write+readback + leftover restore. TXON/RXON/DMAEN stay clear.
// No pin-mux. No FIFO. No output/audio claim.
// Does not write PWM or GPIO event-detect registers.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define PCM_BASE  0xFE203000UL
#define CM_PCMCTL 0xFE101098UL
#define CM_PCMDIV 0xFE10109CUL

#define G32(off) (*(volatile uint32_t *)(PCM_BASE + (unsigned long)(off)))
#define CM32(addr) (*(volatile uint32_t *)(addr))

#define PCM_CS 0x00U

#define PCM_CS_EN    (1U << 0)
#define PCM_CS_RXON  (1U << 1)
#define PCM_CS_TXON  (1U << 2)
#define PCM_CS_DMAEN (1U << 9)
/* Programmable leftover: EN/RXON/TXON/DMAEN. RXD/TXW/SYNC status is live. */
#define PCM_CS_PROG  (PCM_CS_EN | PCM_CS_RXON | PCM_CS_TXON | PCM_CS_DMAEN)

#define CM_PASSWD  0x5A000000U
#define CM_SRC_OSC 1U
#define CM_ENAB    (1U << 4)
#define CM_BUSY    (1U << 7)
#define CM_SRC_MASK  0x0FU
#define CM_PROG      (CM_ENAB | CM_SRC_MASK)

#define PCM_DIVI       50U
#define PCM_WAIT_TICKS 108000UL /* ~2ms @ 54MHz CNTPCT */

static int pcm_probed;
static int pcm_ok_val;
static unsigned int pcm_clk_val;
static unsigned int pcm_en_val;
static unsigned int pcm_restore_val;

static int pcm_wait_busy(unsigned int want) {
    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < PCM_WAIT_TICKS) {
        unsigned int ctl = CM32(CM_PCMCTL);
        if (ctl == 0xFFFFFFFFU) return 0;
        if (want) {
            if ((ctl & CM_BUSY) != 0U) return 1;
        } else if ((ctl & CM_BUSY) == 0U) {
            return 1;
        }
    }
    return 0;
}

int kernel_pcm_selftest(void) {
    if (pcm_probed) return pcm_ok_val;
    pcm_probed = 1;
    pcm_ok_val = 0;
    pcm_clk_val = 0;
    pcm_en_val = 0;
    pcm_restore_val = 0;

    unsigned int saved_cm_ctl = CM32(CM_PCMCTL);
    unsigned int saved_cm_div = CM32(CM_PCMDIV);
    unsigned int saved_cs = G32(PCM_CS);
    if (saved_cm_ctl == 0xFFFFFFFFU || saved_cm_div == 0xFFFFFFFFU || saved_cs == 0xFFFFFFFFU) {
        return 0;
    }

    CM32(CM_PCMCTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    if (pcm_wait_busy(0) == 0) {
        CM32(CM_PCMCTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);
        return 0;
    }

    CM32(CM_PCMDIV) = CM_PASSWD | (PCM_DIVI << 12);
    CM32(CM_PCMCTL) = CM_PASSWD | CM_ENAB | CM_SRC_OSC;
    if (pcm_wait_busy(1) != 0) pcm_clk_val = 1;

    if (pcm_clk_val == 1U) {
        unsigned int want = (saved_cs | PCM_CS_EN) & ~(PCM_CS_RXON | PCM_CS_TXON | PCM_CS_DMAEN);
        G32(PCM_CS) = want;
        unsigned int cs = G32(PCM_CS);
        if ((cs & PCM_CS_EN) != 0U && (cs & (PCM_CS_RXON | PCM_CS_TXON | PCM_CS_DMAEN)) == 0U) {
            pcm_en_val = 1;
        }
    }

    G32(PCM_CS) = saved_cs;
    CM32(CM_PCMCTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    (void)pcm_wait_busy(0);
    CM32(CM_PCMDIV) = CM_PASSWD | (saved_cm_div & 0x00FFFFFFU);
    CM32(CM_PCMCTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);

    if ((G32(PCM_CS) & PCM_CS_PROG) == (saved_cs & PCM_CS_PROG) &&
        (CM32(CM_PCMCTL) & CM_PROG) == (saved_cm_ctl & CM_PROG) &&
        (CM32(CM_PCMDIV) & 0x00FFFFFFU) == (saved_cm_div & 0x00FFFFFFU)) {
        pcm_restore_val = 1;
    }

    if (pcm_clk_val == 1U && pcm_en_val == 1U && pcm_restore_val == 1U) {
        pcm_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_pcm_ok(void)      { return pcm_ok_val; }
unsigned int kernel_pcm_clk(void)     { return pcm_clk_val; }
unsigned int kernel_pcm_en(void)      { return pcm_en_val; }
unsigned int kernel_pcm_restore(void) { return pcm_restore_val; }
