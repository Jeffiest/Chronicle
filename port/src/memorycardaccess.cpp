#include "memorycardaccess.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "dataread_port.hpp"
#include "menu_draw.hpp"
#include "savedata.hpp"

// The bodies are retail's, which pass string literals as char *.
#pragma clang diagnostic ignored "-Wwritable-strings"

namespace {

// Retail's ((int)p >> 6) + 1) << 6 on the whole pointer: the next 64-byte boundary strictly after p.
char *PastNext64(char *pointer) {
    return pointer + (64 - reinterpret_cast<std::uintptr_t>(pointer) % 64);
}

} // namespace

// Saves go to the save directory of the release whose disc the data came from.
PC_OVERRIDE void CMemoryCardAccess::Initialize() {
    if (PortNtscData()) {
        switch (GetMenuLangFlag()) {
            case LANG_JAPANESE:
                strcpy(this->dir_name, "BISCPS-15004dkcloud");
                break;
            case LANG_ENGLISH_US:
            default:
                strcpy(this->dir_name, "BASCUS-97111dkcloud");
                break;
        }
    } else {
        strcpy(this->dir_name, "BESCES-50295dkcloud");
    }

    strcpy(this->file_name, "darkcloud");

    for (int i = 0; i < 0x40; i++) {
        this->current_dir[i] = 0;
    }

    this->port = 0;
    this->file_no = 0;
    this->fd = -1;
    memset(&this->error, 0, sizeof(this->error));
    this->SetVersion("darkcloudVer1.9");
    this->func_no = MC_OPERATION_IDLE;
    this->idle_code = 0x3D;
    this->step = 0;
    this->save_buffer = NULL;
    this->load_buffer = NULL;
    this->read_buffer = NULL;
    this->dir_table = SaveFileInfo;
    this->transferred = 0;
    this->transfer_size = 0;
    memset(this->card, 0, sizeof(this->card));
    memset(this->file_info, 0, sizeof(this->file_info));
    memset(&this->icon, 0, sizeof(this->icon));
    this->card[0].present = 1;
    this->card[1].present = 1;
    printf("SaveData size = %zu\n", sizeof(CSaveData));
}

PC_OVERRIDE void CMemoryCardAccess::SetBuff(char *buffer) {
    char *data;
    char *sum;
    u32   i;
    char  total;

    buffer = PastNext64(buffer);
    this->save_buffer = (CSaveData *) buffer;
    memcpy(this->save_buffer, SaveData, 0x131C0);
    this->save_buffer->ConvertConfig(&sys_config);
    char *version = (char *) this->save_buffer + 0x131C0;
    strcpy(version, this->version);
    this->check_sum = version + 0x20;
    data = (char *) this->save_buffer;
    sum = this->check_sum;
    memset(sum, 0, 0x4C7);
    total = 0;

    for (i = 0; i < 0x131C0; i++) {
        total += *data++;

        if ((int) i % 64 == 63) {
            *sum++ = total;
            total = 0;
        }
    }

    this->read_buffer = PastNext64(sum);
    this->load_buffer = this->read_buffer;
}
