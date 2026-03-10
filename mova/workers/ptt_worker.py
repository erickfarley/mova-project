import threading
import time

import speech_recognition as sr
from PySide6 import QtCore

HAVE_PYAUDIO = False
try:
    import pyaudio  # noqa: F401

    HAVE_PYAUDIO = True
except Exception:
    HAVE_PYAUDIO = False

HAVE_SD = False
try:
    import sounddevice as sd  # noqa: F401

    HAVE_SD = True
except Exception:
    HAVE_SD = False


class PTTWorker(QtCore.QThread):
    heard = QtCore.Signal(str)
    info = QtCore.Signal(str)

    def __init__(self, use_pyaudio: bool, rate: int = 16000, max_seconds: float = 6.0, parent=None):
        super().__init__(parent)
        self._use_pyaudio = use_pyaudio and HAVE_PYAUDIO
        self._rate = rate
        self._max_seconds = max_seconds
        self._stop_record = threading.Event()
        self._running = False
        self._recog = sr.Recognizer()

    def stop_recording(self):
        self._stop_record.set()

    def run(self):
        if self._running:
            return

        self._running = True
        self._stop_record.clear()
        try:
            if self._use_pyaudio:
                self._record_pyaudio()
            else:
                self._record_sounddevice()
        finally:
            self._running = False

    def _record_pyaudio(self):
        self.info.emit("[PTT] Gravando (PyAudio). Solte o botão para enviar...")
        import pyaudio

        pa = pyaudio.PyAudio()
        stream = pa.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=self._rate,
            input=True,
            frames_per_buffer=1024,
        )
        frames = []
        duration = 0.0
        try:
            while not self._stop_record.is_set() and duration < self._max_seconds:
                data = stream.read(1024, exception_on_overflow=False)
                frames.append(data)
                duration += 1024 / self._rate
        finally:
            stream.stop_stream()
            stream.close()
            pa.terminate()

        if not frames:
            self.info.emit("[PTT] Sem áudio (soltou muito rápido?)")
            return

        audio_bytes = b"".join(frames)
        audio_data = sr.AudioData(audio_bytes, self._rate, 2)
        try:
            text = self._recog.recognize_google(audio_data, language="pt-BR")
            self.heard.emit(text)
        except sr.UnknownValueError:
            self.info.emit("[STT] Não entendi.")
        except Exception as exc:
            self.info.emit(f"[STT] Erro: {exc}")

    def _record_sounddevice(self):
        if not HAVE_SD:
            self.info.emit("[PTT] sounddevice indisponível.")
            return

        import numpy as np
        import sounddevice as sd

        self.info.emit("[PTT] Gravando (sounddevice). Solte o botão para enviar...")
        frames = []
        block = int(self._rate * 0.05)

        with sd.InputStream(samplerate=self._rate, channels=1, dtype="int16") as stream:
            started = time.time()
            while not self._stop_record.is_set() and (time.time() - started) < self._max_seconds:
                data, _ = stream.read(block)
                frames.append(data.copy())

        if not frames:
            self.info.emit("[PTT] Sem áudio (soltou muito rápido?)")
            return

        audio = np.concatenate(frames, axis=0)
        audio_data = sr.AudioData(audio.tobytes(), self._rate, 2)
        try:
            text = self._recog.recognize_google(audio_data, language="pt-BR")
            self.heard.emit(text)
        except sr.UnknownValueError:
            self.info.emit("[STT] Não entendi.")
        except Exception as exc:
            self.info.emit(f"[STT] Erro: {exc}")
