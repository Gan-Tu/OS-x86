#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

/* Function pointers to hw3 functions */
void* (*mm_malloc)(size_t);
void* (*mm_realloc)(void*, size_t);
void (*mm_free)(void*);

void load_alloc_functions() {
    void *handle = dlopen("hw3lib.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    char* error;
    mm_malloc = dlsym(handle, "mm_malloc");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    mm_realloc = dlsym(handle, "mm_realloc");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    mm_free = dlsym(handle, "mm_free");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }
}

int main() {
    load_alloc_functions();

    int *data = (int*) mm_malloc(sizeof(int));
    assert(data != NULL);
    data[0] = 0x162;
    assert(data[0] == 0x162);
    mm_free(data);

    int *data2 = (int*) mm_malloc(sizeof(int));
    assert(data2 != NULL);
    assert(data == data2);
    assert(data2[0] == 0);
    data2[0] = 0x162;
    assert(data2[0] == 0x162);

    data2 = (int*) mm_realloc(data2, sizeof(int) * 2);
    assert(data2 != NULL);
    assert(data2[0] == 0x162);
    assert(data2[1] == 0);
    mm_free(data2);

    data = (int*) mm_malloc(sizeof(int));
    data2 = (int*) mm_malloc(sizeof(int));
    int *data3 = (int*) mm_realloc(data, sizeof(int) * 2);
    assert(data != data3);
    assert(data[0] == data3[0]);
    assert(data3[1] == 0);
    int *data4 = (int*) mm_malloc(sizeof(int));
    assert(data == data4);

    data = (int*) mm_malloc(sizeof(int));
    data2 = (int*) mm_malloc(sizeof(int));
    data2[0] = 0x162;
    data3 = (int*) mm_realloc(data, sizeof(int) * 2);
    data3[0] = 0x111;
    data3[0] = 0x122;
    mm_free(data);
    data4 = (int*) mm_realloc(data3, sizeof(int));
    assert(data2[0] = 0x162);
    assert(data4[0] = 0x111);
 

    printf("malloc test successful!\n");
    return 0;
}
