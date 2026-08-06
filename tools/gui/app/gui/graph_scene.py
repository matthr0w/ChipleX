"""The node-graph scene: builds items from a system.yaml doc and edits it."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from PySide6.QtCore import QLineF, QPointF, Qt, Signal
from PySide6.QtGui import QColor, QPen
from PySide6.QtWidgets import QGraphicsLineItem, QGraphicsScene

from .. import setup_doc
from .graph_items import GRID_SIZE, ChipletNode, ConnectionEdge, PortItem


class GraphScene(QGraphicsScene):
    node_selected = Signal(str)
    connection_selected = Signal(int)
    selection_empty = Signal()
    topology_changed = Signal()
    connection_rejected = Signal(str)

    CONNECT_TOLERANCE = 34.0

    def __init__(self, doc: Dict[str, Any], layout: Optional[Dict[str, List[float]]] = None, parent=None):
        super().__init__(parent)
        self.doc = doc
        self.layout: Dict[str, List[float]] = dict(layout or {})
        self._nodes: Dict[str, ChipletNode] = {}
        self._edges: List[ConnectionEdge] = []
        self._pending: Optional[PortItem] = None
        self._temp_line: Optional[QGraphicsLineItem] = None
        self.selectionChanged.connect(self._on_selection_changed)
        self.rebuild()

    # -- grid -------------------------------------------------------------

    def drawBackground(self, painter, rect) -> None:
        super().drawBackground(painter, rect)
        left = (rect.left() // GRID_SIZE) * GRID_SIZE
        top = (rect.top() // GRID_SIZE) * GRID_SIZE
        lines = []
        x = left
        while x < rect.right():
            lines.append(QLineF(x, rect.top(), x, rect.bottom()))
            x += GRID_SIZE
        y = top
        while y < rect.bottom():
            lines.append(QLineF(rect.left(), y, rect.right(), y))
            y += GRID_SIZE
        painter.setPen(QPen(QColor(128, 128, 128, 40), 0))
        painter.drawLines(lines)

    # -- build ------------------------------------------------------------

    def capture_layout(self) -> Dict[str, List[float]]:
        for name, node in self._nodes.items():
            self.layout[name] = [node.pos().x(), node.pos().y()]
        return dict(self.layout)

    def rebuild(self) -> None:
        self.capture_layout()
        self.clear()
        self._nodes = {}
        self._edges = []

        for index, chiplet in enumerate(setup_doc.chiplets(self.doc)):
            node = ChipletNode(chiplet)
            name = chiplet.get("name", "")
            if name in self.layout:
                node.setPos(self.layout[name][0], self.layout[name][1])
            else:
                node.setPos(40 + (index % 4) * 230.0, 40 + (index // 4) * 220.0)
            self.addItem(node)
            self._nodes[name] = node

        for conn in setup_doc.connections(self.doc):
            eps = conn.get("endpoints", [])
            if len(eps) == 2:
                edge = ConnectionEdge((eps[0], eps[1]))
                self.addItem(edge)
                self._edges.append(edge)
        self.update_edges()

    def update_edges(self) -> None:
        for edge in self._edges:
            edge.adjust(self)

    def port_center(self, endpoint: str) -> Optional[QPointF]:
        chiplet_name, _, ic_name = endpoint.partition(".")
        node = self._nodes.get(chiplet_name)
        if node is None:
            return None
        port = node.port(ic_name)
        if port is None:
            return None
        return port.scenePos()

    # -- drag-to-connect --------------------------------------------------

    def begin_connection(self, port: PortItem) -> None:
        self._pending = port
        start = port.scenePos()
        self._temp_line = QGraphicsLineItem(start.x(), start.y(), start.x(), start.y())
        self._temp_line.setPen(QPen(QColor(90, 170, 250), 2, Qt.DashLine))
        self._temp_line.setZValue(3)
        self.addItem(self._temp_line)

    def mouseMoveEvent(self, event):
        if self._pending is not None and self._temp_line is not None:
            start = self._pending.scenePos()
            pos = event.scenePos()
            self._temp_line.setLine(start.x(), start.y(), pos.x(), pos.y())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self._pending is not None:
            source = self._pending
            self._pending = None
            if self._temp_line is not None:
                self.removeItem(self._temp_line)
                self._temp_line = None
            target = self._port_at(event.scenePos(), exclude=source)
            if target is not None:
                if target.chiplet_name == source.chiplet_name:
                    self.connection_rejected.emit("Cannot connect a chiplet to itself.")
                else:
                    ok, msg = setup_doc.add_connection(self.doc, source.endpoint, target.endpoint)
                    if ok:
                        self.rebuild()
                        self.topology_changed.emit()
                    else:
                        self.connection_rejected.emit(msg)
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def _port_at(self, scene_pos: QPointF, exclude: Optional[PortItem] = None) -> Optional[PortItem]:
        """Return the nearest port within tolerance, so connecting is forgiving."""
        best: Optional[PortItem] = None
        best_dist = self.CONNECT_TOLERANCE
        for node in self._nodes.values():
            for port in node.ports:
                if port is exclude:
                    continue
                delta = port.scenePos() - scene_pos
                dist = (delta.x() ** 2 + delta.y() ** 2) ** 0.5
                if dist <= best_dist:
                    best_dist = dist
                    best = port
        return best

    # -- selection --------------------------------------------------------

    def _on_selection_changed(self) -> None:
        items = self.selectedItems()
        if not items:
            self.selection_empty.emit()
            return
        item = items[0]
        if isinstance(item, ChipletNode):
            self.node_selected.emit(item.name)
        elif isinstance(item, ConnectionEdge):
            index = self._connection_index(item.endpoints)
            if index >= 0:
                self.connection_selected.emit(index)

    def _connection_index(self, endpoints: Tuple[str, str]) -> int:
        pair = set(endpoints)
        for index, conn in enumerate(setup_doc.connections(self.doc)):
            if set(conn.get("endpoints", [])) == pair:
                return index
        return -1

    def select_node(self, name: str) -> None:
        self.clearSelection()
        node = self._nodes.get(name)
        if node is not None:
            node.setSelected(True)
