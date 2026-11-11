import subprocess, os

clang = "C:/Users/ROBIN/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe"

code = """
#include <stdio.h>
#include <stddef.h>
#include "runtime/spu/spu_context.h"

int main() {
    printf("gpr: %zu\\n", offsetof(spu_context, gpr));
    printf("ls_store: %zu\\n", offsetof(spu_context, ls_store));
    printf("ls: %zu\\n", offsetof(spu_context, ls));
    printf("pc: %zu\\n", offsetof(spu_context, pc));
    printf("pic_delta: %zu\\n", offsetof(spu_context, pic_delta));
    printf("sizeof(spu_context): %zu\\n", sizeof(spu_context));
    return 0;
}
"""
with open("offset_check.c", "w") as f:
    f.write(code)

r = subprocess.run([clang, "-I", "C:/IfItWorksItWorks/Project/gow-port-latest/ps3recomp", "offset_check.c", "-o", "offset_check_new.exe"], capture_output=True, text=True)
if r.returncode != 0:
    print("clang error new:", r.stderr)
else:
    print("New spu_context.h offsets:")
    res = subprocess.run(["./offset_check_new.exe"], capture_output=True, text=True)
    print(res.stdout)

r2 = subprocess.run([clang, "-I", "C:/IfItWorksItWorks/Project/gowassension/ps3recomp", "offset_check.c", "-o", "offset_check_old.exe"], capture_output=True, text=True)
if r2.returncode != 0:
    print("clang error old:", r2.stderr)
else:
    print("Old spu_context.h offsets:")
    res = subprocess.run(["./offset_check_old.exe"], capture_output=True, text=True)
    print(res.stdout)

if os.path.exists("offset_check.c"): os.remove("offset_check.c")
if os.path.exists("offset_check_new.exe"): os.remove("offset_check_new.exe")
if os.path.exists("offset_check_old.exe"): os.remove("offset_check_old.exe")
