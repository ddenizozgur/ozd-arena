#pragma once

#include "core.h"

static size_t os_pagesize = 0;

#if IS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static SYSTEM_INFO os_win32_sysinfo = {0};

before_main(os_win32_sysinfo_init) {
  GetSystemInfo(&os_win32_sysinfo);
  os_pagesize = os_win32_sysinfo.dwPageSize;
}

#elif IS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

before_main(os_linux_pagesize_init) { os_pagesize = sysconf(_SC_PAGESIZE); }

#endif

static inline void *os_virtual_reserve(size_t size) {
  void *ptr = NULL;
#if IS_OS_WINDOWS
  ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
  assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif IS_OS_LINUX
  ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) { // FATAL error
    assert(false && "mmap(): reserve failed");
    return NULL;
  }
#endif
  return ptr;
}

static inline bool os_virtual_commit(void *ptr, size_t size) {
#if IS_OS_WINDOWS
  void *res = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
  if (res == NULL) {
    DWORD err = GetLastError();
    switch (err) {
    case 0:
      assert(false && "VirtualAlloc(): commit failed. invalid argument");
      return false;
    default:
      assert(false && "VirtualAlloc(): commit failed");
      return false;
    }
  }
#elif IS_OS_LINUX
  // manually align to page boundary for linux
  int res = mprotect(ptr, size, PROT_READ | PROT_WRITE);
  if (res == -1) {
    assert(false && "mprotect(): commit failed");
    return false;
  }
#endif
  return true;
}

// static inline bool os_virtual_decommit(void *ptr, size_t size) {
// #if IS_OS_WINDOWS
//   if (!VirtualFree(ptr, size, MEM_DECOMMIT)) {
//     assert(false && "VirtualFree(): decommit failed");
//     return false;
//   }
// #elif IS_OS_LINUX
//   mprotect(ptr, size, PROT_NONE);
//   madvise(ptr, size, MADV_DONTNEED);
// #endif
//   return true;
// }

static inline bool os_virtual_release(void *ptr, size_t size) {
#if IS_OS_WINDOWS
  (void)size;
  if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
    assert(false && "VirtualFree(): release failed");
    return false;
  }
#elif IS_OS_LINUX
  munmap(ptr, size);
#endif
  return true;
}
