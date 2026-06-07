// Runtime V54: BCM2711 EMMC2/SDHCI register probe (Arasan SDHCI 3.0).
// Runtime V55: Card identification (CMD0/CMD8/ACMD41/CMD2/CMD3, get RCA).
// Runtime V56: Single block read via CMD17; MBR 0x55AA signature verify.
// Runtime V57: FAT32 file read (config.txt) with byte count + checksum.
#include "Support.h"

// BCM2711 EMMC2 (Arasan SDHCI) base address in low-peripheral mode.
#define EMMC2_BASE  0xFE340000UL

// SDHCI standard register offsets (32-bit accesses unless noted).
#define SDHCI_ARG2              0x00
#define SDHCI_BLKSIZECNT        0x04
#define SDHCI_ARG1              0x08
#define SDHCI_CMDTM             0x0C
#define SDHCI_RESP0             0x10
#define SDHCI_RESP1             0x14
#define SDHCI_RESP2             0x18
#define SDHCI_RESP3             0x1C
#define SDHCI_DATA              0x20
#define SDHCI_STATUS            0x24
#define SDHCI_CONTROL0          0x28
#define SDHCI_CONTROL1          0x2C
#define SDHCI_INTERRUPT         0x30
#define SDHCI_IRPT_MASK         0x34
#define SDHCI_IRPT_EN           0x38
#define SDHCI_CONTROL2          0x3C
#define SDHCI_CAPABILITIES0     0x40
#define SDHCI_CAPABILITIES1     0x44
#define SDHCI_FORCE_IRPT        0x50
#define SDHCI_BOOT_TIMEOUT      0x70
#define SDHCI_DBG_SEL           0x74
#define SDHCI_EXRDFIFO_CFG      0x80
#define SDHCI_EXRDFIFO_EN       0x84
#define SDHCI_TUNE_STEP         0x88
#define SDHCI_TUNE_STEPS_STD    0x8C
#define SDHCI_TUNE_STEPS_DDR    0x90
#define SDHCI_SPI_INT_SPT       0xF0
#define SDHCI_SLOTISR_VER       0xFC   // bits[31:16]=host spec ver, bits[15:0]=slot int

// SDHCI_STATUS bits
#define SDHCI_STATUS_CMD_INHIBIT    (1u << 0)
#define SDHCI_STATUS_DATA_INHIBIT   (1u << 1)
#define SDHCI_STATUS_CARD_DETECT    (1u << 16)

// SDHCI_CONTROL1 bits
#define SDHCI_C1_CLK_INTLEN     (1u << 0)   // Internal clock enable
#define SDHCI_C1_CLK_STABLE     (1u << 1)   // Internal clock stable
#define SDHCI_C1_CLK_EN         (1u << 2)   // SD clock enable
#define SDHCI_C1_CLK_GENSEL     (1u << 5)   // Clock generator select (0=divided)
#define SDHCI_C1_CLK_FREQ_MS2   (1u << 6)   // Freq select high bits [9:8]
#define SDHCI_C1_CLK_FREQ8      (1u << 8)   // Freq select bits [7:0]
#define SDHCI_C1_TOUNIT_DIS     (0xEu << 16)// Data timeout unit (0xE = 2^27 cycles)
#define SDHCI_C1_SRST_HC        (1u << 24)  // Software reset (full host)
#define SDHCI_C1_SRST_CMD       (1u << 25)  // Software reset (CMD line)
#define SDHCI_C1_SRST_DATA      (1u << 26)  // Software reset (DATA line)

// SDHCI_CONTROL0 bits
#define SDHCI_C0_HCTL_DWIDTH    (1u << 1)   // 4-bit bus width
#define SDHCI_C0_HCTL_HSPE      (1u << 2)   // High speed enable

// SDHCI_INTERRUPT bits
#define SDHCI_INT_CMD_DONE      (1u << 0)
#define SDHCI_INT_DATA_DONE     (1u << 1)
#define SDHCI_INT_WRITE_RDY     (1u << 4)
#define SDHCI_INT_READ_RDY      (1u << 5)
#define SDHCI_INT_ERROR         (1u << 15)
#define SDHCI_INT_CTO_ERR       (1u << 16)  // CMD timeout
#define SDHCI_INT_CCRC_ERR      (1u << 17)  // CMD CRC error
#define SDHCI_INT_CEND_ERR      (1u << 18)
#define SDHCI_INT_CBAD_ERR      (1u << 19)
#define SDHCI_INT_DTO_ERR       (1u << 20)
#define SDHCI_INT_DCRC_ERR      (1u << 21)
#define SDHCI_INT_DEND_ERR      (1u << 22)
#define SDHCI_INT_ERR_MASK      0xFFFF0000u

// SDHCI command register encoding
#define CMD_RESP_NONE    0x00
#define CMD_RESP_136     0x01  // 136-bit response
#define CMD_RESP_48      0x02  // 48-bit response
#define CMD_RESP_48B     0x03  // 48-bit response with busy
#define CMD_CRC_CHK      (1u << 3)
#define CMD_IXCHK_EN     (1u << 4)
#define CMD_IS_DATA      (1u << 5)
#define CMD_TYPE_NORMAL  0x00
#define CMD_TYPE_ABORT   0x03
// Full command word: (cmd_idx << 24) | (cmd_type << 22) | flags
#define CMDTM_CMD(n, resp, flags) \
    (((unsigned int)(n) << 24) | ((unsigned int)(resp)) | (flags))

// Transfer mode bits (stored in lower 16 bits of CMDTM for data commands)
#define TM_BLKCNT_EN    (1u << 1)
#define TM_AUTO_CMD12   (1u << 2)
#define TM_DAT_DIR_RD   (1u << 4)
#define TM_MULTI_BLOCK  (1u << 5)

// ---- Helpers ---------------------------------------------------------------

static void sdhci_delay_us(unsigned int us) {
    volatile unsigned int i;
    for (i = 0; i < us * 300; i++) {}
}

static int sdhci_wait_bits(unsigned long reg, unsigned int mask, unsigned int val,
                           unsigned int timeout_loops) {
    for (unsigned int i = 0; i < timeout_loops; i++) {
        if ((mmio_read32(EMMC2_BASE + reg) & mask) == val) return 1;
        sdhci_delay_us(1);
    }
    return 0;
}

// ============================================================================
// Runtime V54: register probe
// ============================================================================

int kernel_sdhci_probe_selftest(void) {
    unsigned int cap0 = mmio_read32(EMMC2_BASE + SDHCI_CAPABILITIES0);
    unsigned int ver_word = mmio_read32(EMMC2_BASE + SDHCI_SLOTISR_VER);
    unsigned int host_spec_ver = (ver_word >> 16) & 0xFF;
    // BCM2711 Arasan SDHCI 3.0 reports spec_ver==2; cap0 is non-zero.
    return (cap0 != 0 && host_spec_ver <= 3) ? 1 : 0;
}

unsigned long kernel_sdhci_probe_cap0(void) {
    return (unsigned long)mmio_read32(EMMC2_BASE + SDHCI_CAPABILITIES0);
}

unsigned long kernel_sdhci_probe_host_version(void) {
    unsigned int ver_word = mmio_read32(EMMC2_BASE + SDHCI_SLOTISR_VER);
    return (unsigned long)((ver_word >> 16) & 0xFF);
}
