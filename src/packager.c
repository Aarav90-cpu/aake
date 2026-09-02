#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "aake.h"

// Helper to get file size
static size_t get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fclose(f);
    return sz;
}

// Helper to align up to multiple
static size_t align_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

// C version of pack_boot.py
static void pack_boot_x86_64(void) {
    printf("  [Native Packager] Building hybrid UEFI/BIOS boot.img...\n");
    
    // Read files
    size_t bootloader_sz = get_file_size("out_staging/bootloader.bin");
    size_t stage2_sz = get_file_size("out_staging/stage2.bin");
    size_t kernel_sz = get_file_size("kernel/prebuilts/bzImage");
    size_t initramfs_sz = get_file_size("finished/initramfs.img");
    size_t animation_sz = get_file_size("out_staging/animation.bin");
    
    if (kernel_sz == 0) {
        printf("  ERROR: Missing kernel at kernel/prebuilts/bzImage\n");
        return;
    }
    
    uint8_t *bootloader = malloc(bootloader_sz);
    uint8_t *stage2 = calloc(1, 7 * 512); // Padded to 7 sectors
    uint8_t *kernel = malloc(kernel_sz);
    uint8_t *initramfs = malloc(initramfs_sz);
    uint8_t *animation = animation_sz > 0 ? malloc(animation_sz) : NULL;
    
    FILE *f;
    f = fopen("out_staging/bootloader.bin", "rb"); if (f) { fread(bootloader, 1, bootloader_sz, f); fclose(f); }
    f = fopen("out_staging/stage2.bin", "rb"); if (f) { fread(stage2, 1, stage2_sz, f); fclose(f); }
    f = fopen("kernel/prebuilts/bzImage", "rb"); if (f) { fread(kernel, 1, kernel_sz, f); fclose(f); }
    f = fopen("finished/initramfs.img", "rb"); if (f) { fread(initramfs, 1, initramfs_sz, f); fclose(f); }
    if (animation) { f = fopen("out_staging/animation.bin", "rb"); if (f) { fread(animation, 1, animation_sz, f); fclose(f); } }
    
    // Calculate LBA sectors
    uint32_t bootloader_sectors = 1;
    uint32_t gpt_metadata_sectors = 33;
    
    uint32_t animation_lba = bootloader_sectors + gpt_metadata_sectors + 7;
    uint32_t animation_size_sectors = align_up(animation_sz, 512) / 512;
    
    uint32_t bzimage_lba = animation_lba + animation_size_sectors;
    uint32_t bzimage_size_sectors = align_up(kernel_sz, 512) / 512;
    
    uint32_t initramfs_lba = bzimage_lba + bzimage_size_sectors;
    uint32_t initramfs_size_sectors = align_up(initramfs_sz, 512) / 512;
    uint32_t initramfs_size_bytes = initramfs_sz;
    
    // Patch stage2 variables
    // Look for 8,0,0,0,0,0,0 (28 bytes)
    uint32_t pattern[7] = {8, 0, 0, 0, 0, 0, 0};
    int found_idx = -1;
    for (int i = 0; i < (7 * 512) - 28; i++) {
        if (memcmp(&stage2[i], pattern, 28) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx != -1) {
        uint32_t patched_vars[7] = {
            bzimage_lba, bzimage_size_sectors,
            initramfs_lba, initramfs_size_sectors, initramfs_size_bytes,
            animation_lba, animation_size_sectors
        };
        memcpy(&stage2[found_idx], patched_vars, 28);
    }
    
    // Construct BIOS part
    size_t bios_size_bytes = (1 * 512) + (gpt_metadata_sectors * 512) + (7 * 512) + 
                             (animation_size_sectors * 512) + (bzimage_size_sectors * 512) + 
                             (initramfs_size_sectors * 512);
    size_t bios_aligned_size = align_up(bios_size_bytes, 1024 * 1024);
    uint32_t bios_aligned_sectors = bios_aligned_size / 512;
    
    uint8_t *bios_data_padded = calloc(1, bios_aligned_size);
    size_t offset = 0;
    memcpy(bios_data_padded + offset, bootloader, bootloader_sz); offset += (1 * 512) + (gpt_metadata_sectors * 512);
    memcpy(bios_data_padded + offset, stage2, 7 * 512); offset += 7 * 512;
    if (animation) { memcpy(bios_data_padded + offset, animation, animation_sz); } offset += animation_size_sectors * 512;
    memcpy(bios_data_padded + offset, kernel, kernel_sz); offset += bzimage_size_sectors * 512;
    memcpy(bios_data_padded + offset, initramfs, initramfs_sz); offset += initramfs_size_sectors * 512;
    
    // Create EFI System Partition (FAT32)
    system("rm -f out_staging/efi_part.img");
    system("dd if=/dev/zero of=out_staging/efi_part.img bs=1M count=300 2>/dev/null");
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkfs.vfat -F 32 -h %u out_staging/efi_part.img >/dev/null 2>&1", bios_aligned_sectors);
    system(cmd);
    system("mmd -i out_staging/efi_part.img ::/EFI >/dev/null 2>&1");
    system("mmd -i out_staging/efi_part.img ::/EFI/BOOT >/dev/null 2>&1");
    system("mcopy -i out_staging/efi_part.img out_staging/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1");
    system("mcopy -i out_staging/efi_part.img kernel/prebuilts/bzImage ::/EFI/BOOT/bzImage >/dev/null 2>&1");
    system("mcopy -i out_staging/efi_part.img finished/initramfs.img ::/EFI/BOOT/initramfs.img >/dev/null 2>&1");
    if (animation) {
        system("mcopy -i out_staging/efi_part.img out_staging/animation.bin ::/EFI/BOOT/animation.bin >/dev/null 2>&1");
        system("mcopy -i out_staging/efi_part.img out_staging/animation.bin ::/animation.bin >/dev/null 2>&1");
    }
    
    size_t efi_sz = get_file_size("out_staging/efi_part.img");
    uint8_t *efi_data = malloc(efi_sz);
    f = fopen("out_staging/efi_part.img", "rb"); if (f) { fread(efi_data, 1, efi_sz, f); fclose(f); }
    uint32_t efi_part_sectors = efi_sz / 512;
    
    // Write out_path
    f = fopen("finished/boot.img", "wb");
    if (f) {
        fwrite(bios_data_padded, 1, bios_aligned_size, f);
        fwrite(efi_data, 1, efi_sz, f);
        
        // GPT backup padding
        uint8_t gpt_padding[33 * 512] = {0};
        fwrite(gpt_padding, 1, sizeof(gpt_padding), f);
        
        // Overwrite MBR boot code
        fseek(f, 0, SEEK_SET);
        fwrite(bootloader, 1, 446, f);
        
        fclose(f);
    }
    
    // sgdisk
    snprintf(cmd, sizeof(cmd), "sgdisk -g -n 1:%u:%u -t 1:ef00 -c 1:'EFI' finished/boot.img >/dev/null 2>&1", 
             bios_aligned_sectors, bios_aligned_sectors + efi_part_sectors - 1);
    system(cmd);
    
    printf("  [Native Packager] boot.img (hybrid UEFI/BIOS) built successfully!\n");
    
    free(bootloader);
    free(stage2);
    free(kernel);
    free(initramfs);
    if (animation) free(animation);
    free(bios_data_padded);
    free(efi_data);
}

static void pack_boot_arm64(void) {
    printf("  [Native Packager] Building rpi4.img...\n");
    
    system("mkdir -p finished/rpi4 out_staging/rpi4/boot");
    
    // Stage boot files
    system("cp kernel/prebuilts/Image out_staging/rpi4/boot/kernel8.img 2>/dev/null || true");
    system("cp kernel/prebuilts/arm64 out_staging/rpi4/boot/kernel8.img 2>/dev/null || true");
    system("cp finished/initramfs.img out_staging/rpi4/boot/initramfs.img 2>/dev/null || true");
    system("cp boot/rpi4/config.txt out_staging/rpi4/boot/config.txt 2>/dev/null || true");
    system("cp boot/rpi4/cmdline.txt out_staging/rpi4/boot/cmdline.txt 2>/dev/null || true");
    system("cp kernel/prebuilts/bcm2711-rpi-4-b.dtb out_staging/rpi4/boot/bcm2711-rpi-4-b.dtb 2>/dev/null || true");
    
    // Create boot partition
    system("dd if=/dev/zero of=out_staging/rpi4/boot.img bs=1M count=256 2>/dev/null");
    system("mkfs.vfat -F 32 -n ARKOS_BOOT out_staging/rpi4/boot.img 2>/dev/null");
    
    // mcopy
    system("mcopy -i out_staging/rpi4/boot.img out_staging/rpi4/boot/* ::/ >/dev/null 2>&1");
    
    // Assemble rpi4.img
    system("dd if=/dev/zero of=finished/rpi4/rpi4.img bs=1M count=2410 2>/dev/null");
    system("parted -s finished/rpi4/rpi4.img mklabel gpt");
    system("parted -s finished/rpi4/rpi4.img mkpart boot fat32 4MiB 260MiB");
    system("parted -s finished/rpi4/rpi4.img mkpart system ext4 260MiB 2308MiB");
    system("parted -s finished/rpi4/rpi4.img mkpart vendor ext4 2308MiB 2408MiB");
    system("parted -s finished/rpi4/rpi4.img set 1 boot on");
    
    system("dd if=out_staging/rpi4/boot.img of=finished/rpi4/rpi4.img bs=1M seek=4 conv=notrunc 2>/dev/null");
    system("dd if=finished/sys.img of=finished/rpi4/rpi4.img bs=1M seek=260 conv=notrunc 2>/dev/null");
    system("dd if=finished/vend.img of=finished/rpi4/rpi4.img bs=1M seek=2308 conv=notrunc 2>/dev/null");
    
    printf("  [Native Packager] rpi4.img built successfully!\n");
}

void package_sign(void) {
    system("mkdir -p out_staging");
    system("if [ ! -f keys/priv.pem ]; then mkdir -p keys && openssl genpkey -algorithm RSA -out keys/priv.pem -pkeyopt rsa_keygen_bits:2048 && openssl rsa -pubout -in keys/priv.pem -out keys/pub.pem; fi");
    system("openssl dgst -sha256 -sign keys/priv.pem -out out_staging/signature.bin builddir/arkrt");
}

void package_images(bool is_arm64) {
    system("mkdir -p finished out_staging/system/frameworks out_staging/system/services out_staging/vendor");
    
    system("cp -r frameworks/* out_staging/system/frameworks/ 2>/dev/null || true");
    system("cp -r system/services/* out_staging/system/services/ 2>/dev/null || true");
    system("cp -r vendor/* out_staging/vendor/ 2>/dev/null || true");
    
    package_sign();
    
    system("rm -rf out_staging/initramfs_ext && mkdir -p out_staging/initramfs_ext/system/services out_staging/initramfs_ext/run out_staging/initramfs_ext/tmp out_staging/initramfs_ext/var/log");
    system("cp builddir/arkrt out_staging/initramfs_ext/init && chmod +x out_staging/initramfs_ext/init");
    system("cp builddir/ui_test out_staging/initramfs_ext/system/");
    system("cp out_staging/signature.bin out_staging/initramfs_ext/signature.bin");
    
    system("mkdir -p out_staging/initramfs_ext/lib && cp -r arkrt/ark.display.graphics/prebuilts/x86_64/lib/* out_staging/initramfs_ext/lib/ 2>/dev/null || true");
    system("ldd arkrt/ark.display.graphics/prebuilts/x86_64/lib/*.so builddir/ui_test 2>/dev/null | grep -o '/[^ ]*\\.so[^ ]*' | grep -v 'prebuilts' | sort -u | xargs -I {} cp -L {} out_staging/initramfs_ext/lib/ 2>/dev/null || true");
    system("mkdir -p out_staging/initramfs_ext/lib64 && ln -sf ../lib/ld-linux-x86-64.so.2 out_staging/initramfs_ext/lib64/ld-linux-x86-64.so.2");
    system("cp -L /usr/lib/swift/lib/swift/linux/lib*.so out_staging/initramfs_ext/lib/ 2>/dev/null || cp -L /usr/lib/swift/linux/lib*.so out_staging/initramfs_ext/lib/ 2>/dev/null || true");
    
    system("cp -r out_staging/system/services/* out_staging/initramfs_ext/system/services/ 2>/dev/null || true");
    
    system("cd out_staging/initramfs_ext && find . | cpio -H newc -o 2>/dev/null | gzip > ../../finished/initramfs.img");
    
    system("dd if=/dev/zero of=finished/sys.img bs=1M count=2048 2>/dev/null");
    system("mkfs.ext4 -d out_staging/system finished/sys.img 2>/dev/null");
    
    system("dd if=/dev/zero of=finished/vend.img bs=1M count=100 2>/dev/null");
    system("mkfs.ext4 -F -d out_staging/vendor finished/vend.img 2>/dev/null || mkfs.ext4 -F finished/vend.img 2>/dev/null");
    
    if (!is_arm64) {
        pack_boot_x86_64();
    } else {
        pack_boot_arm64();
    }
    
    printf("Build assembly complete!\n");
}
