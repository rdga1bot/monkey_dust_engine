#include <cstdlib>
#define GLAD_MALLOC(sz)   malloc(sz)
#define GLAD_FREE(ptr)    free(ptr)
#define GLAD_GL_IMPLEMENTATION
#include "glad.h"
