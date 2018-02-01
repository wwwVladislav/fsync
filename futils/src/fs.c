#include "fs.h"
#include "log.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

struct fsdir
{
    DIR *pdir;
    char name[FMAX_FILENAME];
};

struct fsfile { int test; };

struct fsiterator
{
    char           path[FMAX_PATH];
    unsigned short depth;
    fsdir_t        dirs[FMAX_DIR_DEPTH];
};

static bool fsopendir(fsdir_t *pdir, char const *path, char const *name)
{
    if (!pdir || !path) return false;
    pdir->pdir = opendir(path);
    if (!pdir->pdir)
    {
        FS_ERR("Unable to open the directory \'%s\'. Error: %d", path, errno);
        return false;
    }
    if (name)
        strncpy(pdir->name, name, sizeof pdir->name);
    else
        pdir->name[0] = 0;
    return true;
}

static void fsclosedir(fsdir_t *pdir)
{
    if (pdir)
    {
        closedir(pdir->pdir);
        pdir->pdir = 0;
    }
}

static bool fsseekdir(fsdir_t *pdir, char const *dir_name)
{
    if (!pdir || !dir_name)
        return false;

    struct dirent *ent;
    size_t const dir_name_len = strlen(dir_name);

    rewinddir(pdir->pdir);

    if (!*dir_name)
        return false;

    while((ent = readdir(pdir->pdir)))
    {
        if (dir_name_len == strlen(ent->d_name)
            && strncmp(ent->d_name, dir_name, dir_name_len) == 0)
            return true;
    }

    return false;
}

static fsdir_entry_t fsdir_entry_type(unsigned type)
{
    switch(type)
    {
        case DT_BLK:    return FS_BLK;
        case DT_CHR:    return FS_CHR;
        case DT_DIR:    return FS_DIR;
        case DT_FIFO:   return FS_FIFO;
        case DT_LNK:    return FS_LNK;
        case DT_REG:    return FS_REG;
        case DT_SOCK:   return FS_SOCK;
    }
    return FS_UNKNOWN;
}

fsdir_t *fsdir_open(char const *path)
{
    if (!path) return 0;

    fsdir_t *dir = malloc(sizeof(fsdir_t));

    if (!dir)
    {
        FS_ERR("Unable to allocate memory for directory handle");
        return 0;
    }

    if (!fsopendir(dir, path, 0))
    {
        free(dir);
        return 0;
    }

    return dir;
}

void fsdir_close(fsdir_t *pdir)
{
    if (pdir)
    {
        fsclosedir(pdir);
        free(pdir);
    }
}

bool fsdir_read(fsdir_t *pdir, dirent_t *pentry)
{
    if (!pdir || !pentry) return false;
    struct dirent *ent;

    while((ent = readdir(pdir->pdir)))
    {
        size_t const dlen = strlen(ent->d_name);

        if (ent->d_type == DT_DIR)
        {
            if ((dlen == 1 && !strncmp(ent->d_name, ".", 1))
                || (dlen == 2 && !strncmp(ent->d_name, "..", 2)))
                continue;
        }
        pentry->type = fsdir_entry_type(ent->d_type);
        if (dlen < sizeof pentry->name)
        {
            pentry->namlen = dlen;
            pentry->name[pentry->namlen] = 0;
        }
        else
            pentry->namlen = sizeof pentry->name;
        memcpy(pentry->name, ent->d_name, pentry->namlen);
        return true;
    }

    return false;
}

fsiterator_t *fsdir_iterator(char const *path)
{
    if (!path) return 0;

    size_t len = strlen(path);
    if (len > FMAX_PATH)
    {
        FS_ERR("Path is too long");
        return 0;
    }

    fsiterator_t *pfsiterator = malloc(sizeof(fsiterator_t));
    if (!pfsiterator)
    {
        FS_ERR("Unable to allocate memory for directory files iterator");
        return 0;
    }

    if (!fsopendir(pfsiterator->dirs, path, 0))
    {
        fsdir_iterator_free(pfsiterator);
        pfsiterator = 0;
    }

    strncpy(pfsiterator->path, path, sizeof pfsiterator->path);
    pfsiterator->depth = 1;

    return pfsiterator;
}

void fsdir_iterator_free(fsiterator_t *piterator)
{
    if (piterator)
    {
        for(short i = 0; i < piterator->depth; ++i)
            fsclosedir(piterator->dirs + i);
        free(piterator);
    }
}

static size_t fsget_directory_by_iterator(fsiterator_t *piterator, char *subdir, char *path, size_t size, bool full_path)
{
    if (!piterator || !path) return 0;

    size_t wsize = 0;
    char delimiter = '/';

    for(char *ch = piterator->path;
        *ch && wsize < sizeof piterator->path;
        ++ch)
    {
        if (full_path)
        {
            if (wsize < size)
                path[wsize] = *ch;
            ++wsize;
        }
        if (*ch == '\\')
            delimiter = '\\';
    }

    for (unsigned short i = 1; i < piterator->depth; ++i)
    {
        fsdir_t *pdir = &piterator->dirs[i];

        if (full_path || i != 0)
        {
            if (pdir->name[0]
                && wsize
                && wsize < size
                && path[wsize - 1] != '/'
                && path[wsize - 1] != '\\')
                path[wsize++] = delimiter;
        }

        for(char *ch = pdir->name;
            *ch && (ch - pdir->name) < sizeof pdir->name;
            ++ch)
        {
            if (wsize < size)
                path[wsize++] = *ch;
        }
    }

    if (subdir && subdir[0])
    {
        if (wsize && wsize < size)
            path[wsize++] = delimiter;

        for(char *ch = subdir; *ch; ++ch)
        {
            if (wsize < size)
                path[wsize++] = *ch;
        }
    }

    if (wsize < size)
        path[wsize] = 0;
    return wsize;
}

bool fsdir_iterator_next(fsiterator_t *piterator, dirent_t *pentry)
{
    if (!piterator || !pentry) return false;
    if (!piterator->depth) return false;

    while(piterator->depth)
    {
        while (fsdir_read(&piterator->dirs[piterator->depth - 1], pentry))
        {
            switch(pentry->type)
            {
                case FS_REG:
                    return true;

                case FS_DIR:
                {
                    if (piterator->depth >= FMAX_DIR_DEPTH)
                    {
                        FS_ERR("The directory \'%s\' is skipped due the restriction for maximum directory depth", pentry->name);
                        continue;
                    }

                    char path[FMAX_PATH];
                    size_t len = fsget_directory_by_iterator(piterator, pentry->name, path, sizeof path, true);
                    if (len >= sizeof path)
                    {
                        FS_ERR("The directory \'%s\' is skipped due the restriction for maximum path length", pentry->name);
                        continue;
                    }

                    if (fsopendir(&piterator->dirs[piterator->depth], path, pentry->name))
                    {
                        piterator->depth++;
                        return true;
                    }
                }
                default:
                    // Unsupported type
                    break;
            }
        }

        fsclosedir(&piterator->dirs[piterator->depth - 1]);
        piterator->depth--;
    }

    return false;
}

bool fsdir_iterator_seek(fsiterator_t *piterator, char const *path)
{
    if (!piterator || !path) return false;
    if (!piterator->depth) return false;

    unsigned short n = 1;

    for (char const *ch = path, *p = ch; ; ++ch)
    {
        if (*ch != '/' && *ch != '\\' && *ch)
            continue;

        char dir_name[FMAX_PATH];
        size_t dir_name_len = ch - p;
        if (dir_name_len >= sizeof dir_name)
        {
            FS_ERR("Directory name is too long");
            return false;
        }
        strncpy(dir_name, p, dir_name_len);
        dir_name[dir_name_len] = 0;

        if (n < piterator->depth)
        {
            if (dir_name_len && strncmp(dir_name, piterator->dirs[n].name, dir_name_len) == 0)
            {
                p = ch + 1;
                ++n;
                if (!*ch) break;
                continue;
            }
            else
            {
                for (unsigned short i = n; i < piterator->depth; ++i)
                    fsclosedir(&piterator->dirs[i]);
                piterator->depth = n;
            }
        }

        char path[FMAX_PATH];
        size_t len = fsget_directory_by_iterator(piterator, dir_name, path, sizeof path, true);
        if (len >= sizeof path)
        {
            FS_ERR("Directory name is too long");
            return false;
        }

        if (fsseekdir(&piterator->dirs[piterator->depth - 1], dir_name)
            && fsopendir(&piterator->dirs[piterator->depth], path, dir_name))
            piterator->depth++;

        if (!*ch) break;

        p = ch + 1;
        ++n;
    }

    return true;
}

size_t fsdir_iterator_directory(fsiterator_t *piterator, char *path, size_t size)
{
    return fsget_directory_by_iterator(piterator, 0, path, size, false);
}

size_t fsdir_iterator_path(fsiterator_t *piterator, dirent_t *pentry, char *path, size_t size)
{
    return fsget_directory_by_iterator(piterator, pentry->name, path, size, false);
}

size_t fsdir_iterator_full_path(fsiterator_t *piterator, dirent_t *pentry, char *path, size_t size)
{
    return fsget_directory_by_iterator(piterator, pentry->name, path, size, true);
}

bool fsdir_is_exist(char const *path)
{
    if (!path)
        return false;
    struct stat st;
    if (stat(path, &st) == -1)
        return false;
    return (st.st_mode & S_IFDIR) != 0;
}

char fspath_delimiter(char const *path)
{
    char delimiter = '/';

    if (path)
    {
        for(char const *ch = path; *ch; ++ch)
        {
            if (*ch == '\\')
                delimiter = '\\';
            break;
        }
    }

    return delimiter;
}

bool fsfile_md5sum(char const *path, fmd5_t *sum)
{
    if (!path || !sum) return false;

    fmd5_context_t md5_ctx;
    fmd5_init(&md5_ctx);

    int fd = open(path, O_RDONLY);
    if (fd != -1)
    {
        uint8_t buffer[10 * 1024];
        ssize_t size;
        while((size = read(fd, buffer, sizeof buffer)) > 0)
            fmd5_update(&md5_ctx, buffer, size);
        close(fd);
        fmd5_final(&md5_ctx, sum);
        return true;
    }
    else
        FS_ERR("Unable to open the file: \'%s\'", path);
    return false;
}

bool fsfile_size(char const *path, uint64_t *size)
{
    if (!path || !size)
        return false;
    struct stat st;
    if (stat(path, &st) == -1)
        return false;
    *size = st.st_size;
    return true;
}
