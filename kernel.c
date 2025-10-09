#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
// Simple busy-wait delay
void delay(uint32_t cycles){
    for(uint32_t i = 0; i < cycles; i++){
        __asm__ volatile("nop");
    }
}
const char* logo[] = {
    "----  ---  ----",
    "--  ---      --",
    "--  ---      --",
    "--  ---      --",
    "----  ---  ----"
};
// DISK CONFIG
// =======================
#define DISK_SIZE 65536  // 64 KB virtual disk
int DISK_KB = DISK_SIZE / 1024;
static char disk[DISK_SIZE];
static uint32_t disk_used = 0;

// =======================
// MULTIBOOT HEADER
// =======================
__attribute__((section(".multiboot"))) 
struct { uint32_t magic, flags, checksum; } mb = {0x1BADB002, 3, -(0x1BADB002+3)};

// =======================
// STACK
// =======================
__attribute__((section(".bss"))) static char stack[16384];

// =======================
// VGA
// =======================
#define VGAVIDEO 0xB8000
#define MAX_COLS 80
#define MAX_ROWS 25
static int cursor_row = 0;
static int cursor_col = 0;

// =======================
// NOTES SYSTEM
// =======================
#define MAX_NOTES 256
#define NOTE_LENGTH 100
static char notes[MAX_NOTES][NOTE_LENGTH];
static int note_count = 0;

// =======================
// COMMAND HISTORY
// =======================
#define MAX_HISTORY 16
// NOTE: PROMPT_LEN must be updated if prompt string changes!
// Length of "[cerulea ] > " + path length
#define BASE_PROMPT_LEN 12 
static char history[MAX_HISTORY][128];
static int history_count = 0;
static int history_index = 0;

// =======================
// KEYBOARD STATE
// =======================
static bool shift_pressed_left = false;
static bool shift_pressed_right = false;
// =======================
// MULTIBOOT STRUCTS
// =======================
typedef struct {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms_count;
    uint32_t syms_addr;
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;


// Example directory at some memory location


// =======================
// STRING HELPERS
// =======================
size_t strlen(const char* s) { 
    size_t len = 0;
    while(s[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char* s1,const char* s2){ while(*s1 && *s1==*s2){ s1++; s2++;} return *(unsigned char*)s1-*(unsigned char*)s2; }
void strcpy(char* d,const char* s){ while((*d++=*s++)); }

// Simple string concatenation function
void strcat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*dest++ = *src++);
}

// Function to find the first occurrence of a substring in a string
const char* strstr(const char* haystack, const char* needle) {
    size_t needle_len = strlen(needle);
    if (!needle_len) return haystack;
    
    for (size_t i = 0; haystack[i]; i++) {
        if (haystack[i] == needle[0]) {
            size_t j;
            for (j = 0; j < needle_len; j++) {
                if (haystack[i + j] != needle[j]) break;
            }
            if (j == needle_len) return &haystack[i];
        }
    }
    return NULL;
}

void itoa(int n,char* str){ int i=0,sign=n;if(sign<0)n=-n;do{str[i++]=n%10+'0';}while((n/=10)>0);if(sign<0)str[i++]='-';str[i]=0;for(int a=0,b=i-1;a<b;a++,b--){char t=str[a]; str[a]=str[b]; str[b]=t;}}

void itoa_f(double n, char* str, int precision) {
    if (precision < 0) precision = 6; // default precision
    int i = 0;

    // Handle negative numbers
    if (n < 0) {
        str[i++] = '-';
        n = -n;
    }

    // Extract integer part (32-bit safe)
    int int_part = (int)n;
    double frac_part = n - int_part;

    // Convert integer part to string
    char int_str[12]; // enough for 32-bit int
    int j = 0;
    if (int_part == 0) {
        int_str[j++] = '0';
    } else {
        while (int_part > 0) {
            int_str[j++] = int_part % 10 + '0';
            int_part /= 10;
        }
    }

    // Reverse integer string into output
    for (int k = 0; k < j; k++)
        str[i + k] = int_str[j - k - 1];
    i += j;

    // Add decimal point
    str[i++] = '.';

    // Convert fractional part
    for (int k = 0; k < precision; k++) {
        frac_part *= 10;
        int digit = (int)frac_part;
        str[i++] = digit + '0';
        frac_part -= digit;
    }

    // Null terminate
    str[i] = '\0';
}

// =======================
// VGA HELPERS
// =======================
static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t ret; __asm__ volatile("inb %1,%0":"=a"(ret):"Nd"(port)); return ret; }
uint8_t read_rtc(uint8_t reg){
    outb(0x70, reg);
    return inb(0x71);
}

uint8_t bcd2bin(uint8_t val){ return ((val>>4)*10) + (val & 0x0F); }

void get_rtc_time(uint8_t *h,uint8_t *m,uint8_t *s){
    // Wait until RTC not updating
    while(read_rtc(0x0A) & 0x80);

    uint8_t hours = read_rtc(0x04);
    uint8_t minutes = read_rtc(0x02);
    uint8_t seconds = read_rtc(0x00);
    uint8_t regB = read_rtc(0x0B);

    // BCD to binary
    if(!(regB & 0x04)){
        hours = bcd2bin(hours);
        minutes = bcd2bin(minutes);
        seconds = bcd2bin(seconds);
    }

    // 12-hour to 24-hour
    if(!(regB & 0x02) && (hours & 0x80)){
        hours = ((hours & 0x7F)+12) % 24;
    }

    *h = hours;
    *m = minutes;
    *s = seconds;
}

static void update_cursor(){ 
    uint16_t pos = cursor_row * MAX_COLS + cursor_col; 
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos&0xFF)); 
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos>>8)&0xFF)); 
}

void scroll_up(){ 
    char* video = (char*)VGAVIDEO; 
    for(int r=1;r<MAX_ROWS;r++) 
        for(int c=0;c<MAX_COLS;c++){ int from=(r*MAX_COLS+c)*2,to=((r-1)*MAX_COLS+c)*2; video[to]=video[from]; video[to+1]=video[from+1]; }
    for(int c=0;c<MAX_COLS;c++){ int pos=((MAX_ROWS-1)*MAX_COLS+c)*2; video[pos]=' '; video[pos+1]=0x07; } 
    if(cursor_row>0) cursor_row--; 
}

void cls(){ 
    char* video=(char*)VGAVIDEO; 
    for(int i=0;i<MAX_COLS*MAX_ROWS;i++){ video[i*2]=' '; video[i*2+1]=0x07; }
    cursor_row=0; cursor_col=0; update_cursor(); 
}

void kprint_col(const char* str,uint8_t color){ 
    char *v=(char*)VGAVIDEO; int i=0; 
    while(str[i]){ 
        if(str[i]=='\n'){ cursor_col=0; cursor_row++; if(cursor_row>=MAX_ROWS) scroll_up(); } 
        else { 
            int pos=cursor_row*MAX_COLS+cursor_col; v[pos*2]=str[i]; v[pos*2+1]=color; cursor_col++; 
            if(cursor_col>=MAX_COLS){ cursor_col=0; cursor_row++; if(cursor_row>=MAX_ROWS) scroll_up(); } 
        } 
        i++; 
    } 
    update_cursor(); 
}

void kprint(const char* str){ 
    char *v=(char*)VGAVIDEO; int i=0; 
    while(str[i]){ 
        if(str[i]=='\n'){ cursor_col=0; cursor_row++; if(cursor_row>=MAX_ROWS) scroll_up(); } 
        else { 
            int pos=cursor_row*MAX_COLS+cursor_col; v[pos*2]=str[i]; v[pos*2+1]=0x07; cursor_col++; 
            if(cursor_col>=MAX_COLS){ cursor_col=0; cursor_row++; if(cursor_row>=MAX_ROWS) scroll_up(); } 
        } 
        i++; 
    } 
    update_cursor(); 
}
void kputchar(char c){ char s[2]={c,0}; kprint(s); }
void kputchar_col(char c, uint8_t col){ char s[2]={c,0}; kprint_col(s, col); }
void kprint_int(int n,uint8_t color){ char buf[32]; itoa(n,buf); kprint_col(buf,color); }
void kprint_float(double d,uint8_t color){ char buf[32]; itoa_f(d,buf, 6); kprint_col(buf,color); }
void kprint_hex(uint16_t u){
    char buf[16];
    itoa(u, buf);
    kprint(buf);
}
/**
 * Clears only the text on the current input line (after the prompt).
 * This is crucial for history navigation to avoid clearing the whole screen.
 */
void clear_current_line(int prompt_len) {
    // Overwrite the current command with spaces
    for (int i = prompt_len; i < MAX_COLS; i++) {
        char *v = (char*)VGAVIDEO;
        int pos = cursor_row * MAX_COLS + i;
        v[pos*2] = ' ';
        v[pos*2+1] = 0x07; // Default color
    }
    
    // Move cursor back to the start of the command entry area
    cursor_col = prompt_len;
    update_cursor();
}

// =======================
// KEYBOARD
// =======================
char kgetchar(){ uint8_t sc; while(!(inb(0x64)&1)); sc=inb(0x60); return sc; }

char scancode_to_ascii(char sc){ 
    if(sc==0x2A){ shift_pressed_left=true; return 0; }
    if(sc==0x36){ shift_pressed_right=true; return 0; }
    
    // Handle Shift releases (0xAA = Left Shift Release, 0xB6 = Right Shift Release)
    if(sc==0xAA){ shift_pressed_left=false; return 0; }
    if(sc==0xB6){ shift_pressed_right=false; return 0; }
    
    // Ignore other key releases (any scancode with bit 7 set)
    if (sc & 0x80) {
        return 0; // Key release, ignore
    }

    switch(sc){ 
        case 0x1C: return '\n'; 
        case 0x0E: return '\b'; 
        
        // Numbers with symbols
        case 0x02: return shift_pressed_left || shift_pressed_right ? '!' : '1';
        case 0x03: return shift_pressed_left || shift_pressed_right ? '@' : '2';
        case 0x04: return shift_pressed_left || shift_pressed_right ? '#' : '3';
        case 0x05: return shift_pressed_left || shift_pressed_right ? '$' : '4';
        case 0x06: return shift_pressed_left || shift_pressed_right ? '%' : '5';
        case 0x07: return shift_pressed_left || shift_pressed_right ? '^' : '6';
        case 0x08: return shift_pressed_left || shift_pressed_right ? '&' : '7';
        case 0x09: return shift_pressed_left || shift_pressed_right ? '*' : '8';
        case 0x0A: return shift_pressed_left || shift_pressed_right ? '(' : '9';
        case 0x0B: return shift_pressed_left || shift_pressed_right ? ')' : '0';
        
        // Letters
        case 0x10: return shift_pressed_left || shift_pressed_right ? 'Q' : 'q';
        case 0x11: return shift_pressed_left || shift_pressed_right ? 'W' : 'w';
        case 0x12: return shift_pressed_left || shift_pressed_right ? 'E' : 'e';
        case 0x13: return shift_pressed_left || shift_pressed_right ? 'R' : 'r';
        case 0x14: return shift_pressed_left || shift_pressed_right ? 'T' : 't';
        case 0x15: return shift_pressed_left || shift_pressed_right ? 'Y' : 'y';
        case 0x16: return shift_pressed_left || shift_pressed_right ? 'U' : 'u';
        case 0x17: return shift_pressed_left || shift_pressed_right ? 'I' : 'i';
        case 0x18: return shift_pressed_left || shift_pressed_right ? 'O' : 'o';
        case 0x19: return shift_pressed_left || shift_pressed_right ? 'P' : 'p';
        case 0x1E: return shift_pressed_left || shift_pressed_right ? 'A' : 'a';
        case 0x1F: return shift_pressed_left || shift_pressed_right ? 'S' : 's';
        case 0x20: return shift_pressed_left || shift_pressed_right ? 'D' : 'd';
        case 0x21: return shift_pressed_left || shift_pressed_right ? 'F' : 'f';
        case 0x22: return shift_pressed_left || shift_pressed_right ? 'G' : 'g';
        case 0x23: return shift_pressed_left || shift_pressed_right ? 'H' : 'h';
        case 0x24: return shift_pressed_left || shift_pressed_right ? 'J' : 'j';
        case 0x25: return shift_pressed_left || shift_pressed_right ? 'K' : 'k';
        case 0x26: return shift_pressed_left || shift_pressed_right ? 'L' : 'l';
        case 0x2C: return shift_pressed_left || shift_pressed_right ? 'Z' : 'z';
        case 0x2D: return shift_pressed_left || shift_pressed_right ? 'X' : 'x';
        case 0x2E: return shift_pressed_left || shift_pressed_right ? 'C' : 'c';
        case 0x2F: return shift_pressed_left || shift_pressed_right ? 'V' : 'v';
        case 0x30: return shift_pressed_left || shift_pressed_right ? 'B' : 'b';
        case 0x31: return shift_pressed_left || shift_pressed_right ? 'N' : 'n';
        case 0x32: return shift_pressed_left || shift_pressed_right ? 'M' : 'm';
        
        case 0x39: return ' '; // Space
        
        // Symbols
        case 0x0C: return shift_pressed_left || shift_pressed_right ? '_' : '-';  // Minus/Underscore
        case 0x34: return shift_pressed_left || shift_pressed_right ? '>' : '.';  // Period/Greater than
        case 0x35: return shift_pressed_left || shift_pressed_right ? '?' : '/';  // Forward slash/Question mark
        case 0x2B: return shift_pressed_left || shift_pressed_right ? '|' : '\\'; // Backslash/Pipe
        case 0x0D: return shift_pressed_left || shift_pressed_right ? '+' : '=';  // Equals/Plus
        case 0x27: return shift_pressed_left || shift_pressed_right ? ':' : ';';  // Semicolon/Colon
        case 0x1A: return shift_pressed_left || shift_pressed_right ? '{' : '[';  // Left bracket/brace
        case 0x1B: return shift_pressed_left || shift_pressed_right ? '}' : ']';  // Right bracket/brace
        case 0x28: return shift_pressed_left || shift_pressed_right ? '"' : '\''; // Single quote/Double quote

        default: return 0; 
    } 
}

// =======================
// NOTES FUNCTIONS
// =======================
void add_note(const char* content){ if(note_count<MAX_NOTES){ strcpy(notes[note_count++],content); kprint_col("Note added.\n",0x0A); } else kprint_col("Error: Notes full\n",0x04); }
void read_notes(){ if(note_count==0){ kprint("No notes\n"); return; } for(int i=0;i<note_count;i++){ kprint(" - "); kprint(notes[i]); kputchar('\n'); } }

// =======================
// FILESYSTEM (RAM for now)
// =======================
#define MAX_FILES 256
#define MAX_FILENAME 512
#define MAX_FILE_SIZE 512
typedef struct{ 
    char name[MAX_FILENAME]; 
    uint32_t offset; 
    uint32_t size; 
    int is_dir;
    int is_crl; 
    int parent_index; // Index of the parent directory in the 'files' array
} file_entry_t;
static file_entry_t files[MAX_FILES]; 
static int file_count=0;

// Global variable to track the current directory index
static int current_dir_index = 0;
static char current_path[MAX_FILENAME * 2] = "/";


// Helper function to build the full path string from the current_dir_index
void fs_build_path() {
    int indices[MAX_FILES];
    int depth = 0;
    int current = current_dir_index;

    // Traverse up to the root (index 0), storing indices
    // The root index (0) will have files[0].parent_index == 0
    while (current != 0 && current < MAX_FILES) {
        indices[depth++] = current;
        current = files[current].parent_index;
    }

    // Handle root case first
    if (current_dir_index == 0) {
        strcpy(current_path, "/");
        return;
    }
    
    // Reverse traverse to build path string
    current_path[0] = '\0'; // Start with an empty string
    
    for (int i = depth - 1; i >= 0; i--) {
        strcat(current_path, "/");
        strcat(current_path, files[indices[i]].name);
    }

    // Ensure it starts with a slash if it's not the root itself (though traversal should handle this)
    if (current_path[0] != '/') {
        char temp[MAX_FILENAME * 2];
        strcpy(temp, current_path);
        strcpy(current_path, "/");
        strcat(current_path, temp);
    }
}
static bool has_scancode(){ return inb(0x64) & 1; } 
// Helper to find an entry *relative to the current directory*
int fs_find_entry_index(const char* name) {
    for (int i = 0; i < file_count; i++) {
        // Check if the name matches AND the entry is a child of the current directory
        if (files[i].parent_index == current_dir_index && strcmp(files[i].name, name) == 0) {
            return i;
        }
    }
    return -1; // Not found
}


void fs_init(){ 
    file_count=1; 
    // Root directory (index 0)
    strcpy(files[0].name,""); // Root dir name is empty string, path is just "/"
    files[0].offset=0; 
    files[0].size=0; 
    files[0].is_dir=1; 
    files[0].parent_index = 0; // Root is its own parent (index 0)
    current_dir_index = 0;
    strcpy(current_path, "/");
}
// Directory entry structure
struct dir_entry {
    char name[256];
    int size;
};
struct dir_entry *directory = (struct dir_entry *)0x1000;
void fs_mkdir(const char* name){ 
    if(fs_find_entry_index(name) != -1) { 
        kprint_col("FS: Directory already exists\n", 0x04); 
        return; 
    }
    if(file_count>=MAX_FILES){ 
        kprint_col("FS: Max files reached\n",0x04); 
        return; 
    } 
    strcpy(files[file_count].name,name); 
    files[file_count].offset=0; 
    files[file_count].size=0; 
    files[file_count].is_dir=1; 
    files[file_count].parent_index = current_dir_index; // Set parent to CWD
    file_count++; 
    kprint_col("Directory created.\n",0x0A); 
}
int fs_delete(const char *filename) {
    for (int i = 0; i < MAX_FILES; i++) {
        // Check if entry exists and matches filename
        if (directory[i].name[0] != 0 && strcmp(directory[i].name, filename) == 0) {
            // Mark file as deleted
            directory[i].name[0] = 0;  
            directory[i].size = 0;
            // Optionally, free blocks: free_blocks(directory[i].start_block, directory[i].size);
            return 1; // success
        }
    }
    return 0; // file not found
}
void fs_rm(const char *filename) {
    if (fs_delete(filename)) {
        kprint_col("File deleted successfully.\n", 0x0A); // green
    } else {
        kprint_col("Error deleting file!\n", 0x04);      // red
    }
}

void fs_touch(const char* name){ 
    if(fs_find_entry_index(name) != -1) { 
        kprint_col("FS: File already exists\n", 0x04); 
        return; 
    }
    if(file_count>=MAX_FILES){ 
        kprint_col("FS: Max files reached\n",0x04); 
        return; 
    } 
    strcpy(files[file_count].name,name); 
    files[file_count].offset=disk_used; 
    files[file_count].size=0; 
    files[file_count].is_dir=0; 
    files[file_count].parent_index = current_dir_index; // Set parent to CWD
    file_count++; 
    kprint_col("File created.\n",0x0A); 
}

void fs_df(){ kprint_col("Disk usage: ",0x0A); kprint_int(disk_used,0x0A); kprint(" / "); kprint_int(DISK_KB,0x0A); kprint(" KB\n"); }

void fs_ls(){ 
    // If not root, show parent link
    if (current_dir_index != 0) {
        kprint_col("..\n", 0x0F); 
    }

    for(int i=0;i<file_count;i++){ 
        // Only list files whose parent is the current directory
        if (files[i].parent_index == current_dir_index) {
             
            if(files[i].is_dir) { 
                kprint_col("/", 0x0E); 
                kprint_col(files[i].name, 0x0E);
            }else
            {
                kprint_col(files[i].name, 0x0F);
            }
            
            kputchar('\n'); 
        }
    } 
}

void fs_write(const char* name,const char* content){ 
    int index = fs_find_entry_index(name);
    
    if(index != -1 && !files[index].is_dir){ 
        uint32_t len=0; 
        while(content[len]) len++; 
        if(disk_used+len>DISK_SIZE){ 
            kprint_col("FS: Disk full\n",0x04); 
            return; 
        } 
        
        // This is a simplified overwrite logic, storing new content at the end of disk
        for(uint32_t j=0;j<len;j++) disk[disk_used+j]=content[j]; 
        files[index].offset=disk_used; 
        files[index].size=len; 
        disk_used+=len; 
        kprint_col("Written to file.\n",0x0A); 
        return;
    }
    kprint_col("FS: File not found or is a directory\n",0x04); 
}

void fs_cat(const char* name){ 
    int index = fs_find_entry_index(name);

    if(index != -1 && !files[index].is_dir){ 
        for(uint32_t j=0;j<files[index].size;j++) kputchar(disk[files[index].offset+j]); 
        kputchar('\n'); 
        return; 
    } 
    kprint_col("FS: File not found or is a directory\n",0x04); 
}
void time(){
    uint8_t h,m,s;
    get_rtc_time(&h, &m, &s);
    char buf[16];
    buf[0] = (h/10)+'0'; buf[1] = (h%10)+'0'; buf[2] = ':';
    buf[3] = (m/10)+'0'; buf[4] = (m%10)+'0'; buf[5] = ':';
    buf[6] = (s/10)+'0'; buf[7] = (s%10)+'0'; buf[8]=0;
    kprint_col(buf, 0X0A);
    kputchar('\n');
}
/**
 * Handles changing the current directory (cd).
 */

void fs_cd(const char* name) {
    // If command is 'cd ..'
    if (strcmp(name, "..") == 0) {
        if (current_dir_index != 0) {
            current_dir_index = files[current_dir_index].parent_index;
            fs_build_path();
            kputchar('\n');
        } else {
            //TROLL
        }
        return;
    }

    // If command is 'cd /'
    if (strcmp(name, "/") == 0) {
        if (current_dir_index != 0) {
            current_dir_index = 0;
            fs_build_path();
            kprint_col("Changed directory to root (/).\n", 0x0A);
        } else {
            kprint_col("Already at root (/).\n", 0x07);
        }
        return;
    }
    
    // Try to find a directory as a child of the current directory
    int new_dir_index = fs_find_entry_index(name);

    if (new_dir_index != -1 && files[new_dir_index].is_dir) {
        current_dir_index = new_dir_index;
        fs_build_path();
        
        kputchar('\n');
    } else {
        kprint_col("FS: Directory not found or is a file: ", 0x04);
        kprint(name);
        kputchar('\n');
    }
}

/**
 * Simulates executing a .crl file by looking for the [EXE] stub
 * and printing its contents. This simulates loading the payload.
 */
void fs_run_crl(const char* name){ 
    int index = fs_find_entry_index(name);

    if(index != -1 && !files[index].is_dir){ 
        
        
        char* content_start = &disk[files[index].offset];
        uint32_t content_size = files[index].size;
        
        // Simplified search for the [EXE] marker
        uint32_t payload_offset = 0;
        const char* exe_marker = "[EXE]";
        uint32_t marker_len = strlen(exe_marker);
        
        bool found_exe = false;
        
        // Loop through the file content to find the "[EXE]" marker
        for(uint32_t i=0; i <= content_size - marker_len; i++) {
            if (content_start[i] == '[' && 
                content_start[i+1] == 'E' && 
                content_start[i+2] == 'X' && 
                content_start[i+3] == 'E' && 
                content_start[i+4] == ']') {
                
                // Found the marker. The payload starts after the marker.
                payload_offset = i + marker_len;
                
                // Advance past spaces/newlines immediately after marker
                while(payload_offset < content_size && 
                      (content_start[payload_offset] == ' ' || content_start[payload_offset] == '\n')) {
                    payload_offset++;
                }
                found_exe = true;
                break;
            }
        }

        if (found_exe) {
            kprint_col(" \n", 0x06); // New messaging
            
            // Print content starting from payload_offset (the executable payload)
            for(uint32_t j=payload_offset; j < content_size; j++) {
                 kputchar(disk[files[index].offset+j]); 
            }
        } else {
            kprint_col(" (CEF format error: [EXE] stub not found. Displaying full file.)\n", 0x04);
            // Fallback: print everything if the format is incorrect
            for(uint32_t j=0;j<files[index].size;j++) kputchar(disk[files[index].offset+j]); 
        }
        
        kputchar('\n'); 
        kprint_col("\n", 0x05);
        return; 
    } 
    kprint_col("EXEC: Executable file not found or is a directory: ", 0x04); 
    kprint(name); 
    kputchar('\n'); 
}


// =======================
// MEMORY INFO
// =======================
void show_memory(multiboot_info_t* mbi){
    if(!(mbi->flags & (1<<6))){ kprint_col("No memory map\n",0x04); return; }
    mmap_entry_t* mmap=(mmap_entry_t*)mbi->mmap_addr;
    uint32_t total=0, free=0;
    while((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length){
        if(mmap->type==1) free += mmap->len/1024;
        total += mmap->len/1024;
        mmap=(mmap_entry_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }
    
    // Convert KB to MB for human-readable output
    double total_h = (double)total / (1024.0*1024.0);
    double free_h = (double)free / (1024.0*1024.0);
    double used_h = (double)(total-free) / (1024.0*1024.0);
    
    kprint_col("Memory:\n",0x0A);
    kprint(" Total: "); kprint_float(total_h ,0x0F); kprint_col(" KB\n", 0x0A); 
    kprint(" Free:  "); kprint_float(free_h,0x0F); kprint_col(" KB\n", 0x0A);
    kprint(" Used:  "); kprint_float(used_h,0x0F); kprint_col(" KB\n", 0x0A);
}
void cerulefetch(multiboot_info_t* mbi){
    if(!(mbi->flags & (1<<6))){ kprint_col("No memory map\n",0x04); return; }
    mmap_entry_t* mmap=(mmap_entry_t*)mbi->mmap_addr;
    uint32_t total=0, free=0;
    while((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length){
        if(mmap->type==1) free += mmap->len/1024;
        total += mmap->len/1024;
        mmap=(mmap_entry_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }
    const char* logo[] = {
        "----; ---; ----;",
        "--; ---;     --;",
        "--; ---;     --;",
        "--; ---;     --;",
        "----; ---; ----;"
    };
    // Convert KB to MB for human-readable output
    double total_h = (double)total / (1024.0*1024.0);
    
    kprint_col("Memory: ",0x03); kprint_float(total_h ,0x0F); kprint_col(" KB\n", 0x0A);
    kprint_col("VERSION: ", 0x0B); kprint("1.01\n");
    kprint("Kernel: Cerulea\n");
    for (int i = 0; i < 5; i++)
    {
        kprint_col(logo[i],0x0B);
        kputchar('\n');
    }
    
}
void mansay(const char* man){ //cowsay part
    if (strlen(man) > 80)
    {
        kprint_col("too long\n", 0x04);
    } else
    {
        kprint_col(man, 0x0E);
        kputchar('\n');
        for (int i = 0; i < strlen(man); i++)
        {
            kprint("-");
        }
        kputchar('\n');
        for (int i = 0; i < 3; i++)
        {
            kprint(" ");
        }
    
        kprint_col("\\/\n", 0x0F);
        kprint_col(" o\n/|\\\n", 0x06);
        kprint_col("/\\ \n", 0x01);
    }
}
// =======================
// SHELL (simplified prompt)
// =======================
void print_prompt() {
    kprint_col("cer ]", 0x03);
    kprint_col(current_path, 0x0F); // Print the current path
    kprint_col(" > ", 0x07);
}

void kscanf(char* buffer, int size) {
    int i = 0;
    char c;
    while (i < size - 1 && (c = kgetchar()) != '\n') {
        buffer[i++] = c;
    }
    buffer[i] = '\0';
}
// Returns 1 if the file exists, 0 if it doesn't
int fs_exists(const char *filename) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].name[0] != 0 && strcmp(directory[i].name, filename) == 0) {
            return 1; // file exists
        }
    }
    return 0; // file not found
}

void prompt(multiboot_info_t* mbi){
    char buf[128]; int idx=0;
    
    print_prompt(); // Initial prompt
    
    while(1){
        // Calculate the current expected prompt length based on path
        int prompt_len = BASE_PROMPT_LEN + strlen(current_path);

        uint8_t sc=kgetchar();

        // Arrow key handling (scancodes: 0x48=up, 0x50=down)
        if(sc==0x48 && history_count>0){ // Up
            if(history_index>0) history_index--; else history_index=history_count-1;
            
            idx=0; while(history[history_index][idx]){ buf[idx]=history[history_index][idx]; idx++; }
            buf[idx]=0;
            
            // Use clear_current_line instead of cls()
            clear_current_line(prompt_len);
            kprint(buf);
            idx = strlen(buf);
            
            continue;
        } else if(sc==0x50 && history_count>0){ // Down
            if(history_index<history_count-1) history_index++; else history_index=0;
            
            idx=0; while(history[history_index][idx]){ buf[idx]=history[history_index][idx]; idx++; }
            buf[idx]=0;
            
            // Use clear_current_line instead of cls()
            clear_current_line(prompt_len);
            kprint(buf);
            idx = strlen(buf);

            continue;
        }

        char c=scancode_to_ascii(sc);
        if(!c) continue;
        while(has_scancode()){
            uint8_t next_sc = inb(0x60);  // Read and remove the next scancode
            scancode_to_ascii(next_sc);   // Process the scancode (updates shift state)
        }
        shift_pressed_left = false;
        shift_pressed_right = false;
        if(c=='\n'){ 
            buf[idx]=0; kputchar('\n');
            if(idx>0){
                if(history_count<MAX_HISTORY){ strcpy(history[history_count++],buf); }
                history_index=history_count;

                char* cmd=buf; char* arg=0; 
                for(int i=0;i<idx;i++){ if(buf[i]==' '){ buf[i]=0; arg=&buf[i+1]; break; } }

                if(strcmp(cmd,"help")==0) { 
                    kprint_col("[Commands:]\n", 0x0D); 
                    kprint("help\nversion | cerulefetch | rm \nerror <arg>\ndir | about | terry\ncls | echo [arg] | open\nsmiley | note\nls | cat [file]\ntouch [filename] | mkdir [dir] \ndf | write [filename] [content]\nmem | cd [dir] \n| name\nmansay [arg] | time\n[executable.crl]\n"); 
                }
                else if(strcmp(cmd,"version")==0) kprint("Cerulea 1.01 \n");
                else if(strcmp(cmd,"about")==0) { kprint("Cerulea - hobby OS kernel\n"); }
                else if(strcmp(cmd,"cls")==0) { cls(); }
                else if(strcmp(cmd,"echo")==0 && arg) { kprint(arg); kputchar('\n'); }
                else if(strcmp(cmd,"smiley")==0) { kputchar_col(2, 0x0D); kprint("\n");}
                else if(strcmp(cmd,"note")==0){ if(arg && arg[0]=='-' && arg[1]=='w') add_note(arg+3); else if(arg && arg[1]=='r') read_notes(); }
                else if(strcmp(cmd,"touch")==0 && arg){ fs_touch(arg); }
                else if(strcmp(cmd,"mkdir")==0 && arg) fs_mkdir(arg);
                else if(strcmp(cmd,"ls")==0) { fs_ls(); }
                else if(strcmp(cmd,"cat")==0 && arg) fs_cat(arg);
                else if(strcmp(cmd,"df")==0) { fs_df(); }
                else if(strcmp(cmd,"mem")==0) { show_memory(mbi); }
                else if(strcmp(cmd,"cd")==0 && arg) { fs_cd(arg); }
                else if(strcmp(cmd,"say")==0 && arg) {
                    for (int i = 0; i < strlen(arg); i++)
                    {
                        kprint("-");
                    }
                    kputchar('\n');
                    kprint(arg);
                    for (int i = 0; i < strlen(arg); i++)
                    {
                        kprint("-");
                    }
                    kputchar('\n');
                    kprint("\\/");
                }
                else if(strcmp(cmd, "write")==0 && arg){
                    char* arg2 = 0;
                    for (int i = 0; arg[i]; i++)
                    {
                        if(arg[i]==' '){
                            arg[i]=0; arg2=&arg[i+1]; break;
                        }
                    }
                    fs_write(arg, arg2);
                } else if (strcmp(cmd, "mansay")==0 && arg)
                {
                    mansay(arg);
                } else if (strcmp(cmd, "time")==0)
                {
                    time();
                } else if (strcmp(cmd, "name") == 0) {
                    kprint("what's your name: ");

                    char name[32]; // buffer for the input
                    int idx = 0;
                    char c;

                    // Read characters until Enter (newline)
                    while (1) {
                        c = kgetchar();
                        c = scancode_to_ascii(c);
                        if (c == '\n') break;      // stop at Enter
                        if (c == '\b' && idx > 0) { // handle backspace
                            idx--;
                            cursor_col--;          // update VGA cursor manually
                            kputchar(' ');        // erase character visually
                            cursor_col--;
                            update_cursor();
                        } else if (idx < 31 && c != 0) {
                            name[idx++] = c;
                            kputchar(c);
                        }
                    }
                    name[idx] = '\0'; // null-terminate the string

                    kputchar('\n');
                    kprint("hello, ");
                    kprint(name);
                    kputchar('\n');
                } else if (strcmp(cmd, "hello")==0)
                {
                    kprint("Hello, world!\n");
                } else if (strcmp(cmd, "terry")==0)
                {
                    for (size_t i = 0; i < strlen("R.I.P Terry.A.Davis 1969-2018"); i++)
                    {
                        kprint("-");
                    } 
                    kputchar('\n');
                    kprint("R.I.P Terry.A.Davis 1969-2018\n");
                    for (size_t i = 0; i < strlen("R.I.P Terry.A.Davis 1969-2018"); i++)
                    {
                        kprint("-");
                    } 
                    kputchar('\n');
                } else if(strcmp(cmd,"dir")==0){
                    kprint_col("[Current dir]: ", 0x0A);
                    kprint(current_path);
                    kputchar('\n');
                } else if (strcmp(cmd, "error")==0 && arg)
                {
                    kprint_col("(!) ERROR: ", 0x04);
                    kprint(arg);
                    kputchar('\n');
                } else if (strcmp(cmd, "cerulefetch")==0){ cerulefetch(mbi); }
                else if (strcmp(cmd, "open")==0)
                {

                    char buf[512]; // buffer for the input
                    int idx = 0;
                    char c;
                    kprint_col("Filename to open: ", 0x06);
                    // Read characters until Enter (newline)
                    while (1) {
                        c = kgetchar();
                        c = scancode_to_ascii(c);
                        if (c == '\n') break;      // stop at Enter
                        if (c == '\b' && idx > 0) { // handle backspace
                            idx--;
                            cursor_col--;          // update VGA cursor manually
                            kputchar(' ');        // erase character visually
                            cursor_col--;
                            update_cursor();
                        } else if (idx < 31 && c != 0) {
                            buf[idx++] = c;
                            kputchar(c);
                        }
                    }
                    buf[idx] = '\0'; // null-terminate the string
                    kputchar('\n');
                    fs_cat(buf);
                } else if (strcmp(cmd, "rm")==0 && arg)
                {
                    fs_rm(arg);
                } else{
                    size_t cmd_len = strlen(cmd);
                    if (cmd_len > 4 && strcmp(cmd + cmd_len - 4, ".crl") == 0) {
                        fs_run_crl(cmd);
                    }else if (cmd_len > 4 && strcmp(cmd + cmd_len - 4, ".txt") == 0) {
                        fs_cat(cmd);
                    } else {
                        kprint_col("Command not found: ", 0x04); kprint(cmd); kputchar('\n'); 
                    }
                }
            }
            idx=0; 
            print_prompt(); // Print new prompt, potentially with new path
        } else if(c == '\b'){ 
            if(idx > 0){
                int prompt_len = BASE_PROMPT_LEN + strlen(current_path);

                // Only delete if cursor is past the prompt
                if(cursor_col > prompt_len || cursor_row > 0){
                    idx--; // remove last char from buffer

                    // Move cursor back visually
                    cursor_col--;
                    if(cursor_col < 0){ // handle line wrap
                        cursor_col = MAX_COLS - 1;
                        cursor_row--;
                    }

                    // Clear the character on screen
                    char *v = (char*)VGAVIDEO;
                    int pos = cursor_row * MAX_COLS + cursor_col;
                    v[pos*2] = ' ';
                    v[pos*2+1] = 0x07;

                    update_cursor();
                }
            }
        }
        else{ 
            if(idx<127){ 
                buf[idx++]=c; 
                kputchar(c); 
            } 
        }
    }
}
void start_sysfiles(){
    fs_mkdir("programs");
    fs_cd("programs");
    fs_touch("hello.crl");
    fs_write("hello.crl", "[EXE] Hello, world!");
    fs_touch("aboutsys.crl");
    fs_write("aboutsys.crl", "[EXE] Cerulea 1.010 - simple hobby OS");
    fs_cd(".."); 
    fs_touch("hello.txt");
    fs_write("hello.txt", "Hello, world");
}
void welcome(){
    for (int i = 0; i < 5; i++)
    {
        kprint_col(logo[i], 0x03);
    }
    kprint_col("WELCOME TO CERULEA", 0x0B);
}
// =======================
// KERNEL ENTRY
// =======================
void kmain(uint32_t magic,uint32_t addr){
    multiboot_info_t* mbi=(multiboot_info_t*)addr;
    
    fs_init();
    start_sysfiles();
    cls();
    welcome();
    kputchar('\n');
    kprint_col("cerulea [(c) 2025 print1488k_official]\n", 0x06);
    prompt(mbi);
}

void _start(void){
    __asm__ volatile("movl %0, %%esp"::"r"(stack+16384));
    uint32_t magic,addr;
    __asm__ volatile("mov %%eax,%0":"=r"(magic));
    __asm__ volatile("mov %%ebx,%0":"=r"(addr));
    kmain(magic,addr);
    while(1);
} 