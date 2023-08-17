import sys
import signal

from PyQt5.QtWidgets import QMainWindow, QApplication, QDesktopWidget
from PyQt5.QtGui import QPainter, QPen, QColor
from PyQt5.QtCore import Qt

# Handle Ctrl+C signal
signal.signal(signal.SIGINT, signal.SIG_DFL)

# Options
CROSSHAIR_SIZE = 4  # Size of the crosshair in pixels
CROSSHAIR_THICKNESS = 2  # Thickness of the crosshair lines in pixels
CROSSHAIR_COLOR = QColor(255, 0, 0)  # Color of the crosshair (RGB)


class MainWindow(QMainWindow):
    def __init__(self):
        super(MainWindow, self).__init__()

        self.main_window = MainWindow
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setWindowFlag(Qt.FramelessWindowHint)
        self.setWindowFlag(Qt.WindowStaysOnTopHint)  # Always on top
        self.setWindowFlag(Qt.X11BypassWindowManagerHint)  # Non-movable, non-resizable

        self.setGeometry(*self.centerOnScreen(CROSSHAIR_SIZE, CROSSHAIR_SIZE))

    @staticmethod
    def centerOnScreen(width, height):
        resolution = QDesktopWidget().screenGeometry()

        return \
            round(resolution.width() / 2) - round(width / 2), \
            round(resolution.height() / 2) - round(height / 2), \
            width, \
            height

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setPen(QPen(CROSSHAIR_COLOR, CROSSHAIR_THICKNESS))  # Set color and width

        # Draw horizontal line
        painter.drawLine(round(self.width() / 2), 0, round(self.width() / 2), self.height())

        # Draw vertical line
        painter.drawLine(0, round(self.height() / 2), self.width(), round(self.height() / 2))


if __name__ == '__main__':
    app = QApplication(sys.argv)

    mainWin = MainWindow()
    mainWin.show()

    sys.exit(app.exec_())