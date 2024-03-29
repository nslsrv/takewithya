import sys


def is_cmdfile_arg(arg):
    return arg.startswith('@')

def cmdfile_path(arg):
    return arg[1:]

def read_from_command_file(arg):
    with open(arg) as afile:
        return afile.read().splitlines()

def iter_args(args):
    for arg in args:
        if not is_cmdfile_arg(arg):
            if arg == '--ya-start-command-file' or arg == '--ya-end-command-file':
                continue
            yield arg
        else:
            for cmdfile_arg in read_from_command_file(cmdfile_path(arg)):
                yield cmdfile_arg
