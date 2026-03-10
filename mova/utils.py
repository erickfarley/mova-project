from datetime import datetime

import serial.tools.list_ports


def now() -> str:
    return datetime.now().strftime("%H:%M:%S")


def list_ports():
    return [p for p in serial.tools.list_ports.comports()]
