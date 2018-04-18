#include <stdio.h>
#include <memory.h>

#define MTK_CHIP_ID 0x80000008

void dump_dev_id(void)
{
    printf("Device ID code: 0x%04x\n", readw(MTK_CHIP_ID));
}
