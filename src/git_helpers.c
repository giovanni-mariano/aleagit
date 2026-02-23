// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "git_helpers.h"
#include "util.h"
#include "aleagit.h"
#include <stdlib.h>
#include <string.h>

git_repository* ag_repo_open(void) {
    git_repository* repo = NULL;
    int err = git_repository_open_ext(&repo, ".", 0, NULL);
    if (err < 0) {
        const git_error* e = git_error_last();
        ag_error("not a git repository: %s", e ? e->message : "unknown error");
        return NULL;
    }
    return repo;
}

git_commit* ag_resolve_commit(git_repository* repo, const char* spec) {
    git_object* obj = NULL;
    int err = git_revparse_single(&obj, repo, spec);
    if (err < 0) {
        const git_error* e = git_error_last();
        ag_error("cannot resolve '%s': %s", spec, e ? e->message : "unknown");
        return NULL;
    }

    git_commit* commit = NULL;
    err = git_commit_lookup(&commit, repo, git_object_id(obj));
    git_object_free(obj);
    if (err < 0) {
        const git_error* e = git_error_last();
        ag_error("cannot load commit for '%s': %s", spec, e ? e->message : "unknown");
        return NULL;
    }
    return commit;
}

char* ag_read_blob(git_repository* repo, git_commit* commit,
                   const char* path, size_t* out_len) {
    git_tree* tree = NULL;
    if (git_commit_tree(&tree, commit) < 0) return NULL;

    git_tree_entry* entry = NULL;
    int err = git_tree_entry_bypath(&entry, tree, path);
    if (err < 0) {
        git_tree_free(tree);
        return NULL;
    }

    git_blob* blob = NULL;
    err = git_blob_lookup(&blob, repo, git_tree_entry_id(entry));
    git_tree_entry_free(entry);
    git_tree_free(tree);
    if (err < 0) return NULL;

    size_t len = git_blob_rawsize(blob);
    char* data = malloc(len + 1);
    if (!data) {
        git_blob_free(blob);
        return NULL;
    }
    memcpy(data, git_blob_rawcontent(blob), len);
    data[len] = '\0';
    *out_len = len;
    git_blob_free(blob);
    return data;
}

char* ag_read_staged_blob(git_repository* repo, const char* path,
                          size_t* out_len) {
    git_index* index = NULL;
    if (git_repository_index(&index, repo) < 0) return NULL;

    const git_index_entry* entry = git_index_get_bypath(index, path, 0);
    if (!entry) {
        git_index_free(index);
        return NULL;
    }

    git_blob* blob = NULL;
    int err = git_blob_lookup(&blob, repo, &entry->id);
    git_index_free(index);
    if (err < 0) return NULL;

    size_t len = git_blob_rawsize(blob);
    char* data = malloc(len + 1);
    if (!data) {
        git_blob_free(blob);
        return NULL;
    }
    memcpy(data, git_blob_rawcontent(blob), len);
    data[len] = '\0';
    *out_len = len;
    git_blob_free(blob);
    return data;
}

/* Check if a path looks like a geometry file */
static bool is_geometry_file(const char* path) {
    for (int i = 0; GEOM_EXTENSIONS[i]; i++) {
        if (ag_str_ends_with(path, GEOM_EXTENSIONS[i]))
            return true;
    }
    return false;
}

static void file_list_add(ag_file_list_t* list, const char* path) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->paths = realloc(list->paths, list->capacity * sizeof(char*));
    }
    list->paths[list->count++] = ag_strdup(path);
}

/* Recursive tree walk to find geometry files */
static int tree_walk_cb(const char* root, const git_tree_entry* entry,
                        void* payload) {
    ag_file_list_t* list = payload;
    if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) return 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s%s", root, git_tree_entry_name(entry));

    if (is_geometry_file(path))
        file_list_add(list, path);
    return 0;
}

ag_file_list_t* ag_find_geometry_files(git_repository* repo, git_commit* commit) {
    (void)repo;
    ag_file_list_t* list = calloc(1, sizeof(ag_file_list_t));
    if (!list) return NULL;

    git_tree* tree = NULL;
    if (git_commit_tree(&tree, commit) < 0) {
        free(list);
        return NULL;
    }

    git_tree_walk(tree, GIT_TREEWALK_PRE, tree_walk_cb, list);
    git_tree_free(tree);
    return list;
}

ag_file_list_t* ag_find_geometry_files_workdir(git_repository* repo) {
    ag_file_list_t* list = calloc(1, sizeof(ag_file_list_t));
    if (!list) return NULL;

    git_status_list* status = NULL;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    if (git_status_list_new(&status, repo, &opts) < 0) {
        free(list);
        return NULL;
    }

    size_t n = git_status_list_entrycount(status);
    for (size_t i = 0; i < n; i++) {
        const git_status_entry* se = git_status_byindex(status, i);
        const char* path = se->index_to_workdir ? se->index_to_workdir->new_file.path
                         : se->head_to_index   ? se->head_to_index->new_file.path
                         : NULL;
        if (path && is_geometry_file(path)) {
            /* Check for duplicates */
            bool dup = false;
            for (size_t j = 0; j < list->count; j++) {
                if (strcmp(list->paths[j], path) == 0) { dup = true; break; }
            }
            if (!dup) file_list_add(list, path);
        }
    }

    git_status_list_free(status);
    return list;
}

void ag_file_list_free(ag_file_list_t* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    free(list);
}

/* Get the blob OID for a file at a given commit */
static int get_blob_oid(git_repository* repo, git_commit* commit,
                        const char* path, git_oid* out) {
    (void)repo;
    git_tree* tree = NULL;
    if (git_commit_tree(&tree, commit) < 0) return -1;

    git_tree_entry* entry = NULL;
    int err = git_tree_entry_bypath(&entry, tree, path);
    if (err < 0) {
        git_tree_free(tree);
        return -1;
    }
    git_oid_cpy(out, git_tree_entry_id(entry));
    git_tree_entry_free(entry);
    git_tree_free(tree);
    return 0;
}

int ag_walk_history(git_repository* repo, const char* path,
                    ag_history_cb callback, void* payload) {
    git_revwalk* walker = NULL;
    if (git_revwalk_new(&walker, repo) < 0) return -1;

    git_revwalk_sorting(walker, GIT_SORT_TIME);
    git_revwalk_push_head(walker);

    git_oid oid;
    git_oid prev_blob_oid;
    bool have_prev = false;

    while (git_revwalk_next(&oid, walker) == 0) {
        git_commit* commit = NULL;
        if (git_commit_lookup(&commit, repo, &oid) < 0) continue;

        git_oid blob_oid;
        int found = get_blob_oid(repo, commit, path, &blob_oid);

        if (found < 0) {
            /* File doesn't exist at this commit */
            if (have_prev) {
                /* File was deleted/added boundary */
                have_prev = false;
            }
            git_commit_free(commit);
            continue;
        }

        if (!have_prev || git_oid_cmp(&blob_oid, &prev_blob_oid) != 0) {
            int ret = callback(commit, path, &blob_oid, payload);
            if (ret != 0) {
                git_commit_free(commit);
                break;
            }
        }

        git_oid_cpy(&prev_blob_oid, &blob_oid);
        have_prev = true;
        git_commit_free(commit);
    }

    git_revwalk_free(walker);
    return 0;
}

char* ag_short_oid(const git_oid* oid) {
    char full[GIT_OID_HEXSZ + 1];
    git_oid_tostr(full, sizeof(full), oid);
    char* s = malloc(8);
    if (s) {
        memcpy(s, full, 7);
        s[7] = '\0';
    }
    return s;
}
