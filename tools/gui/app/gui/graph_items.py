"""Graphics items for the setup node graph: chiplet nodes, ports, and edges."""

from __future__ import annotations

from typing import Any, Dict, List, Tuple

from PySide6.QtCore import Qt
from PySide6.QtGui import (QBrush, QColor, QFont, QPainterPath,
                           QPainterPathStroker, QPen)
from PySide6.QtWidgets import (QGraphicsEllipseItem, QGraphicsItem,
                               QGraphicsLineItem, QGraphicsRectItem,
                               QGraphicsSimpleTextItem)

NODE_WIDTH = 180.0
HEADER_HEIGHT = 40.0
LINE_HEIGHT = 16.0
PORT_RADIUS = 9.0

_COMPUTE_BRUSH = QColor(60, 90, 140)
_MEMORY_BRUSH = QColor(110, 80, 130)
_PORT_BRUSH = QColor(230, 200, 90)


class PortItem(QGraphicsEllipseItem):
    def __init__(self, chiplet_name: str, ic_name: str, ic_type: str, parent: "ChipletNode"):
        super().__init__(-PORT_RADIUS, -PORT_RADIUS, 2 * PORT_RADIUS, 2 * PORT_RADIUS, parent)
        self.chiplet_name = chiplet_name
        self.ic_name = ic_name
        self.ic_type = ic_type
        self.setBrush(QBrush(_PORT_BRUSH))
        self.setPen(QPen(QColor(40, 40, 40), 1))
        self.setToolTip(f"{chiplet_name}.{ic_name}  [{ic_type}]")
        self.setAcceptHoverEvents(True)
        self.setZValue(2)

    @property
    def endpoint(self) -> str:
        return f"{self.chiplet_name}.{self.ic_name}"

    def mousePressEvent(self, event):
        scene = self.scene()
        if scene is not None and event.button() == Qt.LeftButton:
            scene.begin_connection(self)
            event.accept()
            return
        super().mousePressEvent(event)


class ChipletNode(QGraphicsRectItem):
    def __init__(self, chiplet: Dict[str, Any]):
        super().__init__()
        self.chiplet = chiplet
        self.ports: List[PortItem] = []
        self.setFlags(
            QGraphicsItem.ItemIsMovable
            | QGraphicsItem.ItemIsSelectable
            | QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setZValue(1)
        self._build()

    @property
    def name(self) -> str:
        return self.chiplet.get("name", "")

    def _build(self) -> None:
        for child in list(self.childItems()):
            child.setParentItem(None)
            if self.scene() is not None:
                self.scene().removeItem(child)
        self.ports = []

        accels = self.chiplet.get("accelerators", []) or []
        ics = self.chiplet.get("interconnects", []) or []
        height = HEADER_HEIGHT + LINE_HEIGHT * (len(accels) + 1) + 24
        self.setRect(0, 0, NODE_WIDTH, height)

        is_memory = self.chiplet.get("type") == "memory"
        self.setBrush(QBrush(_MEMORY_BRUSH if is_memory else _COMPUTE_BRUSH))
        self.setPen(QPen(QColor(20, 20, 20), 1.5))

        title = QGraphicsSimpleTextItem(self.name, self)
        title.setBrush(QBrush(QColor(245, 245, 245)))
        font = QFont()
        font.setBold(True)
        title.setFont(font)
        title.setPos(10, 8)

        subtitle = QGraphicsSimpleTextItem(f"[{self.chiplet.get('type', '')}]", self)
        subtitle.setBrush(QBrush(QColor(210, 210, 210)))
        subtitle.setPos(10, 22)

        y = HEADER_HEIGHT
        for accel in accels:
            label = QGraphicsSimpleTextItem(f"accel: {accel.get('name')} [{accel.get('type')}]", self)
            label.setBrush(QBrush(QColor(230, 230, 230)))
            label.setPos(10, y)
            y += LINE_HEIGHT

        count = len(ics)
        for index, ic in enumerate(ics):
            port = PortItem(self.name, ic.get("name", ""), ic.get("type", ""), self)
            spacing = NODE_WIDTH / (count + 1)
            port.setPos(spacing * (index + 1), height)
            label = QGraphicsSimpleTextItem(ic.get("name", ""), self)
            label.setBrush(QBrush(QColor(230, 230, 230)))
            label.setPos(spacing * (index + 1) - 12, height + PORT_RADIUS)
            self.ports.append(port)

    def rebuild(self) -> None:
        self._build()

    def port(self, ic_name: str) -> PortItem | None:
        for port in self.ports:
            if port.ic_name == ic_name:
                return port
        return None

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionHasChanged and self.scene() is not None:
            self.scene().update_edges()
        return super().itemChange(change, value)


class ConnectionEdge(QGraphicsLineItem):
    HIT_WIDTH = 14.0

    def __init__(self, endpoints: Tuple[str, str]):
        super().__init__()
        self.endpoints = endpoints
        self.setFlags(QGraphicsItem.ItemIsSelectable)
        self.setZValue(0)
        self.setPen(QPen(QColor(180, 180, 180), 2))

    def shape(self) -> QPainterPath:
        path = QPainterPath(self.line().p1())
        path.lineTo(self.line().p2())
        stroker = QPainterPathStroker()
        stroker.setWidth(self.HIT_WIDTH)
        return stroker.createStroke(path)

    def boundingRect(self):
        return self.shape().boundingRect()

    def paint(self, painter, option, widget=None):
        pen = QPen(QColor(90, 170, 250), 3) if self.isSelected() else QPen(QColor(170, 170, 170), 2)
        self.setPen(pen)
        super().paint(painter, option, widget)

    def adjust(self, scene: "object") -> None:
        p0 = scene.port_center(self.endpoints[0])
        p1 = scene.port_center(self.endpoints[1])
        if p0 is not None and p1 is not None:
            self.setLine(p0.x(), p0.y(), p1.x(), p1.y())
