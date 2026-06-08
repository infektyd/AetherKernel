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

// SDHCI command register encoding.
// Response type goes into bits[17:16] of CMDTM; control bits at [20:19].
#define CMD_RESP_NONE    0x00000000u
#define CMD_RESP_136     (1u << 16)    // 136-bit response (R2)
#define CMD_RESP_48      (2u << 16)    // 48-bit response (R1, R3, R6, R7)
#define CMD_RESP_48B     (3u << 16)    // 48-bit with busy (R1b)
#define CMD_CRC_CHK      (1u << 19)    // CRC check enable
#define CMD_IXCHK_EN     (1u << 20)    // Index check enable
#define CMD_IS_DATA      (1u << 21)    // Data present select
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

// ============================================================================
// Runtime V55: Card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)
// ============================================================================

// Stored card state (populated once by kernel_sdhci_card_init).
static int          sdhci_card_init_ok = 0;
static unsigned int sdhci_card_rca     = 0;
static unsigned int sdhci_card_ocr     = 0;
static unsigned int sdhci_card_is_hc   = 0; // 1 = SDHC/SDXC (block addressing)

// SD bus power control bits in CONTROL0.
#define SDHCI_C0_BUS_PWR        (1u << 8)
#define SDHCI_C0_BUS_VLT_33V    (7u << 9)   // 3.3V

// --- internal helpers -------------------------------------------------------

static int sdhci_hard_reset(void) {
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1,
                 mmio_read32(EMMC2_BASE + SDHCI_CONTROL1) | SDHCI_C1_SRST_HC);
    return sdhci_wait_bits(SDHCI_CONTROL1, SDHCI_C1_SRST_HC, 0, 100000);
}

// Set SDHCI divided clock to a target frequency.
// base_mhz: base clock in MHz (from CAPABILITIES0 bits[15:8]).
// target_khz: desired SD clock frequency in kHz.
static int sdhci_set_clock(unsigned int base_mhz, unsigned int target_khz) {
    unsigned int base_khz = base_mhz * 1000u;
    unsigned int n = base_khz / (2u * target_khz);
    if (n == 0) n = 1;
    if (n > 0x3FFu) n = 0x3FFu;

    // Disable SD clock before changing frequency.
    unsigned int c1 = mmio_read32(EMMC2_BASE + SDHCI_CONTROL1);
    c1 &= ~SDHCI_C1_CLK_EN;
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1, c1);
    sdhci_delay_us(20);

    // Program divisor: lower 8 bits → bits[15:8], upper 2 bits → bits[7:6].
    c1 &= ~0x0000FFE0u;                             // clear old freq + gensel
    c1 |= ((n & 0xFF) << 8) | (((n >> 8) & 0x3) << 6) | SDHCI_C1_CLK_INTLEN;
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1, c1);

    if (!sdhci_wait_bits(SDHCI_CONTROL1, SDHCI_C1_CLK_STABLE, SDHCI_C1_CLK_STABLE, 20000))
        return 0;

    c1 = mmio_read32(EMMC2_BASE + SDHCI_CONTROL1);
    c1 |= SDHCI_C1_CLK_EN;
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1, c1);
    sdhci_delay_us(10);
    return 1;
}

// Send a command; poll for CMD_DONE or ERROR.
// resp[4] receives RESP0..RESP3; pass NULL for no-response commands.
// Returns 1 on success, 0 on error/timeout.
static int sdhci_send_cmd(unsigned int cmdtm, unsigned int arg, unsigned int resp[4]) {
    // Wait for CMD line not inhibited.
    if (!sdhci_wait_bits(SDHCI_STATUS, SDHCI_STATUS_CMD_INHIBIT, 0, 200000))
        return 0;
    // Data commands also require the DAT line to be idle.
    if ((cmdtm & CMD_IS_DATA) &&
        !sdhci_wait_bits(SDHCI_STATUS, SDHCI_STATUS_DATA_INHIBIT, 0, 200000))
        return 0;

    // Clear all interrupt flags before issuing command.
    mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, 0xFFFFFFFFu);

    mmio_write32(EMMC2_BASE + SDHCI_ARG1,  arg);
    mmio_write32(EMMC2_BASE + SDHCI_CMDTM, cmdtm);

    // Poll until CMD_DONE or any error bit.
    unsigned int irpt = 0;
    for (unsigned int i = 0; i < 200000; i++) {
        irpt = mmio_read32(EMMC2_BASE + SDHCI_INTERRUPT);
        if (irpt & (SDHCI_INT_CMD_DONE | SDHCI_INT_ERROR | SDHCI_INT_ERR_MASK))
            break;
        sdhci_delay_us(1);
    }

    // Any error → failure.
    if ((irpt & SDHCI_INT_ERROR) || (irpt & SDHCI_INT_ERR_MASK) ||
        !(irpt & SDHCI_INT_CMD_DONE)) {
        mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, 0xFFFFFFFFu);
        return 0;
    }

    if (resp) {
        resp[0] = mmio_read32(EMMC2_BASE + SDHCI_RESP0);
        resp[1] = mmio_read32(EMMC2_BASE + SDHCI_RESP1);
        resp[2] = mmio_read32(EMMC2_BASE + SDHCI_RESP2);
        resp[3] = mmio_read32(EMMC2_BASE + SDHCI_RESP3);
    }

    mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, SDHCI_INT_CMD_DONE);
    return 1;
}

// --- public v55 API ---------------------------------------------------------

int kernel_sdhci_card_init(void) {
    unsigned int resp[4];
    unsigned int cap0    = mmio_read32(EMMC2_BASE + SDHCI_CAPABILITIES0);
    unsigned int base_mhz = (cap0 >> 8) & 0xFF; // base clock MHz from CAP0[15:8]
    if (base_mhz == 0) base_mhz = 100;           // safe fallback

    // 1. Full host controller reset.
    if (!sdhci_hard_reset()) return 0;

    // 2. Set timeout and start internal clock at 400 kHz (identification speed).
    unsigned int c1 = SDHCI_C1_TOUNIT_DIS | SDHCI_C1_CLK_INTLEN;
    unsigned int n  = (base_mhz * 1000u) / (2u * 400u); // divisor for 400 kHz
    if (n == 0) n = 1;
    c1 |= ((n & 0xFF) << 8) | (((n >> 8) & 0x3) << 6);
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1, c1);
    if (!sdhci_wait_bits(SDHCI_CONTROL1, SDHCI_C1_CLK_STABLE, SDHCI_C1_CLK_STABLE, 20000))
        return 0;

    // Enable SD clock.
    c1 = mmio_read32(EMMC2_BASE + SDHCI_CONTROL1);
    c1 |= SDHCI_C1_CLK_EN;
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL1, c1);
    sdhci_delay_us(100);

    // 3. Enable 3.3V SD bus power.
    mmio_write32(EMMC2_BASE + SDHCI_CONTROL0,
                 SDHCI_C0_BUS_VLT_33V | SDHCI_C0_BUS_PWR);
    sdhci_delay_us(5000); // 5 ms power-up delay

    // Route interrupts to status register only (no IRQ line).
    mmio_write32(EMMC2_BASE + SDHCI_IRPT_MASK, 0xFFFFFFFFu);
    mmio_write32(EMMC2_BASE + SDHCI_IRPT_EN,   0x00000000u);

    // 4. CMD0: GO_IDLE_STATE — no response.
    sdhci_send_cmd(CMDTM_CMD(0, CMD_RESP_NONE, 0), 0, (void *)0);
    sdhci_delay_us(2000);

    // 5. CMD8: SEND_IF_COND — verify voltage and SD v2 support.
    //    Arg: VHS=0x1 (2.7-3.6V), check pattern=0xAA → 0x000001AA.
    if (!sdhci_send_cmd(CMDTM_CMD(8, CMD_RESP_48, CMD_CRC_CHK | CMD_IXCHK_EN),
                        0x000001AAu, resp))
        return 0; // CMD8 timeout → SD v1 or no card; not supported
    if ((resp[0] & 0xFFFu) != 0x1AAu)
        return 0; // echo-back mismatch

    // 6. ACMD41 loop: CMD55 + ACMD41 until card power-up complete.
    //    HCS=1 (bit30) requests SDHC/SDXC card; voltage range 0xFF8000.
    unsigned int ocr = 0;
    for (int retry = 0; retry < 500; retry++) {
        // CMD55: APP_CMD — prepares card for ACMD; RCA=0 before identification.
        if (!sdhci_send_cmd(CMDTM_CMD(55, CMD_RESP_48, CMD_CRC_CHK | CMD_IXCHK_EN),
                            0, resp))
            return 0;
        // ACMD41: SD_SEND_OP_COND — R3 response (no CRC/index check).
        if (!sdhci_send_cmd(CMDTM_CMD(41, CMD_RESP_48, 0), 0x40FF8000u, resp))
            return 0;
        ocr = resp[0];
        if (ocr & (1u << 31)) break;  // card power-up complete
        sdhci_delay_us(2000);
        if (retry == 499) return 0;   // 1-second timeout
    }
    sdhci_card_ocr    = ocr;
    sdhci_card_is_hc  = (ocr & (1u << 30)) ? 1u : 0u;

    // 7. CMD2: ALL_SEND_CID — 136-bit R2 response (CID, not parsed here).
    if (!sdhci_send_cmd(CMDTM_CMD(2, CMD_RESP_136, 0), 0, resp))
        return 0;

    // 8. CMD3: SEND_RELATIVE_ADDR — card publishes its RCA.
    if (!sdhci_send_cmd(CMDTM_CMD(3, CMD_RESP_48, CMD_CRC_CHK | CMD_IXCHK_EN),
                        0, resp))
        return 0;
    sdhci_card_rca    = (resp[0] >> 16) & 0xFFFFu;
    sdhci_card_init_ok = 1;
    return 1;
}

int kernel_sdhci_card_init_selftest(void) {
    return sdhci_card_init_ok;
}

unsigned long kernel_sdhci_card_rca(void) {
    return (unsigned long)sdhci_card_rca;
}

unsigned long kernel_sdhci_card_ocr(void) {
    return (unsigned long)sdhci_card_ocr;
}

// ============================================================================
// Runtime V56: Single block read via CMD17; MBR 0x55AA verification.
// ============================================================================

static int          sdhci_mbr_ok    = 0;
static unsigned int sdhci_mbr_magic = 0; // bits[15:0] of word[127] >> 16

// CMD7: SELECT_CARD — moves card from STAND-BY to TRANSFER state.
// R1b response: waits for DAT0 busy to clear.
static int sdhci_card_select(void) {
    unsigned int resp[4];
    if (!sdhci_send_cmd(CMDTM_CMD(7, CMD_RESP_48B, CMD_CRC_CHK | CMD_IXCHK_EN),
                        (unsigned int)(sdhci_card_rca) << 16, resp))
        return 0;
    // R1b: card holds DAT0 low while busy; wait for DATA_INHIBIT to clear.
    return sdhci_wait_bits(SDHCI_STATUS, SDHCI_STATUS_DATA_INHIBIT, 0, 200000);
}

// PIO read of one 512-byte sector into buf[128 words].
static int sdhci_read_block_pio(unsigned int lba, unsigned int buf[128]) {
    unsigned int resp[4];
    unsigned int arg = sdhci_card_is_hc ? lba : (lba * 512u);

    // Block size = 512 bytes, block count = 1.
    mmio_write32(EMMC2_BASE + SDHCI_BLKSIZECNT, (1u << 16) | 512u);

    // CMD17: READ_SINGLE_BLOCK — R1, data present, read direction.
    if (!sdhci_send_cmd(
            CMDTM_CMD(17, CMD_RESP_48, CMD_CRC_CHK | CMD_IXCHK_EN | CMD_IS_DATA | TM_DAT_DIR_RD),
            arg, resp))
        return 0;

    // Wait for READ_RDY or error.
    unsigned int irpt = 0;
    for (unsigned int i = 0; i < 200000; i++) {
        irpt = mmio_read32(EMMC2_BASE + SDHCI_INTERRUPT);
        if (irpt & (SDHCI_INT_READ_RDY | SDHCI_INT_ERROR | SDHCI_INT_ERR_MASK))
            break;
        sdhci_delay_us(1);
    }
    if (!(irpt & SDHCI_INT_READ_RDY) || (irpt & (SDHCI_INT_ERROR | SDHCI_INT_ERR_MASK))) {
        mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, 0xFFFFFFFFu);
        return 0;
    }
    mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, SDHCI_INT_READ_RDY);

    // Read 128 words (512 bytes) from the SDHCI internal buffer.
    for (unsigned int i = 0; i < 128; i++) {
        buf[i] = mmio_read32(EMMC2_BASE + SDHCI_DATA);
    }

    // Wait for DATA_DONE.
    irpt = 0;
    for (unsigned int i = 0; i < 200000; i++) {
        irpt = mmio_read32(EMMC2_BASE + SDHCI_INTERRUPT);
        if (irpt & (SDHCI_INT_DATA_DONE | SDHCI_INT_ERROR | SDHCI_INT_ERR_MASK))
            break;
        sdhci_delay_us(1);
    }
    if (!(irpt & SDHCI_INT_DATA_DONE) || (irpt & (SDHCI_INT_ERROR | SDHCI_INT_ERR_MASK))) {
        mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, 0xFFFFFFFFu);
        return 0;
    }
    mmio_write32(EMMC2_BASE + SDHCI_INTERRUPT, SDHCI_INT_DATA_DONE);
    return 1;
}

int kernel_sdhci_block_read(void) {
    if (!sdhci_card_init_ok) return 0;

    // Select card: STAND-BY → TRANSFER state.
    if (!sdhci_card_select()) return 0;

    // Read sector 0 (MBR).
    unsigned int buf[128];
    if (!sdhci_read_block_pio(0, buf)) return 0;

    // MBR boot signature: byte[510]=0x55, byte[511]=0xAA.
    // In little-endian 32-bit word[127]: bytes are {[508],[509],[510],[511]}.
    // (buf[127] >> 16) extracts bytes [510:511] as a 16-bit value = 0xAA55.
    sdhci_mbr_magic = (buf[127] >> 16) & 0xFFFFu;
    if (sdhci_mbr_magic == 0xAA55u)
        sdhci_mbr_ok = 1;
    return sdhci_mbr_ok;
}

int kernel_sdhci_block_read_selftest(void) { return sdhci_mbr_ok; }
unsigned long kernel_sdhci_mbr_magic(void)  { return (unsigned long)sdhci_mbr_magic; }
