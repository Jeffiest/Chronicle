#include "common.h"

#include "itemdata.hpp"

extern COM_ITEM_INFO ComItemInfo[296];

/* ComItemInfo holds items 81 to 376. Retail indexes it with no check, and PresetSmallItemNo_Get
   asks about weapons up to 379, which reads the words after the table; the port answers nothing
   for an item the table does not hold. */
PC_OVERRIDE COM_ITEM_INFO *GetCommonItemInfo(int item_no) {
    if (item_no <= 0) {
        return 0;
    }

    // Items before slot 81 function as an alias for weapons in the info table.
    if (0 < item_no && item_no < ITEM_ATTACH_START) {
        item_no += ITEM_WEAPON_START - 1 - ITEM_ATTACH_START;
    } else {
        item_no -= ITEM_ATTACH_START;
    }

    if (item_no >= 296) {
        return 0;
    }

    return &ComItemInfo[item_no];
}
