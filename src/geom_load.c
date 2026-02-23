#define _GNU_SOURCE
#include "geom_load.h"
#include "git_helpers.h"
#include "util.h"
#include <alea.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <unistd.h>
#endif

geom_format_t ag_detect_format(const char* path, const char* data, size_t len) {
    /* Check extension first */
    if (path) {
        if (ag_str_ends_with(path, ".xml"))
            return GEOM_FORMAT_OPENMC;
        if (ag_str_ends_with(path, ".inp") ||
            ag_str_ends_with(path, ".i") ||
            ag_str_ends_with(path, ".mcnp"))
            return GEOM_FORMAT_MCNP;
    }

    /* Check content */
    if (data && len > 5) {
        /* Skip BOM / whitespace */
        const char* p = data;
        while (p < data + len && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
        if (p + 5 <= data + len && strncmp(p, "<?xml", 5) == 0)
            return GEOM_FORMAT_OPENMC;
        if (p + 1 <= data + len && *p == '<')
            return GEOM_FORMAT_OPENMC;
    }

    /* Default to MCNP for unknown extensions */
    return GEOM_FORMAT_MCNP;
}

alea_system_t* ag_load_geometry_buffer(const char* data, size_t len,
                                       geom_format_t format) {
    if (format == GEOM_FORMAT_MCNP) {
        return alea_load_mcnp_string(data, len);
    }

    if (format == GEOM_FORMAT_OPENMC) {
        /* OpenMC parser needs a file; write to temp */
        char tmppath[4096];
#ifdef _WIN32
        const char* tmpdir = getenv("TEMP");
        if (!tmpdir) tmpdir = getenv("TMP");
        if (!tmpdir) tmpdir = ".";
        snprintf(tmppath, sizeof(tmppath), "%s\\aleagit_%d.xml",
                 tmpdir, _getpid());
#else
        const char* tmpdir = getenv("TMPDIR");
        if (!tmpdir) tmpdir = "/tmp";
        snprintf(tmppath, sizeof(tmppath), "%s/aleagit_XXXXXX.xml", tmpdir);
        int fd = mkstemps(tmppath, 4);
        if (fd < 0) {
            ag_error("cannot create temp file for OpenMC load");
            return NULL;
        }
        close(fd);
#endif
        FILE* tmpf = fopen(tmppath, "wb");
        if (!tmpf) {
            ag_error("cannot create temp file for OpenMC load");
            return NULL;
        }
        size_t written = fwrite(data, 1, len, tmpf);
        fclose(tmpf);
        if (written != len) {
            remove(tmppath);
            ag_error("failed to write temp file");
            return NULL;
        }
        alea_system_t* sys = alea_load_openmc(tmppath);
        remove(tmppath);
        return sys;
    }

    ag_error("unknown geometry format");
    return NULL;
}

alea_system_t* ag_load_geometry_file(const char* path) {
    geom_format_t fmt = ag_detect_format(path, NULL, 0);
    if (fmt == GEOM_FORMAT_OPENMC)
        return alea_load_openmc(path);
    return alea_load_mcnp(path);
}

alea_system_t* ag_load_geometry_commit(git_repository* repo,
                                       git_commit* commit,
                                       const char* path) {
    size_t len = 0;
    char* data = ag_read_blob(repo, commit, path, &len);
    if (!data) {
        ag_error("cannot read '%s' from commit", path);
        return NULL;
    }

    geom_format_t fmt = ag_detect_format(path, data, len);
    alea_system_t* sys = ag_load_geometry_buffer(data, len, fmt);
    free(data);
    return sys;
}

alea_system_t* ag_load_geometry_workdir(git_repository* repo, const char* path) {
    const char* workdir = git_repository_workdir(repo);
    if (!workdir) {
        ag_error("bare repository has no working directory");
        return NULL;
    }

    char fullpath[4096];
    snprintf(fullpath, sizeof(fullpath), "%s%s", workdir, path);
    return ag_load_geometry_file(fullpath);
}
