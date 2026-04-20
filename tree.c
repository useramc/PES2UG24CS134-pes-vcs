// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
//   "<mode-as-ascii-octal> <name>\0<32-byte-binary-hash>"
//
// Example single entry (conceptual):
//   "100644 hello.txt\0" followed by 32 raw bytes of SHA-256

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── PROVIDED ───────────────────────────────────────────────────────────────

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
// Returns 0 on success, -1 on parse error.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        // 1. Safely find the space character for the mode
        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1; // Malformed data

        // Parse mode into an isolated buffer
        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1; // Skip space

        // 2. Safely find the null terminator for the name
        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1; // Malformed data

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; // Ensure null-terminated

        ptr = null_byte + 1; // Skip null byte

        // 3. Read the 32-byte binary hash
        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Estimate max size: (6 bytes mode + 1 byte space + 256 bytes name + 1 byte null + 32 bytes hash) per entry
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    // Create a mutable copy to sort entries (Git requirement)
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        // Write mode and name (%o writes octal correctly for Git standards)
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 to step over the null terminator written by sprintf
        
        // Write binary hash
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

typedef struct BuildNode {
    char name[256];
    uint32_t mode;
    ObjectID hash;
    int is_file;
    struct BuildNode *child;
    struct BuildNode *sibling;
} BuildNode;

static BuildNode *build_node_create(const char *name, uint32_t mode, const ObjectID *hash, int is_file) {
    BuildNode *node = malloc(sizeof(BuildNode));
    if (node == NULL) {
        return NULL;
    }

    if (strlen(name) >= sizeof(node->name)) {
        free(node);
        return NULL;
    }

    strcpy(node->name, name);
    node->mode = mode;
    if (hash != NULL) {
        node->hash = *hash;
    } else {
        memset(&node->hash, 0, sizeof(node->hash));
    }
    node->is_file = is_file;
    node->child = NULL;
    node->sibling = NULL;
    return node;
}

static void build_node_free(BuildNode *node) {
    if (node == NULL) {
        return;
    }

    build_node_free(node->child);
    build_node_free(node->sibling);
    free(node);
}

static BuildNode *build_node_find_child(BuildNode *parent, const char *name) {
    for (BuildNode *child = parent->child; child != NULL; child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
    }
    return NULL;
}

static BuildNode *build_node_add_child(BuildNode *parent, const char *name, uint32_t mode, const ObjectID *hash, int is_file) {
    BuildNode *node = build_node_create(name, mode, hash, is_file);
    if (node == NULL) {
        return NULL;
    }

    node->sibling = parent->child;
    parent->child = node;
    return node;
}

static int tree_add_entry(Tree *tree, uint32_t mode, const ObjectID *hash, const char *name) {
    TreeEntry *entry;

    if (tree->count >= MAX_TREE_ENTRIES || strlen(name) >= sizeof(tree->entries[0].name)) {
        return -1;
    }

    entry = &tree->entries[tree->count++];
    entry->mode = mode;
    entry->hash = *hash;
    strcpy(entry->name, name);
    return 0;
}

static int insert_index_path(BuildNode *parent, char *path, const IndexEntry *entry) {
    char *slash = strchr(path, '/');

    if (*path == '\0') {
        return -1;
    }

    if (slash == NULL) {
        BuildNode *file = build_node_find_child(parent, path);

        if (file == NULL) {
            file = build_node_add_child(parent, path, entry->mode, &entry->hash, 1);
            if (file == NULL) {
                return -1;
            }
        } else if (!file->is_file) {
            return -1;
        } else {
            file->mode = entry->mode;
            file->hash = entry->hash;
        }
        return 0;
    }

    *slash = '\0';

    {
        BuildNode *dir = build_node_find_child(parent, path);
        if (dir == NULL) {
            dir = build_node_add_child(parent, path, MODE_DIR, NULL, 0);
            if (dir == NULL) {
                return -1;
            }
        } else if (dir->is_file) {
            return -1;
        }

        return insert_index_path(dir, slash + 1, entry);
    }
}

static int write_tree_from_node(const BuildNode *node, ObjectID *id_out) {
    Tree tree;
    void *data = NULL;
    size_t len = 0;

    tree.count = 0;

    for (const BuildNode *child = node->child; child != NULL; child = child->sibling) {
        ObjectID child_id;

        if (child->is_file) {
            if (tree_add_entry(&tree, child->mode, &child->hash, child->name) != 0) {
                return -1;
            }
            continue;
        }

        if (write_tree_from_node(child, &child_id) != 0) {
            return -1;
        }

        if (tree_add_entry(&tree, MODE_DIR, &child_id, child->name) != 0) {
            return -1;
        }
    }

    if (tree_serialize(&tree, &data, &len) != 0) {
        return -1;
    }

    if (object_write(OBJ_TREE, data, len, id_out) != 0) {
        free(data);
        return -1;
    }

    free(data);
    return 0;
}

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
//
// HINTS - Useful functions and concepts for this phase:
//   - index_load      : load the staged files into memory
//   - strchr          : find the first '/' in a path to separate directories from files
//   - strncmp         : compare prefixes to group files belonging to the same subdirectory
//   - Recursion       : you will likely want to create a recursive helper function 
//                       (e.g., `write_tree_level(entries, count, depth)`) to handle nested dirs.
//   - tree_serialize  : convert your populated Tree struct into a binary buffer
//   - object_write    : save that binary buffer to the store as OBJ_TREE
//
// Returns 0 on success, -1 on error.
int tree_from_index(ObjectID *id_out) {
    Index index;
    BuildNode *root;
    int rc;

    if (id_out == NULL) {
        return -1;
    }

    if (index_load(&index) != 0) {
        return -1;
    }

    root = build_node_create("", MODE_DIR, NULL, 0);
    if (root == NULL) {
        return -1;
    }

    for (int i = 0; i < index.count; i++) {
        char path_copy[sizeof(index.entries[i].path)];

        if (strlen(index.entries[i].path) >= sizeof(path_copy)) {
            build_node_free(root);
            return -1;
        }

        strcpy(path_copy, index.entries[i].path);
        if (insert_index_path(root, path_copy, &index.entries[i]) != 0) {
            build_node_free(root);
            return -1;
        }
    }

    rc = write_tree_from_node(root, id_out);
    build_node_free(root);
    return rc;
}
