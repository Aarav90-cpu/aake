#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "aake.h"

// Assuming ark_parser functionality
typedef struct {
    char key[256];
    char value[512];
} ArkPair;

typedef struct {
    char name[256];
    ArkPair pairs[16];
    int pair_count;
} ArkSection;

typedef struct {
    char filepath[512];
    ArkSection sections[16];
    int section_count;
} ArkConfig;

extern ArkConfig *ark_parse(const char *filepath);
extern const char *ark_get_section_val(ArkConfig *config, const char *section, const char *key);
extern void ark_free(ArkConfig *config);

static void process_ark_file(const char *dir_path) {
    char ark_path[1024];
    snprintf(ark_path, sizeof(ark_path), "%s/build.ark", dir_path);
    if (access(ark_path, F_OK) != 0) return;

    ArkConfig *cfg = ark_parse(ark_path);
    if (!cfg) return;

    char meson_path[1024];
    snprintf(meson_path, sizeof(meson_path), "%s/meson.build", dir_path);
    FILE *mf = fopen(meson_path, "w");
    if (!mf) {
        ark_free(cfg);
        return;
    }

    for (int i = 0; i < cfg->section_count; i++) {
        if (strcmp(cfg->sections[i].name, "TARGET") == 0) {
            const char *name = ark_get_section_val(cfg, "TARGET", "Name");
            const char *type = ark_get_section_val(cfg, "TARGET", "Type");
            const char *sources = ark_get_section_val(cfg, "TARGET", "Sources");
            const char *deps = ark_get_section_val(cfg, "TARGET", "Dependencies");
            const char *c_args = ark_get_section_val(cfg, "TARGET", "CArgs");
            
            if (!name || !type || !sources) continue;
            
            fprintf(mf, "%s = %s('%s', ", name, type, name);
            
            // Handle sources
            char src_copy[1024];
            strncpy(src_copy, sources, sizeof(src_copy));
            char *tok = strtok(src_copy, ", ");
            fprintf(mf, "[");
            bool first = true;
            while(tok) {
                if (!first) fprintf(mf, ", ");
                fprintf(mf, "'%s'", tok);
                first = false;
                tok = strtok(NULL, ", ");
            }
            fprintf(mf, "]");
            
            if (deps) {
                fprintf(mf, ", link_with: [%s]", deps);
            }
            if (c_args) {
                fprintf(mf, ", c_args: [%s]", c_args);
            }
            
            fprintf(mf, ")\n");
        }
    }

    fclose(mf);
    ark_free(cfg);
}

static void scan_dir(const char *dir, FILE *root_meson) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *dir_ent;
    while ((dir_ent = readdir(d)) != NULL) {
        if (strcmp(dir_ent->d_name, ".") == 0 || strcmp(dir_ent->d_name, "..") == 0) continue;
        if (strcmp(dir_ent->d_name, "builddir") == 0 || strcmp(dir_ent->d_name, "out_staging") == 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, dir_ent->d_name);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char ark_path[1024];
            snprintf(ark_path, sizeof(ark_path), "%s/build.ark", path);
            if (access(ark_path, F_OK) == 0) {
                fprintf(root_meson, "subdir('%s')\n", path + 2); // remove "./"
                process_ark_file(path);
            }
            scan_dir(path, root_meson);
        }
    }
    closedir(d);
}

void generate_blueprints(void) {
    // Generate root generated_blueprints.build
    FILE *f = fopen("generated_blueprints.build", "w");
    if (!f) return;
    
    // Check if we need to do this (we only scan specific paths to avoid infinite loops)
    scan_dir(".", f);
    fclose(f);
}
