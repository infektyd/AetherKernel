// Runtime V88: walk FAT32 overlays/ after GENET.
// V86 saw OVERLAYS on this bootfs. Count short names; skip '.' / '..'.
// ok=1 only if files >= 1u and one 8.3 name was seen. No EL0 execute
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Uses the shared 512 B FAT scratch, not core0 stack.

#include "Support.h"

static int sdovl_probed;
static int sdovl_ok_val;
static unsigned int sdovl_files_val;
static unsigned int sdovl_name_val;

int kernel_sdovl_selftest(void) {
    if (sdovl_probed) return sdovl_ok_val;
    sdovl_probed = 1;
    sdovl_ok_val = 0;
    sdovl_files_val = 0;
    sdovl_name_val = 0;

    unsigned int files = 0u;
    unsigned int name = 0u;
    (void)kernel_sdhci_fat32_list_overlays(&files, &name);
    sdovl_files_val = files;
    sdovl_name_val = name;
    if (files >= 1u && name != 0u) {
        sdovl_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_sdovl_ok(void)    { return sdovl_ok_val; }
unsigned int kernel_sdovl_files(void) { return sdovl_files_val; }
unsigned int kernel_sdovl_name(void)  { return sdovl_name_val; }
