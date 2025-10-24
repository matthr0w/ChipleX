import sys
from datetime import datetime


def _timestamp():
    return datetime.now().strftime("%H:%M:%S")

def log_info(msg: str):
    print(f"\033[0m[INFO]\033[0m  | {_timestamp():<16} | {msg}")

def log_warn(msg: str):
    print(f"\033[33m[WARN]\033[0m  | {_timestamp():<16} | {msg}")

def log_error(msg: str):
    print(f"\033[31m[ERROR]\033[0m | {_timestamp():<16} | {msg}")
    sys.exit(1)