with open('build/build.ninja', 'r') as f:
    text = f.read()

old_str = "build C$:/IfItWorksItWorks/Project/gowassension/build/CMakeFiles/cmake.verify_globs: VERIFY_GLOBS | C$:/IfItWorksItWorks/Project/gowassension/build/CMakeFiles/VerifyGlobs.cmake_force\n  pool = console\n  restat = 1"
new_str = "build C$:/IfItWorksItWorks/Project/gowassension/build/CMakeFiles/cmake.verify_globs: VERIFY_GLOBS | C$:/IfItWorksItWorks/Project/gowassension/build/CMakeFiles/VerifyGlobs.cmake_force\n  restat = 1"

if old_str in text:
    print("Found and replaced pool = console in VerifyGlobs rule")
    text = text.replace(old_str, new_str)
    with open('build/build.ninja', 'w') as f:
        f.write(text)
else:
    print("Pattern not found, checking occurrences of pool = console:")
    for line in text.splitlines():
        if "pool = console" in line:
            print(" ", line)
