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
static int sdhci_card_selected = 0; // 1 = card is in Transfer State

static int sdhci_card_select(void) {
    unsigned int resp[4];
    if (!sdhci_send_cmd(CMDTM_CMD(7, CMD_RESP_48B, CMD_CRC_CHK | CMD_IXCHK_EN),
                        (unsigned int)(sdhci_card_rca) << 16, resp))
        return 0;
    // R1b: card holds DAT0 low while busy; wait for DATA_INHIBIT to clear.
    if (!sdhci_wait_bits(SDHCI_STATUS, SDHCI_STATUS_DATA_INHIBIT, 0, 200000))
        return 0;
    sdhci_card_selected = 1;
    return 1;
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

// ============================================================================
// Runtime V57: FAT32 file read — locate partition, parse BPB, find
// config.txt in root directory, read file data, emit bytes + checksum.
// ============================================================================

// Shared 512-byte scratch buffer for FAT32 sector reads (no stack alloc needed).
static unsigned int sdhci_fat32_buf[128];

// FAT32 result state.
static int          fat32_ok         = 0;
static unsigned int fat32_file_bytes = 0;
static unsigned int fat32_checksum   = 0;
static unsigned int fat32_last_step  = 0; // diagnostic: last step reached
static unsigned int fat32_diag_name0 = 0; // diagnostic: first byte of first non-skip dir entry name

// FAT32 derived BPB parameters (filled during fat32_read).
static unsigned int fat32_sec_per_clus = 0;
static unsigned int fat32_data_lba     = 0;
static unsigned int fat32_root_clus    = 0;
static unsigned int fat32_fat_lba      = 0; // first FAT table start LBA

// Helpers: read byte/u16/u32 from a 512-byte block buffer (little-endian).
static unsigned int fat32_byte(const unsigned int *buf, unsigned int off) {
    return (buf[off >> 2] >> ((off & 3u) << 3)) & 0xFFu;
}
static unsigned int fat32_u16(const unsigned int *buf, unsigned int off) {
    return fat32_byte(buf, off) | (fat32_byte(buf, off + 1u) << 8);
}
static unsigned int fat32_u32(const unsigned int *buf, unsigned int off) {
    return fat32_u16(buf, off) | (fat32_u16(buf, off + 2u) << 16);
}

// Return the first LBA of cluster N.
static unsigned int fat32_clus_lba(unsigned int clus) {
    return fat32_data_lba + (clus - 2u) * fat32_sec_per_clus;
}

// Scan one directory sector (sdhci_fat32_buf) for an 8.3 short name (11 bytes,
// uppercase, space-padded, e.g. "CONFIG  TXT").
// Returns  1  = found; fills *clus_out and *size_out.
// Returns  0  = not found in this sector, continue scanning.
// Returns -1  = end-of-directory (first byte == 0x00).
static int fat32_scan_dirsec(const char *name83,
                              unsigned int *clus_out, unsigned int *size_out) {
    for (unsigned int e = 0u; e < 16u; e++) {
        unsigned int base = e * 32u;
        unsigned int b0   = fat32_byte(sdhci_fat32_buf, base);
        if (b0 == 0x00u) return -1;   // end-of-dir sentinel
        if (b0 == 0xE5u) continue;    // deleted entry
        unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
        if (attr == 0x0Fu) continue;  // LFN entry
        if (attr & 0x08u) continue;   // volume label
        int match = 1;
        for (unsigned int j = 0u; j < 11u; j++) {
            unsigned int nc = fat32_byte(sdhci_fat32_buf, base + j);
            if (nc >= 'a' && nc <= 'z') nc -= 32u; // uppercase for case-insensitive compare
            if (nc != (unsigned int)(unsigned char)name83[j]) { match = 0; break; }
        }
        if (match) {
            unsigned int hi = fat32_u16(sdhci_fat32_buf, base + 20u);
            unsigned int lo = fat32_u16(sdhci_fat32_buf, base + 26u);
            *clus_out = (hi << 16) | lo;
            *size_out = fat32_u32(sdhci_fat32_buf, base + 28u);
            return 1;
        }
    }
    return 0;
}

int kernel_sdhci_fat32_read(void) {
    fat32_last_step = 0;
    if (!sdhci_card_init_ok) return 0;
    fat32_last_step = 1;
    // Select card if not already in Transfer State.
    if (!sdhci_card_selected && !sdhci_card_select()) return 0;
    fat32_last_step = 2;

    // ---- 1. MBR: find FAT32 partition LBA ----
    if (!sdhci_read_block_pio(0, sdhci_fat32_buf)) return 0;
    fat32_last_step = 3;
    // Verify boot signature.
    if (((sdhci_fat32_buf[127] >> 16) & 0xFFFFu) != 0xAA55u) return 0;
    fat32_last_step = 4;
    // Scan 4 MBR partition entries (table at offset 446, 16 bytes each).
    unsigned int part_lba = 0;
    int found = 0;
    for (unsigned int i = 0u; i < 4u && !found; i++) {
        unsigned int base  = 446u + i * 16u;
        unsigned int ptype = fat32_byte(sdhci_fat32_buf, base + 4u);
        if (ptype == 0x0Bu || ptype == 0x0Cu) {
            part_lba = fat32_u32(sdhci_fat32_buf, base + 8u);
            found = 1;
        }
    }
    if (!found) return 0;
    fat32_last_step = 5;

    // ---- 2. VBR: parse FAT32 BPB ----
    if (!sdhci_read_block_pio(part_lba, sdhci_fat32_buf)) return 0;
    fat32_last_step = 6;
    if (fat32_u16(sdhci_fat32_buf, 510u) != 0xAA55u) return 0;
    fat32_last_step = 7;
    if (fat32_u16(sdhci_fat32_buf, 11u)  != 512u)    return 0; // require 512 B/sec
    fat32_last_step = 8;
    fat32_sec_per_clus       = fat32_byte(sdhci_fat32_buf, 13u);
    unsigned int rsvd        = fat32_u16(sdhci_fat32_buf, 14u);
    unsigned int num_fats    = fat32_byte(sdhci_fat32_buf, 16u);
    unsigned int fat_sz32    = fat32_u32(sdhci_fat32_buf, 36u);
    fat32_root_clus          = fat32_u32(sdhci_fat32_buf, 44u);
    fat32_fat_lba            = part_lba + rsvd;
    fat32_data_lba           = fat32_fat_lba + num_fats * fat_sz32;
    if (fat32_sec_per_clus == 0u) return 0;
    fat32_last_step = 9;

    // ---- 3. Root dir: find "CONFIG  TXT" — follow FAT cluster chain ----
    unsigned int file_clus = 0u, file_size = 0u;
    int file_found = 0;
    unsigned int dir_clus = fat32_root_clus;
    unsigned int dir_done = 0u;
    for (unsigned int max_clus = 256u; !file_found && !dir_done && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            fat32_last_step = 10;
            int r = fat32_scan_dirsec("CONFIG  TXT", &file_clus, &file_size);
            if (r == 1)  { file_found = 1; break; }
            if (r == -1) { dir_done = 1u; break; } // end-of-dir sentinel
        }
        if (!file_found && !dir_done) {
            // Read FAT entry to follow cluster chain.
            unsigned int fat_off = dir_clus * 4u;
            if (!sdhci_read_block_pio(fat32_fat_lba + fat_off / 512u, sdhci_fat32_buf))
                break;
            unsigned int next = fat32_u32(sdhci_fat32_buf, fat_off % 512u) & 0x0FFFFFFFu;
            if (next >= 0x0FFFFFF8u) break; // end of chain
            dir_clus = next;
        }
    }
    if (!file_found || file_size == 0u || file_size > 65536u) return 0;
    fat32_last_step = 11;

    // ---- 4. Read file data and compute 32-bit byte-sum checksum ----
    unsigned int file_lba  = fat32_clus_lba(file_clus);
    unsigned int nsectors  = (file_size + 511u) / 512u;
    unsigned int checksum  = 0u;
    unsigned int remaining = file_size;
    for (unsigned int s = 0u; s < nsectors; s++) {
        if (!sdhci_read_block_pio(file_lba + s, sdhci_fat32_buf)) return 0;
        fat32_last_step = 12 + s;
        unsigned int nbytes = remaining < 512u ? remaining : 512u;
        for (unsigned int b = 0u; b < nbytes; b++)
            checksum += fat32_byte(sdhci_fat32_buf, b);
        remaining -= nbytes;
    }

    fat32_file_bytes = file_size;
    fat32_checksum   = checksum;
    fat32_ok         = 1;
    return 1;
}

int kernel_sdhci_fat32_selftest(void)           { return fat32_ok; }
unsigned long kernel_sdhci_fat32_bytes(void)    { return (unsigned long)fat32_file_bytes; }
unsigned long kernel_sdhci_fat32_checksum(void) { return (unsigned long)fat32_checksum; }
unsigned long kernel_sdhci_fat32_step(void)     { return (unsigned long)fat32_last_step; }

// V86: count root 8.3 entries. Does not write fat32_ok / fat32_file_bytes.
static unsigned int fat32_list_other;

int kernel_sdhci_fat32_listdir(unsigned int *files_out, unsigned int *config_out) {
    unsigned int files = 0u;
    unsigned int config = 0u;
    fat32_list_other = 0u;
    if (files_out) *files_out = 0u;
    if (config_out) *config_out = 0u;
    if (!sdhci_card_init_ok) return 0;
    if (fat32_sec_per_clus == 0u) {
        if (!kernel_sdhci_fat32_read()) return 0;
    }
    if (!sdhci_card_selected && !sdhci_card_select()) return 0;

    unsigned int dir_clus = fat32_root_clus;
    unsigned int dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                files++;
                int match = 1;
                for (unsigned int j = 0u; j < 11u; j++) {
                    unsigned int nc = fat32_byte(sdhci_fat32_buf, base + j);
                    if (nc >= 'a' && nc <= 'z') nc -= 32u;
                    if (nc != (unsigned int)(unsigned char)("CONFIG  TXT"[j])) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    config = 1u;
                } else if (fat32_list_other == 0u) {
                    fat32_list_other =
                        (fat32_byte(sdhci_fat32_buf, base) << 24) |
                        (fat32_byte(sdhci_fat32_buf, base + 1u) << 16) |
                        (fat32_byte(sdhci_fat32_buf, base + 2u) << 8) |
                        fat32_byte(sdhci_fat32_buf, base + 3u);
                }
            }
            if (dir_done) break;
        }
        if (!dir_done) {
            unsigned int fat_off = dir_clus * 4u;
            if (!sdhci_read_block_pio(fat32_fat_lba + fat_off / 512u, sdhci_fat32_buf))
                break;
            unsigned int next = fat32_u32(sdhci_fat32_buf, fat_off % 512u) & 0x0FFFFFFFu;
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (files_out) *files_out = files;
    if (config_out) *config_out = config;
    return (files >= 2u && config == 1u && fat32_list_other != 0u) ? 1 : 0;
}

unsigned int kernel_sdhci_fat32_list_other(void) { return fat32_list_other; }

// V87: read one regular root file that is not CONFIG.TXT. Skip dirs (0x10).
// Size cap 65536. Follows the FAT chain. Own out-params only.
static int fat32_name_eq(unsigned int base, const char *name83) {
    for (unsigned int j = 0u; j < 11u; j++) {
        unsigned int nc = fat32_byte(sdhci_fat32_buf, base + j);
        if (nc >= 'a' && nc <= 'z') nc -= 32u;
        if (nc != (unsigned int)(unsigned char)name83[j]) return 0;
    }
    return 1;
}

static int fat32_sum_chain(unsigned int clus, unsigned int size, unsigned int *sum_out) {
    unsigned int checksum = 0u;
    unsigned int remaining = size;
    for (unsigned int max_clus = 256u; remaining > 0u && max_clus > 0u; max_clus--) {
        unsigned int lba = fat32_clus_lba(clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus && remaining > 0u; s++) {
            if (!sdhci_read_block_pio(lba + s, sdhci_fat32_buf)) return 0;
            unsigned int nbytes = remaining < 512u ? remaining : 512u;
            for (unsigned int b = 0u; b < nbytes; b++)
                checksum += fat32_byte(sdhci_fat32_buf, b);
            remaining -= nbytes;
        }
        if (remaining == 0u) break;
        unsigned int fat_off = clus * 4u;
        if (!sdhci_read_block_pio(fat32_fat_lba + fat_off / 512u, sdhci_fat32_buf))
            return 0;
        unsigned int next = fat32_u32(sdhci_fat32_buf, fat_off % 512u) & 0x0FFFFFFFu;
        if (next >= 0x0FFFFFF8u) return 0;
        clus = next;
    }
    if (remaining != 0u) return 0;
    if (sum_out) *sum_out = checksum;
    return 1;
}

int kernel_sdhci_fat32_read_second(unsigned int *name_out,
                                   unsigned int *bytes_out,
                                   unsigned int *sum_out) {
    unsigned int pick_name = 0u;
    unsigned int pick_clus = 0u;
    unsigned int pick_size = 0u;
    unsigned int pick_rank = 0u;
    if (name_out) *name_out = 0u;
    if (bytes_out) *bytes_out = 0u;
    if (sum_out) *sum_out = 0u;
    if (!sdhci_card_init_ok) return 0;
    if (fat32_sec_per_clus == 0u) {
        if (!kernel_sdhci_fat32_read()) return 0;
    }
    if (!sdhci_card_selected && !sdhci_card_select()) return 0;

    unsigned int dir_clus = fat32_root_clus;
    unsigned int dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                if (attr & 0x10u) continue;
                unsigned int size = fat32_u32(sdhci_fat32_buf, base + 28u);
                if (size == 0u || size > 65536u) continue;
                if (fat32_name_eq(base, "CONFIG  TXT")) continue;
                unsigned int rank = 1u;
                if (fat32_name_eq(base, "CMDLINE TXT")) rank = 3u;
                else if (fat32_name_eq(base, "ISSUE   TXT")) rank = 2u;
                if (rank > pick_rank) {
                    unsigned int hi = fat32_u16(sdhci_fat32_buf, base + 20u);
                    unsigned int lo = fat32_u16(sdhci_fat32_buf, base + 26u);
                    pick_clus = (hi << 16) | lo;
                    pick_size = size;
                    pick_name =
                        (fat32_byte(sdhci_fat32_buf, base) << 24) |
                        (fat32_byte(sdhci_fat32_buf, base + 1u) << 16) |
                        (fat32_byte(sdhci_fat32_buf, base + 2u) << 8) |
                        fat32_byte(sdhci_fat32_buf, base + 3u);
                    pick_rank = rank;
                }
            }
            if (dir_done) break;
        }
        if (!dir_done) {
            unsigned int fat_off = dir_clus * 4u;
            if (!sdhci_read_block_pio(fat32_fat_lba + fat_off / 512u, sdhci_fat32_buf))
                break;
            unsigned int next = fat32_u32(sdhci_fat32_buf, fat_off % 512u) & 0x0FFFFFFFu;
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (pick_rank == 0u || pick_size == 0u || pick_name == 0u) return 0;
    unsigned int sum = 0u;
    if (!fat32_sum_chain(pick_clus, pick_size, &sum)) return 0;
    if (name_out) *name_out = pick_name;
    if (bytes_out) *bytes_out = pick_size;
    if (sum_out) *sum_out = sum;
    return 1;
}

// V88: walk OVERLAYS   (directory, attr 0x10). Count short names, skip '.' / '..'.
static unsigned int fat32_next_clus(unsigned int clus) {
    unsigned int fat_off = clus * 4u;
    if (!sdhci_read_block_pio(fat32_fat_lba + fat_off / 512u, sdhci_fat32_buf))
        return 0x0FFFFFFFu;
    return fat32_u32(sdhci_fat32_buf, fat_off % 512u) & 0x0FFFFFFFu;
}

int kernel_sdhci_fat32_list_overlays(unsigned int *files_out, unsigned int *name_out) {
    unsigned int ovl_clus = 0u;
    unsigned int files = 0u;
    unsigned int first_name = 0u;
    if (files_out) *files_out = 0u;
    if (name_out) *name_out = 0u;
    if (!sdhci_card_init_ok) return 0;
    if (fat32_sec_per_clus == 0u) {
        if (!kernel_sdhci_fat32_read()) return 0;
    }
    if (!sdhci_card_selected && !sdhci_card_select()) return 0;

    unsigned int dir_clus = fat32_root_clus;
    unsigned int dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && ovl_clus == 0u && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                if ((attr & 0x10u) == 0u) continue;
                if (!fat32_name_eq(base, "OVERLAYS   ")) continue;
                unsigned int hi = fat32_u16(sdhci_fat32_buf, base + 20u);
                unsigned int lo = fat32_u16(sdhci_fat32_buf, base + 26u);
                ovl_clus = (hi << 16) | lo;
                break;
            }
            if (dir_done || ovl_clus != 0u) break;
        }
        if (!dir_done && ovl_clus == 0u) {
            unsigned int next = fat32_next_clus(dir_clus);
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (ovl_clus < 2u) return 0;

    dir_clus = ovl_clus;
    dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                if (b0 == 0x2Eu) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                files++;
                if (first_name == 0u) {
                    first_name =
                        (fat32_byte(sdhci_fat32_buf, base) << 24) |
                        (fat32_byte(sdhci_fat32_buf, base + 1u) << 16) |
                        (fat32_byte(sdhci_fat32_buf, base + 2u) << 8) |
                        fat32_byte(sdhci_fat32_buf, base + 3u);
                }
            }
            if (dir_done) break;
        }
        if (!dir_done) {
            unsigned int next = fat32_next_clus(dir_clus);
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (files_out) *files_out = files;
    if (name_out) *name_out = first_name;
    return (files >= 1u && first_name != 0u) ? 1 : 0;
}

// V89: load one regular file from OVERLAYS   . Size cap 65536. FAT chain.
int kernel_sdhci_fat32_read_overlay(unsigned int *name_out,
                                    unsigned int *bytes_out,
                                    unsigned int *sum_out) {
    unsigned int ovl_clus = 0u;
    unsigned int pick_name = 0u;
    unsigned int pick_clus = 0u;
    unsigned int pick_size = 0u;
    if (name_out) *name_out = 0u;
    if (bytes_out) *bytes_out = 0u;
    if (sum_out) *sum_out = 0u;
    if (!sdhci_card_init_ok) return 0;
    if (fat32_sec_per_clus == 0u) {
        if (!kernel_sdhci_fat32_read()) return 0;
    }
    if (!sdhci_card_selected && !sdhci_card_select()) return 0;

    unsigned int dir_clus = fat32_root_clus;
    unsigned int dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && ovl_clus == 0u && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                if ((attr & 0x10u) == 0u) continue;
                if (!fat32_name_eq(base, "OVERLAYS   ")) continue;
                unsigned int hi = fat32_u16(sdhci_fat32_buf, base + 20u);
                unsigned int lo = fat32_u16(sdhci_fat32_buf, base + 26u);
                ovl_clus = (hi << 16) | lo;
                break;
            }
            if (dir_done || ovl_clus != 0u) break;
        }
        if (!dir_done && ovl_clus == 0u) {
            unsigned int next = fat32_next_clus(dir_clus);
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (ovl_clus < 2u) return 0;

    dir_clus = ovl_clus;
    dir_done = 0u;
    for (unsigned int max_clus = 256u; !dir_done && pick_size == 0u && max_clus > 0u; max_clus--) {
        unsigned int dir_lba = fat32_clus_lba(dir_clus);
        for (unsigned int s = 0u; s < fat32_sec_per_clus; s++) {
            if (!sdhci_read_block_pio(dir_lba + s, sdhci_fat32_buf)) return 0;
            for (unsigned int e = 0u; e < 16u; e++) {
                unsigned int base = e * 32u;
                unsigned int b0 = fat32_byte(sdhci_fat32_buf, base);
                if (b0 == 0x00u) { dir_done = 1u; break; }
                if (b0 == 0xE5u) continue;
                if (b0 == 0x2Eu) continue;
                unsigned int attr = fat32_byte(sdhci_fat32_buf, base + 11u);
                if (attr == 0x0Fu) continue;
                if (attr & 0x08u) continue;
                if (attr & 0x10u) continue;
                unsigned int size = fat32_u32(sdhci_fat32_buf, base + 28u);
                if (size == 0u || size > 65536u) continue;
                unsigned int hi = fat32_u16(sdhci_fat32_buf, base + 20u);
                unsigned int lo = fat32_u16(sdhci_fat32_buf, base + 26u);
                pick_clus = (hi << 16) | lo;
                pick_size = size;
                pick_name =
                    (fat32_byte(sdhci_fat32_buf, base) << 24) |
                    (fat32_byte(sdhci_fat32_buf, base + 1u) << 16) |
                    (fat32_byte(sdhci_fat32_buf, base + 2u) << 8) |
                    fat32_byte(sdhci_fat32_buf, base + 3u);
                break;
            }
            if (dir_done || pick_size != 0u) break;
        }
        if (!dir_done && pick_size == 0u) {
            unsigned int next = fat32_next_clus(dir_clus);
            if (next >= 0x0FFFFFF8u) break;
            dir_clus = next;
        }
    }
    if (pick_size == 0u || pick_name == 0u || pick_clus < 2u) return 0;
    unsigned int sum = 0u;
    if (!fat32_sum_chain(pick_clus, pick_size, &sum)) return 0;
    if (name_out) *name_out = pick_name;
    if (bytes_out) *bytes_out = pick_size;
    if (sum_out) *sum_out = sum;
    return 1;
}
