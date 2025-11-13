#!/usr/bin/env python3
"""
Name: crosshair.py
Author: Nikita Neverov (BMTLab)
Version: 1.2.0
Date: 2025-11-13

Description
-----------
Tiny PyQt5 overlay that draws a simple crosshair in the center of the screen.

Features
--------
- Borderless, always-on-top window.
- Transparent background, only the crosshair is visible.
- Window is positioned at the exact screen center.
- Clean shutdown on Ctrl+C (SIGINT).

Usage
-----
Run the script directly:

    python3 crosshair.py
"""

from __future__ import annotations

import signal
import sys
from collections.abc import Sequence
from typing import cast

from PyQt5.QtCore import QRect, Qt
from PyQt5.QtGui import QPainter, QPen, QColor, QPaintEvent, QScreen
from PyQt5.QtWidgets import QApplication, QMainWindow

# Handle Ctrl+C (SIGINT) in a terminal-friendly way
signal.signal(signal.SIGINT, signal.SIG_DFL)

# Crosshair configuration
CROSSHAIR_SIZE: int = 6  #: Window size (square), in pixels
CROSSHAIR_THICKNESS: int = 2  #: Crosshair line thickness, in pixels
CROSSHAIR_COLOR: QColor = QColor(255, 0, 0)  #: Crosshair color (RGB)


class CrosshairWindow(QMainWindow):
    """Frameless, always-on-top window that draws a crosshair at its center.

    The window:

    - Has a transparent background (only the crosshair is visible).
    - Is borderless and bypasses the window manager (non-resizable, non-movable).
    - Is positioned in the center of the primary screen.

    Notes
    -----
    This class is intentionally minimal and tailored for use as a small overlay/helper tool.
    It does not manage multiple screens or dynamic re-layout on screen changes.
    """

    # noinspection PyUnresolvedReferences
    def __init__(self) -> None:
        """Initialize a new crosshair window.

        The constructor:

        - Configures window attributes (transparency, frameless, always-on-top).
        - Pre-creates a :class:`QPen` instance for drawing the crosshair.
        - Centers the window on the primary screen using :data:`CROSSHAIR_SIZE`.
        """
        super().__init__()

        # Make background transparent and window frameless, on top, and WM-bypassing.
        self.setAttribute(
            cast(Qt.WidgetAttribute, Qt.WA_TranslucentBackground),
        )
        self.setWindowFlag(
            cast(Qt.WindowType, Qt.FramelessWindowHint),
        )
        self.setWindowFlag(
            cast(Qt.WindowType, Qt.WindowStaysOnTopHint),
        )
        self.setWindowFlag(
            cast(Qt.WindowType, Qt.X11BypassWindowManagerHint),
        )

        # Pre-create pen to avoid re-allocating it on every paintEvent.
        self._pen: QPen = QPen(CROSSHAIR_COLOR, CROSSHAIR_THICKNESS)

        # Position window at the center of the primary screen.
        self._center_on_primary_screen(CROSSHAIR_SIZE, CROSSHAIR_SIZE)

    def _center_on_primary_screen(self, width: int, height: int) -> None:
        """Center the window on the primary screen and fix its size.

        Parameters
        ----------
        width : int
            Desired window width in pixels.
        height : int
            Desired window height in pixels.

        Notes
        -----
        If the primary screen cannot be obtained
        (for example, in unusual headless setups),
        the method falls back to simply setting a fixed size
        and lets the window manager decide the position.
        """
        screen: QScreen = QApplication.primaryScreen()
        if screen is None:
            # Fallback: set size only; let the WM choose position
            self.setFixedSize(width, height)
            return

        geometry: QRect = screen.geometry()
        x: int = geometry.x() + (geometry.width() - width) // 2
        y: int = geometry.y() + (geometry.height() - height) // 2

        self.setGeometry(x, y, width, height)
        # This overlay is not meant to be resized; lock the size
        self.setFixedSize(width, height)

    def paintEvent(self, event: QPaintEvent) -> None:
        """Draw a crosshair centered inside the window.

        Parameters
        ----------
        event : QPaintEvent
            Paint event provided by the Qt event loop.

        Notes
        -----
        The crosshair consists of two straight lines:

        - One vertical line crossing the full height of the window.
        - One horizontal line crossing the full width of the window.

        Both lines intersect exactly at the window center.
        """
        # Cache width/height locally to avoid repeated virtual calls
        width: int = self.width()
        height: int = self.height()
        half_w: int = width // 2
        half_h: int = height // 2

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, False)
        painter.setPen(self._pen)

        painter.drawLine(half_w, 0, half_w, height)  # Vertical line
        painter.drawLine(0, half_h, width, half_h)  # Horizontal line


# noinspection PyUnresolvedReferences
def main(argv: Sequence[str] | None = None) -> int:
    """Application entry point.

    Parameters
    ----------
    argv : Sequence[str] or None, optional
        Command-line arguments excluding the program name.
        If ``None``, :data:`sys.argv[1:]` is used.
        These arguments are currently not interpreted by the app,
        but they are preserved for future extensibility
        and for consistency with other CLI tools.

    Returns
    -------
    int
        Exit status code. ``0`` on normal exit;
        any non-zero value indicates an abnormal termination from the Qt event loop.
    """
    if argv is None:
        argv = sys.argv[1:]

    # Qt expects argv with program name in argv[0]; keep passed args for future use
    qt_argv: list[str] = [sys.argv[0], *argv]

    # High DPI flags improve appearance on modern displays
    QApplication.setAttribute(
        cast(Qt.ApplicationAttribute, Qt.AA_EnableHighDpiScaling),
    )
    QApplication.setAttribute(
        cast(Qt.ApplicationAttribute, Qt.AA_UseHighDpiPixmaps),
    )

    app = QApplication(qt_argv)
    window = CrosshairWindow()
    window.show()

    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
