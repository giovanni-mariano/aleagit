// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef ALEAGIT_GIT_HELPERS_H
#define ALEAGIT_GIT_HELPERS_H

#include <git2.h>
#include <stdbool.h>
#include <stddef.h>

/* Open repository at or above CWD. Caller must git_repository_free(). */
git_repository* ag_repo_open(void);

/* Resolve a revision spec ("HEAD", "HEAD~3", sha, branch) to a commit.
   Caller must git_commit_free(). Returns NULL on error. */
git_commit* ag_resolve_commit(git_repository* repo, const char* spec);

/* Read file content from a specific commit. Returns malloc'd buffer.
   Sets *out_len to the size. Returns NULL if file not found. */
char* ag_read_blob(git_repository* repo, git_commit* commit,
                   const char* path, size_t* out_len);

/* Read file content from the working tree index (staged).
   Returns malloc'd buffer, sets *out_len. Returns NULL on error. */
char* ag_read_staged_blob(git_repository* repo, const char* path,
                          size_t* out_len);

/* Geometry file list */
typedef struct {
    char** paths;
    size_t count;
    size_t capacity;
} ag_file_list_t;

/* Find geometry files in a commit's tree.
   Caller must ag_file_list_free(). */
ag_file_list_t* ag_find_geometry_files(git_repository* repo, git_commit* commit);

/* Find geometry files in the working directory. */
ag_file_list_t* ag_find_geometry_files_workdir(git_repository* repo);

void ag_file_list_free(ag_file_list_t* list);

/* History walking callback. Return 0 to continue, non-zero to stop. */
typedef int (*ag_history_cb)(git_commit* commit, const char* path,
                             const git_oid* blob_oid, void* payload);

/* Walk commits that changed a specific file. */
int ag_walk_history(git_repository* repo, const char* path,
                    ag_history_cb callback, void* payload);

/* Get short sha string (caller must free). */
char* ag_short_oid(const git_oid* oid);

#endif /* ALEAGIT_GIT_HELPERS_H */
