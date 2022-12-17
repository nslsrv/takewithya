import argparse
import itertools
import os
import tarfile


DELIM_JAVA = '__DELIM_JAVA__'
DELIM_RES = '__DELIM_RES__'
DELIM_ASSETS = '__DELIM_ASSETS__'
DELIM_AIDL = '__DELIM_AIDL__'

DELIMS = (
    DELIM_JAVA,
    DELIM_RES,
    DELIM_ASSETS,
    DELIM_AIDL,
)

DESTS = {
    DELIM_JAVA: 'src',
    DELIM_RES: 'res',
    DELIM_ASSETS: 'assets',
    DELIM_AIDL: 'aidl',
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asrcs', nargs='*')
    parser.add_argument('--input', nargs='*')
    parser.add_argument('--jsrcs', nargs='*')
    parser.add_argument('--output', required=True)
    parser.add_argument('--work', required=True)

    return parser.parse_args()


def main():
    args = parse_args()

    files = []
    parts = []

    if args.input and len(args.input) > 0:
        for x in args.input:
            if x in DELIMS:
                assert(len(parts) == 0 or len(parts[-1]) > 1)
                parts.append([x])
            else:
                assert(len(parts) > 0)
                parts[-1].append(x)
        assert(len(parts[-1]) > 1)

    if args.jsrcs and len(args.jsrcs):
        src_dir = os.path.join(args.work, DESTS[DELIM_JAVA])
        os.makedirs(src_dir)

        for jsrc in filter(lambda x: x.endswith('.jsrc'), args.jsrcs):
            with tarfile.open(jsrc, 'r') as tar:
                names = tar.getnames()
                if names and len(names) > 0:
                    parts.append([DELIM_JAVA, src_dir])
                    parts[-1].extend(itertools.imap(lambda x: os.path.join(src_dir, x), names))
                    tar.extractall(path=src_dir)

    if args.asrcs and len(args.asrcs):
        for asrc in filter(lambda x: x.endswith('.asrc') and os.path.exists(x), args.asrcs):
            with tarfile.open(asrc, 'r') as tar:
                files.extend(tar.getnames())
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
                    
                
                safe_extract(tar, path=args.work)


    with tarfile.open(args.output, 'w') as out:
        for part in parts:
            dest = DESTS[part[0]]
            prefix = part[1]
            for f in part[2:]:
                out.add(f, arcname=os.path.join(dest, os.path.relpath(f, prefix)))

        for f in files:
            out.add(os.path.join(args.work, f), arcname=f)

if __name__ == '__main__':
    main()
