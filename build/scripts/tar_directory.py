import os
import sys
import tarfile


def is_exe(fpath):
    return os.path.isfile(fpath) and os.access(fpath, os.X_OK)


def main(args):
    if len(args) < 2 or len(args) > 3:
        raise Exception("Illegal usage: `tar_directory.py archive.tar directory [skip prefix]` or `tar_directory.py archive.tar output_directory --extract`")
    tar, directory, prefix, extract = args[0], args[1], None, False
    if len(args) == 3:
        if args[2] == '--extract':
            extract = True
        else:
            prefix = args[2]
    for tar_exe in ('/usr/bin/tar', '/bin/tar'):
        if not is_exe(tar_exe):
            continue
        if extract:
            dest = os.path.abspath(directory)
            if not os.path.exists(dest):
                os.makedirs(dest)
            os.execv(tar_exe, [tar_exe, '-xf', tar, '-C', dest])
        else:
            source = os.path.relpath(directory, prefix) if prefix else directory
            os.execv(tar_exe, [tar_exe, '-cf', tar] + (['-C', prefix] if prefix else []) + [source])
        break
    else:
        if extract:
            dest = os.path.abspath(directory)
            if not os.path.exists(dest):
                os.makedirs(dest)
            with tarfile.open(tar, 'r') as tar_file:
                def is_within_directory(directory, target):
                    
                    abs_directory = os.path.abspath(directory)
                    abs_target = os.path.abspath(target)
                
                    prefix = os.path.commonprefix([abs_directory, abs_target])
                    
                    return prefix == abs_directory
                
                def safe_extract(tar, path=".", members=None, *, numeric_owner=False):
                
                    for member in tar.getmembers():
                        member_path = os.path.join(path, member.name)
                        if not is_within_directory(path, member_path):
                            raise Exception("Attempted Path Traversal in Tar File")
                
                    tar.extractall(path, members, numeric_owner=numeric_owner) 
                    
                
                safe_extract(tar_file, dest)
        else:
            with tarfile.open(tar, 'w') as out:
                out.add(os.path.abspath(source), arcname=os.path.relpath(source, prefix) if prefix else None)


if __name__ == '__main__':
    main(sys.argv[1:])
