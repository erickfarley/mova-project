import os
import subprocess
import sys
import threading
from pathlib import Path

from dotenv import load_dotenv
from PySide6 import QtCore, QtWidgets

from ..mapper import fallback_regex, openrouter_map_text_to_command
from ..utils import list_ports, now
from ..workers import HAVE_PYAUDIO, PTTWorker, SerialWorker


class MovaGUI(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        load_dotenv()

        self.setWindowTitle("M.O.V.A. - Movimento Orientado por Voz Ativa")
        self.resize(980, 640)
        self._apply_dark_qss()

        self.serial_thread: SerialWorker | None = None
        self.ptt_thread: PTTWorker | None = None

        self.api_key = os.getenv("OPENROUTER_API_KEY", "")
        self.api_model = os.getenv("OPENROUTER_MODEL", "openrouter/anthropic/claude-3.5-sonnet")
        self.mapper_priority = os.getenv("MAPPER_PRIORITY", "LLM").strip().upper()
        self.llm_fallback_regex = os.getenv("LLM_FALLBACK_REGEX", "1") != "0"
        self._warned_missing_api_key = False

        self._build_ui()
        self._refresh_ports()

    def _build_ui(self):
        tabs = QtWidgets.QTabWidget()
        self.setCentralWidget(tabs)

        mov = QtWidgets.QWidget()
        tabs.addTab(mov, "Movimentos")
        mv = QtWidgets.QVBoxLayout(mov)

        top = QtWidgets.QHBoxLayout()
        self.cmb_ports = QtWidgets.QComboBox()
        self.btn_refresh = QtWidgets.QPushButton("Atualizar Portas")
        self.ed_baud = QtWidgets.QLineEdit("9600")
        self.ed_baud.setFixedWidth(90)
        self.btn_connect = QtWidgets.QPushButton("Conectar")
        self.lbl_conn = QtWidgets.QLabel("Desconectado")

        top.addWidget(QtWidgets.QLabel("Porta:"))
        top.addWidget(self.cmb_ports, 1)
        top.addWidget(self.btn_refresh)
        top.addSpacing(12)
        top.addWidget(QtWidgets.QLabel("Baud:"))
        top.addWidget(self.ed_baud)
        top.addSpacing(12)
        top.addWidget(self.btn_connect)
        top.addSpacing(24)
        top.addWidget(self.lbl_conn)
        top.addStretch(1)
        mv.addLayout(top)

        self.txt_welcome = QtWidgets.QTextEdit()
        self.txt_welcome.setReadOnly(True)
        self.txt_welcome.setFixedHeight(90)
        self.txt_welcome.setText("Clique em **Iniciar M.O.V.A.** para começar.")
        mv.addWidget(self.txt_welcome)

        row = QtWidgets.QHBoxLayout()
        self.btn_start = QtWidgets.QPushButton("Iniciar M.O.V.A.")
        self.btn_home = QtWidgets.QPushButton("Desligar (HOME)")
        self.btn_talk = QtWidgets.QPushButton("Falar (segure)")
        self.btn_talk.setCheckable(True)
        self.btn_talk.setAutoExclusive(False)
        row.addWidget(self.btn_start)
        row.addWidget(self.btn_home)
        row.addStretch(1)
        row.addWidget(self.btn_talk)
        mv.addLayout(row)

        mid = QtWidgets.QHBoxLayout()
        self.ed_heard = QtWidgets.QLineEdit()
        self.ed_heard.setPlaceholderText("O que o M.O.V.A. ouviu...")
        self.ed_mapped = QtWidgets.QLineEdit()
        self.ed_mapped.setPlaceholderText("Comando mapeado...")
        mid.addWidget(QtWidgets.QLabel("Ouvido:"))
        mid.addWidget(self.ed_heard, 2)
        mid.addSpacing(12)
        mid.addWidget(QtWidgets.QLabel("Comando:"))
        mid.addWidget(self.ed_mapped, 2)
        mv.addLayout(mid)

        self.txt_log = QtWidgets.QPlainTextEdit()
        self.txt_log.setReadOnly(True)
        mv.addWidget(self.txt_log, 1)

        cfg = QtWidgets.QWidget()
        tabs.addTab(cfg, "Configuração")
        cgl = QtWidgets.QVBoxLayout(cfg)
        self.btn_install = QtWidgets.QPushButton("Configurar (instalar dependências)")
        self.txt_install = QtWidgets.QPlainTextEdit()
        self.txt_install.setReadOnly(True)
        self.txt_install.setPlaceholderText("Saída do pip install -r requirements.txt...")
        cgl.addWidget(self.btn_install)
        cgl.addWidget(self.txt_install, 1)

        self.btn_refresh.clicked.connect(self._refresh_ports)
        self.btn_connect.clicked.connect(self._toggle_connect)
        self.btn_start.clicked.connect(self._start_mova)
        self.btn_home.clicked.connect(self._send_home)
        self.btn_install.clicked.connect(self._run_install)
        self.btn_talk.pressed.connect(self._ptt_pressed)
        self.btn_talk.released.connect(self._ptt_released)

    def _apply_dark_qss(self):
        self.setStyleSheet(
            """
            QWidget { background: #0f1115; color: #e5e7eb; font-size: 14px; }
            QLineEdit, QTextEdit, QPlainTextEdit, QComboBox {
                background: #161a22; color: #e5e7eb; border: 1px solid #2a2f3a; border-radius: 8px; padding: 6px;
            }
            QPushButton {
                background: #1f2530; color: #f8fafc; border: 1px solid #2a2f3a; border-radius: 10px; padding: 8px 12px;
            }
            QPushButton:hover { background: #2a3240; }
            QPushButton:pressed { background: #18202b; }
            QTabBar::tab { padding: 10px 18px; }
            QLabel { color: #cbd5e1; }
            """
        )

    def _log(self, msg: str):
        self.txt_log.appendPlainText(msg)

    def _refresh_ports(self):
        self.cmb_ports.clear()
        ports = list_ports()
        for port in ports:
            self.cmb_ports.addItem(f"{port.device}  -  {port.description}", port.device)
        if not ports:
            self.cmb_ports.addItem("(nenhuma porta encontrada)", "")

    def _toggle_connect(self):
        if self.serial_thread and self.serial_thread.isRunning():
            self.serial_thread.stop()
            self.serial_thread.wait(1500)
            self.serial_thread = None
            self.lbl_conn.setText("Desconectado")
            self._log(f"[{now()}] Desconectado.")
            self.btn_connect.setText("Conectar")
            return

        port = self.cmb_ports.currentData()
        try:
            baud = int(self.ed_baud.text().strip() or "9600")
        except Exception:
            baud = 9600

        if not port:
            self._log(f"[{now()}] Selecione uma porta válida.")
            return

        self.serial_thread = SerialWorker(port, baud)
        self.serial_thread.rx.connect(self._on_serial_rx)
        self.serial_thread.status.connect(self._log)
        self.serial_thread.start()
        self.lbl_conn.setText(f"Conectado em {port}@{baud}")
        self.btn_connect.setText("Desconectar")

    def _on_serial_rx(self, line: str):
        self._log(f"[{now()}] [ARDUINO] {line}")

    def _send(self, cmd: str):
        if not cmd:
            return
        if not (self.serial_thread and self.serial_thread.isRunning()):
            self._log(f"[{now()}] [WARN] Não conectado.")
            return
        self._log(f"[{now()}] [SEND] {cmd}")
        self.ed_mapped.setText(cmd)
        self.serial_thread.send(cmd)

    def _start_mova(self):
        self.txt_welcome.setHtml(
            "<b>Olá, eu sou o M.O.V.A. (Movimento Orientado por Voz Ativa).</b><br>"
            "O que você deseja? É só falar o comando segurando <b>Falar</b>."
        )
        self._send("HELP")

    def _send_home(self):
        self._send("HOME")

    def _ptt_pressed(self):
        use_pyaudio = HAVE_PYAUDIO and (os.getenv("FORCE_SD", "0") != "1")
        self.ptt_thread = PTTWorker(use_pyaudio=use_pyaudio, rate=16000, max_seconds=6.0)
        self.ptt_thread.heard.connect(self._on_heard_text)
        self.ptt_thread.info.connect(self._log)
        self._log(f"[{now()}] [PTT] pressionado - gravando...")
        self.ptt_thread.start()

    def _ptt_released(self):
        if self.ptt_thread:
            self.ptt_thread.stop_recording()
            self._log(f"[{now()}] [PTT] liberado - reconhecendo...")

    def _on_heard_text(self, text: str):
        self._log(f"[{now()}] [HEARD] {text}")
        self.ed_heard.setText(text)

        cmd = None
        priority = self.mapper_priority if self.mapper_priority in {"LLM", "REGEX"} else "LLM"

        if priority == "LLM":
            cmd = self._map_with_llm(text)
            if not cmd and self.llm_fallback_regex:
                cmd = self._map_with_regex(text)
        else:
            cmd = self._map_with_regex(text)
            if not cmd:
                cmd = self._map_with_llm(text)

        if not cmd:
            cmd = "HELP"
            self._log(f"[{now()}] [MAP] fallback final -> 'HELP'")

        self._send(cmd)

    def _map_with_llm(self, text: str) -> str | None:
        if not self.api_key:
            if self.mapper_priority == "LLM" and not self._warned_missing_api_key:
                self._log(f"[{now()}] [MAP/LLM] OPENROUTER_API_KEY ausente; usando fallback.")
                self._warned_missing_api_key = True
            return None

        cmd = openrouter_map_text_to_command(text, self.api_key, self.api_model)
        if cmd:
            self._log(f"[{now()}] [MAP/LLM] {text!r} -> {cmd!r}")
        return cmd

    def _map_with_regex(self, text: str) -> str | None:
        cmd = fallback_regex(text)
        if cmd:
            self._log(f"[{now()}] [MAP/REGEX] {text!r} -> {cmd!r}")
        return cmd

    @staticmethod
    def _requirements_file() -> Path:
        return Path(__file__).resolve().parents[2] / "requirements.txt"

    def _run_install(self):
        def worker():
            try:
                requirements_file = self._requirements_file()
                self._append_install(
                    f"[{now()}] Executando: pip install -r {requirements_file}\n"
                )
                python = sys.executable
                proc = subprocess.Popen(
                    [python, "-m", "pip", "install", "-r", str(requirements_file)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    encoding="utf-8",
                )
                for line in proc.stdout:
                    self._append_install(line.rstrip())
                proc.wait()
                self._append_install(f"[{now()}] Finalizado (code={proc.returncode}).\n")
            except Exception as exc:
                self._append_install(f"[{now()}] ERRO: {exc}\n")

        threading.Thread(target=worker, daemon=True).start()

    @QtCore.Slot(str)
    def _append_install(self, line: str):
        QtCore.QMetaObject.invokeMethod(
            self.txt_install,
            "appendPlainText",
            QtCore.Qt.QueuedConnection,
            QtCore.Q_ARG(str, line),
        )
