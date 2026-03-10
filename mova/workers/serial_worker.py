import queue
import time

import serial
from PySide6 import QtCore

from ..utils import now


class SerialWorker(QtCore.QThread):
    rx = QtCore.Signal(str)
    status = QtCore.Signal(str)

    def __init__(self, port: str, baud: int, parent=None):
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._ser = None
        self._stop = False
        self.tx_queue: queue.Queue[str] = queue.Queue()

    def run(self):
        try:
            self._ser = serial.Serial(port=self._port, baudrate=self._baud, timeout=0.1)
            time.sleep(2.0)
            self.status.emit(f"[{now()}] [SERIAL] conectado em {self._port} @ {self._baud}")

            try:
                self._ser.reset_input_buffer()
                self._ser.reset_output_buffer()
            except Exception:
                pass

            t0 = time.time()
            while time.time() - t0 < 2.0 and not self._stop:
                line = self._ser.readline().decode(errors="ignore").strip()
                if line:
                    self.rx.emit(line)

            while not self._stop:
                try:
                    cmd = self.tx_queue.get_nowait()
                    if self._ser:
                        self._ser.write((cmd + "\n").encode())
                except queue.Empty:
                    pass

                if self._ser and self._ser.in_waiting:
                    line = self._ser.readline().decode(errors="ignore").strip()
                    if line:
                        self.rx.emit(line)
                else:
                    time.sleep(0.01)

        except Exception as exc:
            self.status.emit(f"[{now()}] [SERIAL] erro: {exc}")
        finally:
            try:
                if self._ser:
                    self._ser.close()
                    self.status.emit(f"[{now()}] [SERIAL] desconectado.")
            except Exception:
                pass

    def send(self, cmd: str):
        self.tx_queue.put(cmd)

    def stop(self):
        self._stop = True
