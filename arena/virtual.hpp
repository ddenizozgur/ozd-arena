#pragma once

#include "pre.hpp"
#include <assert.h>

inline size_t os_pagesize = 0;

#if IS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

inline SYSTEM_INFO _os_win32_sysinfo_init() {
  SYSTEM_INFO sysinfo = {};
  GetSystemInfo(&sysinfo);
  os_pagesize = sysinfo.dwPageSize;
  return sysinfo;
}
inline SYSTEM_INFO os_win32_sysinfo = _os_win32_sysinfo_init();

#elif IS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

inline size_t _os_linux_pagesize_init() { return sysconf(_SC_PAGESIZE); }
os_pagesize = _os_linux_pagesize_init();

#endif

inline void *os_virtual_reserve(size_t size) {
  void *ptr = nullptr;
#if IS_OS_WINDOWS
  ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
  assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif IS_OS_LINUX
  ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) { // FATAL error
    assert(false && "mmap(): reserve failed");
    return nullptr;
  }
#endif
  return ptr;
}

inline bool os_virtual_commit(void *ptr, size_t size) {
#if IS_OS_WINDOWS
  void *res = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
  if (res == NULL) {
    auto err = GetLastError();
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

inline bool os_virtual_decommit(void *ptr, size_t size) {
#if IS_OS_WINDOWS
  if (!VirtualFree(ptr, size, MEM_DECOMMIT)) {
    assert(false && "VirtualFree(): decommit failed");
    return false;
  }
#elif IS_OS_LINUX
  mprotect(ptr, size, PROT_NONE);
  madvise(ptr, size, MADV_DONTNEED);
#endif
  return true;
}

inline bool os_virtual_release(void *ptr, size_t size) {
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
