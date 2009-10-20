
#include <dlfcn.h>
#include <stdio.h>

#include "bluray.h"
#include "util/macro.h"
#include "util/logging.h"


BLURAY *bd_open(const char* device_path, const char* keyfile_path)
{
    BLURAY *bd = NULL;

    printf("A\n");
    if (device_path) {
        strncpy(bd->device_path, device_path, 100);

        bd = malloc(sizeof(BLURAY));

        bd->aacs = NULL;
        bd->h_libaacs = NULL;
        bd->fp = NULL;

        if (keyfile_path) {
            if ((bd->h_libaacs = dlopen("libaacs.so", RTLD_LAZY))) {
                DEBUG(DBG_BLURAY, "Downloaded libaacs (0x%08x)\n", bd->h_libaacs);

                typedef AACS_KEYS* (*fptr)();

                fptr fptr_s = dlsym(bd->h_libaacs, "aacs_open");
                bd->aacs = fptr_s(device_path, keyfile_path);
            } else {
                DEBUG(DBG_BLURAY, "libaacs not present\n");
            }
        } else {
            DEBUG(DBG_BLURAY, "No keyfile provided. You will not be able to make use of crypto functionality (0x%08x)\n", bd);
        }

        DEBUG(DBG_BLURAY, "BLURAY initialized! (0x%08x)\n", bd);
    } else {
        DEBUG(DBG_BLURAY, "No device path provided!\n");
    }

    return bd;
}

void bd_close(BLURAY *bd)
{
    if (bd->h_libaacs) {
        typedef void* (*fptr)();

        fptr fptr_s = dlsym(bd->h_libaacs, "aacs_close");
        fptr_s(bd->aacs);
    }

    dlclose(bd->h_libaacs);

    if (bd->fp) {
        file_close(bd->fp);
    }

    DEBUG(DBG_BLURAY, "BLURAY destroyed! (0x%08x)\n", bd);

    X_FREE(bd);
}

off_t bd_seek(BLURAY *bd, uint64_t pos)
{
    if (pos < bd->s_size) {
        bd->s_pos = pos - (pos % 6144);

        file_seek(bd->fp, bd->s_pos, SEEK_SET);

        DEBUG(DBG_BLURAY, "Seek to %ld (0x%08x)\n", bd->s_pos, bd);
    }

    return bd->s_pos;
}

int bd_read(BLURAY *bd, unsigned char *buf, int len)
{
    if (bd->fp) {
        DEBUG(DBG_BLURAY, "Reading unit [%d bytes] at %ld... (0x%08x)\n", len, bd->s_pos, bd);

        if (len + bd->s_pos < bd->s_size) {
            int read_len;

            if ((read_len = file_read(bd->fp, buf, len))) {
                if (bd->h_libaacs) {
                    if ((bd->libaacs_decrypt_unit = dlsym(bd->h_libaacs, "aacs_decrypt_unit"))) {
                        if (!bd->libaacs_decrypt_unit(bd->aacs, buf, len, bd->s_pos)) {
                            DEBUG(DBG_BLURAY, "Unable decrypt unit! (0x%08x)\n", bd);

                            return 0;
                        }
                    }
                }

                bd->s_pos += len;

                DEBUG(DBG_BLURAY, "%d bytes read OK! (0x%08x)\n", read_len, bd);

                return read_len;
            }
        }
    }

    DEBUG(DBG_BLURAY, "No valid title selected! (0x%08x)\n", bd->s_pos);

    return 0;
}

int bd_select_title(BLURAY *bd, uint64_t title)
{
    char f_name[100];

    memset(f_name, 0, sizeof(f_name));
    snprintf(f_name, 100, "%s/BDMV/STREAM/%05ld.m2ts", bd->device_path, title);

    bd->s_size = 0;
    bd->s_pos = 0;

    if ((bd->fp = file_open(f_name, "rb"))) {
        file_seek(bd->fp, 0, SEEK_END);
        if ((bd->s_size = file_tell(bd->fp))) {
            bd_seek(bd, 0);

            DEBUG(DBG_BLURAY, "Title %s selected! (0x%08x)\n", f_name, bd);

            return 1;
        }

        DEBUG(DBG_BLURAY, "Title %s empty! (0x%08x)\n", f_name, bd);
    }

    DEBUG(DBG_BLURAY, "Unable to select title %s! (0x%08x)\n", f_name, bd);

    return 0;
}
