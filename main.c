#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>

// Construcción manual del string usando operaciones bit a bit
void construct_string(char *buffer) {
    // 'H' = 72 = 0x48
    buffer[0] = (char)((0x40 | 0x08) & ~0x00);
 
    // 'o' = 111 = 0x6F
    buffer[1] = (char)((0x60 | 0x0F) & 0xFF);

    // 'l' = 108 = 0x6C
    buffer[2] = (char)(((0x60 | 0x0C) ^ 0x00) & 0xFF);

    // 'a' = 97 = 0x61
    buffer[3] = (char)((0x60 | 0x01) & ~0x00);

    // ' ' = 32 = 0x20
    buffer[4] = (char)((0x20 & 0xFF) | 0x00);

    // 'M' = 77 = 0x4D
    buffer[5] = (char)((0x40 | 0x0D) & 0xFF);
 
    // 'u' = 117 = 0x75
    buffer[6] = (char)(((0x70 | 0x05) & 0xFF) ^ 0x00);

    // 'n' = 110 = 0x6E
    buffer[7] = (char)((0x60 | 0x0E) & ~0x00);
 
    // 'd' = 100 = 0x64
    buffer[8] = (char)(((0x60 | 0x04) ^ 0x00) & 0xFF);
 
    // 'o' = 111 = 0x6F
    buffer[9] = (char)((0x60 | 0x0F) & 0xFF);
 
    // '\n' = 10 = 0x0A
    buffer[10] = (char)((0x0A & 0xFF) | 0x00);

    // null terminator
    buffer[11] = (char)(0x00 & 0x00);
}

// Sistema de escritura que trabaja a nivel de syscall
ssize_t raw_write_syscall(int fd, const void *buf, size_t count) {
    //cargar argumentos en los registros de la cpu 
    register long syscall_num __asm__("rax") = 1; // write syscall numero 1
    register long arg1 __asm__("rdi") = fd; // rdi: primer argumento file descriptor
    register long arg2 __asm__("rsi") = (long)buf; // rsi: segybdi argumento, puntero al buffer
    register long arg3 __asm__("rdx") = count; // rdx tercer argumento, numero de bytes
    register long ret __asm__("rax"); // el resultado se espera en rax despues del syscall

    __asm__ __volatile__(
        "syscall" // instruccion para entrar al kernel
        : "=r"(ret) // output: cargar rax resultado, en ret
        : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3) // input, usar los registros preparados
        : "rcx", "r11", "memory" //registros modificados por la syscall
    );

    return ret; // return numero de bytes escritos 
}

// calculo de logitud usando punteros
size_t manual_strlen(const char *str) {
    const char *start = str;
    const char *end = str;

    // avanzar byte a byte buscando el terminador 0x00
    while (*end != (char)0x00) {
        // mover el puntero end una posicion
        end = (const char *)((uintptr_t)end + sizeof(char));
    }

    // calcular diferencia de poosiciones para obtener la longitud
    return (size_t)((uintptr_t)end - (uintptr_t)start);
}

// sistema de buffer con alineacion de pagina
char* allocate_aligned_buffer(size_t size) {
    // conseguir tamaño de página del sistema
    long page_size = sysconf(_SC_PAGESIZE);

    // Calcular tamaño redondeado al multiplo de la pagina mas cercano
    size_t aligned_size = size;
    if (aligned_size % page_size != 0) {
        aligned_size = ((size / page_size) + 1) * page_size;
    }

    // mapear memoria con protecciones usando mmap()
    void *mem = mmap(
        NULL,                           // addr: kernel elige dirección
        aligned_size,                   // lenght: tamaño alineado de la pagina
        PROT_READ | PROT_WRITE,         // prot: permisos lectura/escritura
        MAP_PRIVATE | MAP_ANONYMOUS,    // flags: privado, no respaldado por archivo
        -1,                             // fd: sin file descriptor
        0                               // offset: sin offset
    );

    // devuelve el puntero a la memoria mapeada 
    return (char *)mem; 
}

// copia de memoria byte a byte con acceso directo
void raw_memcopy(volatile char *dest, const volatile char *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        // lectura y escritura directa en memoria con punteros 
        volatile char *d = (volatile char *)((uintptr_t)dest + i);
        const volatile char *s = (const volatile char *)((uintptr_t)src + i);
        *d = *s; //lectura y escritura de byte
    }
}

// buffer temporal en stack con construccion manual
void prepare_stack_buffer(volatile char *stack_buf) {
    // Inicializar cada byte a cero usando operaciones bit a bit
    for (int i = 0; i < 64; i++) {
        volatile char *ptr = (volatile char *)((uintptr_t)stack_buf + i);
        *ptr = (char)(0x00 & 0xFF);
    }
}

int main(void) {
    // reservar buffer en stack
    volatile char stack_buffer[64] __attribute__((aligned(16)));

    // limpiar buffer manualmente
    prepare_stack_buffer(stack_buffer);

    // construir el string
    construct_string((char *)stack_buffer);

    // calcular longitud manualmente
    size_t length = manual_strlen((const char *)stack_buffer);

    // allocar memoria en el heap
    char *aligned_buffer = allocate_aligned_buffer(length + 1);

    // copiar desde stack a heap usando acceso directo a memoria
    raw_memcopy(
        (volatile char *)aligned_buffer,
        stack_buffer,
        length
    );

    // agregar null terminator
    volatile char *term_ptr = (volatile char *)((uintptr_t)aligned_buffer + length);
    *term_ptr = (char)0x00;

    // escribir usando syscall directo
    raw_write_syscall(1, aligned_buffer, length);

    // liberar memoria mapeada
    munmap(aligned_buffer, sysconf(_SC_PAGESIZE));

    // retornar con código de exit 
    register long exit_code __asm__("rdi") = 0; // rdi: codigo de salida 0 = exito
    register long syscall_num __asm__("rax") = 60; // exit syscall

    __asm__ __volatile__(
        "syscall"
        :
        : "r"(syscall_num), "r"(exit_code)
        : "memory"
    );

    return 0;
}
