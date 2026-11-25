/*

MIT License

Copyright (c) 2020 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cueparser/fileabstract.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CueFile* create_posix_file(struct CueFile*, const char* filename, uint8_t mode);
struct CueFile* create_posix_file_lazy(struct CueFile*, const char* filename, uint64_t cachedSize);
bool posix_file_ensure_open(struct CueFile* file);
uint64_t posix_file_get_size(const struct CueFile* file);
void posix_file_ensure_fastseek(struct CueFile* file);

#ifdef __cplusplus
}
#endif
