with open('src/main.cpp', 'rb') as f:
    content = f.read()

target = b'    const char* elf_path = "vfs/PS3_GAME/USRDIR/EBOOT.ELF";\r\n    if (argc >= 2) {\r\n        elf_path = argv[1];\r\n    }'
if target not in content:
    target = b'    const char* elf_path = "vfs/PS3_GAME/USRDIR/EBOOT.ELF";\n    if (argc >= 2) {\n        elf_path = argv[1];\n    }'

repl = b'    const char* elf_path = "vfs/PS3_GAME/USRDIR/EBOOT.ELF";\r\n    for (int i = 1; i < argc; i++) {\r\n        if (strncmp(argv[i], "--", 2) != 0) {\r\n            elf_path = argv[i];\r\n            break;\r\n        }\r\n    }'

if target in content:
    content = content.replace(target, repl, 1)
    with open('src/main.cpp', 'wb') as f:
        f.write(content)
    print("SUCCESS")
else:
    print("NOT FOUND")
