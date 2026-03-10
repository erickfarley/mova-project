import sys

from PySide6 import QtWidgets

from .ui import MovaGUI


def main():
    app = QtWidgets.QApplication(sys.argv)
    window = MovaGUI()
    window.show()
    sys.exit(app.exec())
