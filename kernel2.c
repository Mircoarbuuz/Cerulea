#include <stdint.h>

/* Multiboot header: must be in the first 8 KB */
__attribute__((section(".multiboot"), used))
const uint32_t multiboot_header[] = {
    0x1BADB002,                        // magic
    0x00010003 | (1 << 12) | (1 << 16),// flags: align + mem info + framebuffer
    (uint32_t)-(0x1BADB002 + 0x00010003 + (1 << 12) + (1 << 16)) // checksum
};

/* Stack in .bss */
__attribute__((section(".bss")))
uint8_t stack[8192];

/* Example uninitialized .bss variable */
__attribute__((section(".bss")))
uint32_t example_bss_var;

/* Multiboot info structure */
typedef struct {
    uint32_t flags;
    uint32_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_type;
} multiboot_info_t;

/* External symbols for .bss */
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;

void kmain(multiboot_info_t* mb_info);

/* Entry point */
__attribute__((naked))
void _start(void) {
    asm volatile(
        "cli\n"                         // Disable interrupts

        // Clear .bss
        "movl $__bss_start__, %edi\n"
        "movl $__bss_end__, %ecx\n"
        "sub %edi, %ecx\n"
        "xor %eax, %eax\n"
        "shr $2, %ecx\n"
        "rep stosl\n"

        // Setup stack
        "lea stack+8192, %esp\n"

        // GRUB passes multiboot info in EAX
        "movl %eax, %ebx\n"
        "push %ebx\n"
        "call kmain\n"

        "hlt\n"
    );
}

/* Kernel main */
void kmain(multiboot_info_t* mb_info) {
    if (!(mb_info->flags & (1 << 12))) while(1); // No framebuffer

    uint32_t width  = mb_info->framebuffer_width;
    uint32_t height = mb_info->framebuffer_height;
    uint32_t pitch  = mb_info->framebuffer_pitch;
    uint32_t fb_addr = mb_info->framebuffer_addr;

    if (!fb_addr || fb_addr > 0xE0000000) while(1); // Invalid framebuffer

    volatile uint8_t* framebuffer = (volatile uint8_t*)fb_addr;

    // Fill screen turquoise (RGB 0,255,255)
    for (uint32_t y=0; y<height; y++) {
        volatile uint8_t* row = framebuffer + y*pitch;
        for (uint32_t x=0; x<width; x++) {
            row[x*4+0] = 255; // Blue
            row[x*4+1] = 255; // Green
            row[x*4+2] = 0;   // Red
            row[x*4+3] = 0;   // Reserved
        }
    }

    while(1);
}
