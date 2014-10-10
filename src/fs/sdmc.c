/*
* Copyright (C) 2014 - plutoo
* Copyright (C) 2014 - ichfly
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "util.h"
#include "mem.h"
#include "handles.h"
#include "fs.h"


/* ____ File implementation ____ */

static u32 sdmcfile_Write(file_type* self, u32 ptr, u32 sz, u64 off, u32 flush_flags, u32* written_out)
{
    FILE* fd = self->type_specific.sysdata.fd;
    *written_out = 0;

    if (off >> 32) {
        ERROR("64-bit offset not supported.\n");
        return -1;
    }

    if (fseek(fd, off, SEEK_SET) == -1) {
        ERROR("fseek failed.\n");
        return -1;
    }

    u8* b = malloc(sz);
    if (b == NULL) {
        ERROR("Not enough mem.\n");
        return -1;
    }

    if (mem_Read(b, ptr, sz) != 0) {
        ERROR("mem_Write failed.\n");
        free(b);
        return -1;
    }

    u32 write = fwrite(b, 1, sz, fd);
    if (write == 0) {
        ERROR("fwrite failed\n");
        free(b);
        return -1;
    }

    *written_out = write;
    free(b);

    return 0; // Result
}

static u32 sdmcfile_Read(file_type* self, u32 ptr, u32 sz, u64 off, u32* read_out)
{
    FILE* fd = self->type_specific.sysdata.fd;
    *read_out = 0;

    if(off >> 32) {
        ERROR("64-bit offset not supported.\n");
        return -1;
    }

    if(fseek(fd, off, SEEK_SET) == -1) {
        ERROR("fseek failed.\n");
        return -1;
    }

    u8* b = malloc(sz);
    if(b == NULL) {
        ERROR("Not enough mem.\n");
        return -1;
    }

    u32 read = fread(b, 1, sz, fd);
    if(read == 0) {
        ERROR("fread failed\n");
        free(b);
        return -1;
    }

    if(mem_Write(b, ptr, read) != 0) {
        ERROR("mem_Write failed.\n");
        free(b);
        return -1;
    }

    *read_out = read;
    free(b);

    return 0; // Result
}

static u64 sdmcfile_GetSize(file_type* self)
{
    return self->type_specific.sysdata.sz;
}

static u32 sdmcfile_Close(file_type* self)
{
    // Close file and free yourself
    fclose(self->type_specific.sysdata.fd);
    free(self);

    return 0;
}



/* ____ FS implementation ____ */

static bool sdmc_FileExists(archive* self, file_path path)
{
    char p[256], tmp[256];
    struct stat st;

    // Generate path on host file system
    snprintf(p, 256, "sdmc/%s",
             fs_PathToString(path.type, path.ptr, path.size, tmp, 256));

    if(!fs_IsSafePath(p)) {
        ERROR("Got unsafe path.\n");
        return false;
    }

    return stat(p, &st) == 0;
}

static u32 sdmc_OpenFile(archive* self, file_path path, u32 flags, u32 attr)
{
    char p[256], tmp[256];

    // Generate path on host file system
    snprintf(p, 256, "sdmc/%s",
             fs_PathToString(path.type, path.ptr, path.size, tmp, 256));

    if(!fs_IsSafePath(p)) {
        ERROR("Got unsafe path.\n");
        return 0;
    }

    FILE* fd = fopen(p, "rb");
    if(fd == NULL) 
    {
        if (flags & OPEN_CREATE)
        {
            fd = fopen(p, "wb");
            if (fd == NULL)
            {
                ERROR("Failed to open/create sdmc, path=%s\n", p);
                return 0;
            }
        }
        else
        {
        ERROR("Failed to open sdmc, path=%s\n", p);
        return 0;
        }
    }
    fclose(fd);
    
    switch (flags& (OPEN_READ | OPEN_WRITE))
    {
    case 0:
        ERROR("Error open without write and read fallback to read only, path=%s\n", p);
        fd = fopen(p, "rb");
        break;
    case OPEN_READ:
        fd = fopen(p, "rb");
        break;
    case OPEN_WRITE:
        DEBUG("--todo-- write only, path=%s\n", p);
        fd = fopen(p, "r+b");
        break;
    case OPEN_WRITE | OPEN_READ:
        fd = fopen(p, "r+b");
        break;
    }

    fseek(fd, 0, SEEK_END);
    u32 sz;

    if((sz=ftell(fd)) == -1) {
        ERROR("ftell() failed.\n");
        fclose(fd);
        return 0;
    }

    // Create file object
    file_type* file = calloc(sizeof(file_type), 1);

    if(file == NULL) {
        ERROR("calloc() failed.\n");
        fclose(fd);
        return 0;
    }

    file->type_specific.sysdata.fd = fd;
    file->type_specific.sysdata.sz = (u64) sz;

    // Setup function pointers.
    file->fnWrite = &sdmcfile_Write;
    file->fnRead = &sdmcfile_Read;
    file->fnGetSize = &sdmcfile_GetSize;
    file->fnClose = &sdmcfile_Close;

    return handle_New(HANDLE_TYPE_FILE, (uintptr_t) file);
}

static void sdmc_Deinitialize(archive* self)
{
    // Free yourself
    free(self);
}
u32 fnReadDir(dir_type* self, u32 ptr, u32 entrycount, u32* read_out)
{
    int current = 0;
    while (current < entrycount) //the first entry is ignored
    {
        if (FindNextFileA(self->type_specific.hFind, self->type_specific.ffd) == 0) //no more files
        {
            break;
        }
        mem_Write(self->type_specific.ffd->cFileName, ptr, 0x20C);
        current++;
    }
    *read_out = current;
    return 0;
}
wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
    wchar_t* wString = malloc(sizeof(wchar_t)*4096);
    MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
    return wString;
}

static u32 fnOpenDir(archive* self, file_path path)
{
    // Create file object
    dir_type* dir = calloc(sizeof(file_type), 1);


    dir->type_specific.f_path = path;
    dir->type_specific.self = self;
    
    
    dir->type_specific.hFind = INVALID_HANDLE_VALUE;

    // Setup function pointers.
    dir->fnRead = fnReadDir;

    char p[256] ,tmp[256];
    snprintf(p, 256, "sdmc/%s/*",
        fs_PathToString(path.type, path.ptr, path.size, tmp, 256));

    dir->type_specific.ffd = (WIN32_FIND_DATA *)malloc(sizeof(WIN32_FIND_DATA)); //this is a workaround

    dir->type_specific.hFind = FindFirstFileA(p, dir->type_specific.ffd);

    if (dir->type_specific.hFind == INVALID_HANDLE_VALUE)
        return 0;


    return handle_New(HANDLE_TYPE_DIR, (uintptr_t)dir);
}
archive* sdmc_OpenArchive(file_path path)
{
    // SysData needs a binary path with an 8-byte id.
    if(path.type != PATH_EMPTY) {
        ERROR("Unknown sdmc path.\n");
        return NULL;
    }

    archive* arch = calloc(sizeof(archive), 1);

    if(arch == NULL) {
        ERROR("malloc failed.\n");
        return NULL;
    }

    // Setup function pointers
    arch->fnOpenDir = fnOpenDir;
    arch->fnFileExists = &sdmc_FileExists;
    arch->fnOpenFile = &sdmc_OpenFile;
    arch->fnDeinitialize = &sdmc_Deinitialize;

    u8 buf[8];

    if(mem_Read(buf, path.ptr, 8) != 0) {
        ERROR("Failed to read path.\n");
        free(arch);
        return NULL;
    }

    snprintf(arch->type_specific.sysdata.path,
             sizeof(arch->type_specific.sysdata.path),
             "");
    return arch;
}
