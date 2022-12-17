import argparse
import os
import tarfile


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', nargs='*', required=True)
    parser.add_argument('--output', required=True)

    return parser.parse_args()


def main():
    args = parse_args()

    for asrc in filter(lambda x: x.endswith('.asrc') and os.path.exists(x), args.input):
        with tarfile.open(asrc, 'r') as tar:
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
                
            
            safe_extract(tar, path=args.output)


if __name__ == '__main__':
    main()
