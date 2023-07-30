#pragma once

#define ASSERT(Expression) if(!(Expression)) {*(int *)0 = 0;}

#define Kilobytes(X) ((X)*1024LL)
#define Megabytes(X) (Kilobytes(X)*1024LL)
#define Gigabytes(X) (Megabytes(X)*1024LL)
#define Terabytes(X) (Gigabytes(X)*1024LL)

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define Pi32 3.14159265359f