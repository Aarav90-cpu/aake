#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "aake.h"

static void generate_cross_file(bool is_x86_64) {
    mkdir("cross", 0755);
    
    if (is_x86_64) {
        FILE *f = fopen("cross/x86_64.ini", "w");
        if (!f) return;
        fprintf(f, "[binaries]\n");
        fprintf(f, "c = ['/home/arkos/repo/arkos/prebuilts/clang/bin/clang', '-target', 'x86_64-linux-musl', '--sysroot=/home/arkos/repo/arkos/prebuilts/Swift/swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/musl-1.2.5.sdk/x86_64']\n");
        fprintf(f, "cpp = ['/home/arkos/repo/arkos/prebuilts/clang/bin/clang++', '-target', 'x86_64-linux-musl', '--sysroot=/home/arkos/repo/arkos/prebuilts/Swift/swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/musl-1.2.5.sdk/x86_64']\n");
        fprintf(f, "ar = 'ar'\n");
        fprintf(f, "strip = 'strip'\n");
        fprintf(f, "pkgconfig = 'pkg-config'\n\n");
        fprintf(f, "[host_machine]\n");
        fprintf(f, "system = 'linux'\n");
        fprintf(f, "cpu_family = 'x86_64'\n");
        fprintf(f, "cpu = 'x86_64'\n");
        fprintf(f, "endian = 'little'\n");
        fclose(f);
    } else {
        FILE *f = fopen("cross/aarch64.ini", "w");
        if (!f) return;
        fprintf(f, "[binaries]\n");
        fprintf(f, "c = ['/home/arkos/repo/arkos/prebuilts/clang/bin/clang', '-target', 'aarch64-linux-musl', '--sysroot=/home/arkos/repo/arkos/prebuilts/Swift/swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/musl-1.2.5.sdk/aarch64']\n");
        fprintf(f, "cpp = ['/home/arkos/repo/arkos/prebuilts/clang/bin/clang++', '-target', 'aarch64-linux-musl', '--sysroot=/home/arkos/repo/arkos/prebuilts/Swift/swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/musl-1.2.5.sdk/aarch64']\n");
        fprintf(f, "ar = 'aarch64-linux-gnu-ar'\n");
        fprintf(f, "strip = 'aarch64-linux-gnu-strip'\n");
        fprintf(f, "pkgconfig = 'aarch64-linux-gnu-pkg-config'\n\n");
        fprintf(f, "[host_machine]\n");
        fprintf(f, "system = 'linux'\n");
        fprintf(f, "cpu_family = 'aarch64'\n");
        fprintf(f, "cpu = 'aarch64'\n");
        fprintf(f, "endian = 'little'\n");
        fclose(f);
    }
}

static void run_test(bool is_x86_64, bool is_uefi) {
    char cmd[1024];
    if (is_x86_64) {
        if (is_uefi) {
            snprintf(cmd, sizeof(cmd), "qemu-system-x86_64 -m 2G -enable-kvm -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd -drive if=pflash,format=raw,file=ovmf_vars.fd -drive file=finished/boot.img,format=raw -serial stdio");
            // Setup vars
            system("cp /usr/share/OVMF/OVMF_VARS.fd ovmf_vars.fd 2>/dev/null");
        } else {
            snprintf(cmd, sizeof(cmd), "qemu-system-x86_64 -m 2G -enable-kvm -drive file=finished/boot.img,format=raw -serial stdio");
        }
    } else {
        snprintf(cmd, sizeof(cmd), "qemu-system-aarch64 -M virt -cpu max -m 2G -device virtio-gpu-pci -device virtio-keyboard-pci -drive file=finished/rpi4/rpi4.img,format=raw,if=virtio -serial stdio");
    }
    printf("Running: %s\n", cmd);
    system(cmd);
}

static void print_help(void) {
    printf("ArkOS Build System (aake)\n\n");
    printf("Usage:\n");
    printf("  aake [options] [commands]\n\n");
    printf("Commands:\n");
    printf("  build          Build ArkOS for the host architecture (default if options passed)\n");
    printf("  test           Run ArkOS in QEMU (automatically builds if needed)\n");
    printf("  clean          Clean the build directory\n");
    printf("  sync           Sync source code (git pull)\n");
    printf("  sign           Sign binaries\n");
    printf("  font-gen       Generate TTF fonts to Swift/C arrays\n");
    printf("  cursor-gen     Generate mouse cursor arrays\n\n");
    printf("Options:\n");
    printf("  --x86_64       Build/test for x86_64 architecture\n");
    printf("  --arm64        Build/test for ARM64 architecture\n");
    printf("  --uefi         Test in UEFI mode (for x86_64)\n");
    printf("  --bios         Test in BIOS mode (for x86_64)\n");
    printf("  -v             Verbose output\n");
    printf("  -j <jobs>      Number of parallel jobs\n");
    printf("  --no-ninja     Generate build files but don't compile\n");
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    bool do_ninja = true;
    bool is_x86_64 = false;
    bool is_arm64 = false;
    bool is_test = false;
    bool is_uefi = false;
    bool verbose = false;
    int jobs = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "build") == 0) {
            // default behavior
        } else if (strcmp(argv[i], "--x86_64") == 0) {
            is_x86_64 = true;
        } else if (strcmp(argv[i], "--arm64") == 0 || strcmp(argv[i], "--arch64") == 0) {
            is_arm64 = true;
        } else if (strcmp(argv[i], "test") == 0) {
            is_test = true;
        } else if (strcmp(argv[i], "--uefi") == 0) {
            is_uefi = true;
        } else if (strcmp(argv[i], "--bios") == 0) {
            is_uefi = false;
        } else if (strcmp(argv[i], "clean") == 0) {
            printf("Cleaning build directory...\n");
            system("rm -rf builddir finished out_staging cross");
            return 0;
        } else if (strcmp(argv[i], "sync") == 0) {
            printf("Syncing source code...\n");
            system("git pull");
            return 0;
        } else if (strcmp(argv[i], "sign") == 0) {
            printf("Signing binaries...\n");
            package_sign();
            return 0;
        } else if (strcmp(argv[i], "font-gen") == 0) {
            generate_fonts();
            return 0;
        } else if (strcmp(argv[i], "cursor-gen") == 0) {
            generate_cursors();
            return 0;
        } else if (strcmp(argv[i], "--no-ninja") == 0) {
            do_ninja = false;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--arkos-version") == 0) {
            printf("ArkOS version: current alpha 0.0.1:45\n");
            return 0;
        } else if (strcmp(argv[i], "--no-warnings") == 0) {
            // Ignore
        } else if (strncmp(argv[i], "-j", 2) == 0) {
            jobs = atoi(argv[i] + 2);
        }
    }

    if (!is_x86_64 && !is_arm64) {
        struct utsname unameData;
        uname(&unameData);
        if (strstr(unameData.machine, "x86_64") || strstr(unameData.machine, "amd64")) {
            is_x86_64 = true;
        } else {
            is_arm64 = true;
        }
    }



    // 1. Generate blueprints
    printf("Parsing .ark blueprints and generating meson.build...\n");
    generate_blueprints();

    // 2. Cross Compilation files
    generate_cross_file(is_x86_64);

    // 3. Meson Setup
    if (access("builddir/build.ninja", F_OK) != 0) {
        printf("Running Meson setup...\n");
        char cmd[256];
        if (is_x86_64) {
            snprintf(cmd, sizeof(cmd), "meson setup builddir --cross-file cross/x86_64.ini");
        } else {
            snprintf(cmd, sizeof(cmd), "meson setup builddir --cross-file cross/aarch64.ini");
        }
        if (system(cmd) != 0) {
            printf("Meson setup failed!\n");
            return 1;
        }
    }

    // 4. Ninja Compilation
    if (do_ninja) {
        if (run_ninja_ui(jobs, verbose, is_x86_64 ? "x86_64" : "aarch64") != 0) {
            return 1; // build failed
        }
        
        // 5. Packaging
        printf("Packaging ArkOS...\n");
        package_images(is_arm64);
    }

    if (is_test) {
        run_test(is_x86_64, is_uefi);
    }

    return 0;
}
