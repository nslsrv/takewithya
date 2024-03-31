import os
import subprocess
import sys


def just_do_it(argv):
    delim = argv[0]
    args = []
    for item in argv:
        if item == delim:
            args.append([])
        else:
            args[-1].append(item)
    dll_cmd, java_cmd, inputs, dll_out, java_out, jsrs_out, build_root = args
    dll_out, java_out, jsrs_out, build_root = dll_out[0], java_out[0], jsrs_out[0], build_root[0]
    for inp in inputs:
        if os.path.isabs(inp):
            inp = os.path.relpath(inp, build_root)
        ext = os.path.splitext(inp)[1]
        if ext in ('.o', '.obj'):
            if os.path.join(build_root, inp) in java_cmd:
                inp = os.path.join(build_root, inp)
            java_cmd.remove(inp)
        if ext in ('.java', '.jsrc'):
            if os.path.join(build_root, inp) in dll_cmd:
                inp = os.path.join(build_root, inp)
            dll_cmd.remove(inp)
    java_cmd.insert(java_cmd.index(dll_out), java_out)
    java_cmd.remove(dll_out)
    subprocess.check_call(java_cmd)
    subprocess.check_call(dll_cmd)


if __name__ == '__main__':
    just_do_it(sys.argv[1:])
