import re

path_fp = 'C:/IfItWorksItWorks/Project/gow-port-latest/ps3recomp/libs/video/rsx_fp_decompiler.c'
with open(path_fp, 'rb') as f:
    data = f.read()

pattern = rb'<<<<<<< HEAD\r?\n.*?\r?\n=======\r?\n.*?\r?\n>>>>>>> 7ad17869[^\n]*\r?\n'
rep = b'''            fp_shadow_helper(&p, u, per_unit, &shadow[u]);\r\n    if (gamma_units || strstr(o.p, "_rsx_gamma_input(")) fp_gamma_helper(&p);\r\n'''
data, count = re.subn(pattern, rep, data, flags=re.DOTALL)
print(f"rsx_fp_decompiler.c replacements: {count}")
with open(path_fp, 'wb') as f:
    f.write(data)
