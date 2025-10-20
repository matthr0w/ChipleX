import sys


def log_info(module: str, msg: str):
    print(f"\033[0m[INFO]\033[0m  | {module}: {msg}")

def log_warn(module: str, msg: str):
    print(f"\033[33m[WARN]\033[0m  | {module}: {msg}")

def log_error(module: str, msg: str):
    print(f"\033[31m[ERROR]\033[0m | {module}: {msg}")
    sys.exit(1)