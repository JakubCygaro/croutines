#ifdef LINUX_X86_68
#include "cco_asm_linux_x86_64.asm"
#elseif WINDOWS_X86_68
#include "cco_asm_win_x86_64.asm"
#else
#error "Unsupported architecture"
#endif
