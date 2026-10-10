#!/usr/bin/env python3
"""Qt desktop port retaining the classic LCB workbench layout and C core."""
import argparse
import copy
import ctypes as C
import json
import os
import shlex
import shutil
from pathlib import Path
import sys
import threading
import time

from PySide6.QtCore import Qt, QThread, Signal, QTimer, QUrl
from PySide6.QtGui import QAction, QKeySequence, QIcon, QTextCursor, QDesktopServices, QFont, QPalette, QColor, QTextCharFormat, QTextBlockFormat
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QPlainTextEdit, QTextBrowser, QTreeWidget, QTreeWidgetItem,
    QComboBox, QSpinBox, QDoubleSpinBox, QTabWidget, QTabBar, QSplitter, QTableWidget,
    QTableWidgetItem, QHeaderView, QFileDialog, QMessageBox, QDialog, QDialogButtonBox,
    QFormLayout, QCheckBox, QMenu, QProgressBar, QAbstractItemView, QListWidget, QListWidgetItem, QSizePolicy)
from backend import (Core, Store, Settings, Engine, Transport, Cancelled, Generation,
    FIELDS, LABELS, MAX_PROMPT, truncate, text_ok, scan_models, EngineHTTPError,
    MEDIA_TYPES, model_role, suggested_projector)


class Job(QThread):
    result = Signal(object)
    progress = Signal(str)
    budget = Signal(object)
    telemetry = Signal(object)
    phase = Signal(str)

    def __init__(self, fn, parent=None):
        super().__init__(parent)
        self.fn = fn

    def run(self):
        try:
            self.result.emit((True, self.fn(self)))
        except Exception as exc:
            self.result.emit((False, str(exc)))


def column(*widgets):
    w = QWidget()
    layout = QVBoxLayout(w)
    layout.setContentsMargins(4, 4, 4, 4)
    layout.setSpacing(4)
    for widget in widgets:
        layout.addWidget(widget)
    return w


def row(*widgets):
    w = QWidget()
    layout = QHBoxLayout(w)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(4)
    for widget in widgets:
        layout.addWidget(widget)
    return w


def heading(text):
    w = QLabel(text)
    w.setObjectName('heading')
    return w


def button(text, fn):
    b = QPushButton(text)
    b.clicked.connect(fn)
    return b


class SizeItem(QTableWidgetItem):
    def __lt__(self, other):
        return self.data(Qt.UserRole) < other.data(Qt.UserRole)


class Window(QMainWindow):
    def __init__(self, root=None):
        super().__init__()
        self.core, self.engine = Core(), Engine()
        self.store = Store(root)
        self.config = Settings(self.store.root, self.core)
        self.chat = None
        self.workspace_id = next(iter(self.store.workspaces))
        self.model_info = None
        self.busy = self.restoring = self.scanning = self.probing = False
        self.jobs = set()
        self.transport = None
        self.scan_cancel = threading.Event()
        self.health = 'Engine offline'
        self.phase_text = 'Select a model, then Load. Your chat is ready.'
        self.phase_started = time.monotonic()
        self.last_stream = {'content': '', 'reasoning_content': ''}
        self.metadata_epoch = 0
        self.settings_loading = False
        self.starting_engine = False
        self.importing_media = False
        self.attachments = []
        self.projector_path = ''
        self.modalities = {}
        self.engine_path = self.config.get('engine', 'executable')
        self.model_path = self.config.get('engine', 'model')
        self.g = self.core.defaults()
        self.setWindowTitle('lcb-ai')
        self.setWindowIcon(QIcon(str(Path(__file__).parents[2] / 'assets/lcb-icon.svg')))
        self.resize(1240, 820)
        self.setMinimumSize(1000, 680)
        self.build_ui()
        self.build_menus()
        self.save_timer = QTimer(self)
        self.save_timer.setSingleShot(True)
        self.save_timer.setInterval(700)
        self.save_timer.timeout.connect(self.save_draft)
        self.prompt.textChanged.connect(self.draft_changed)
        self.refresh_tree()
        self.load_settings()
        if self.model_path:
            QTimer.singleShot(0, lambda: self.read_model(self.model_path, initial=True) if self.metadata_epoch == 0 else None)
        try:
            saved = json.loads(self.config.get('linux-session', 'tabs', '[]'))
            for identifier in saved[:32]:
                if identifier in self.store.chats:
                    self.open_chat(identifier)
            active = self.config.get('linux-session', 'active')
            if active in saved and active in self.store.chats:
                self.open_chat(active)
            sizes = json.loads(self.config.get('linux-layout', 'sizes', '[210, 730, 280]'))
            self.horizontal.setSizes(sizes)
            self.vertical.setSizes(json.loads(self.config.get('linux-layout', 'vertical', '[640, 128]')))
            for key, widget in (('left', self.left), ('right', self.right), ('output', self.output)):
                widget.setVisible(self.config.get('linux-layout', key, '1') == '1')
        except (ValueError, TypeError):
            self.reset_layout()
        if not self.chat:
            self.new_chat()
        self.refresh_controls()
        self.note('Chat ready. Load a model and type, or press + for a new chat. Workspaces are optional.')
        if self.store.skipped:
            self.note(f'{len(self.store.skipped)} invalid saved files were left unchanged.')
        self.poll_timer = QTimer(self)
        self.poll_timer.setInterval(2000)
        self.poll_timer.timeout.connect(self.poll)
        self.poll_timer.start()
        self.activity_timer = QTimer(self)
        self.activity_timer.setInterval(500)
        self.activity_timer.timeout.connect(self.refresh_activity)
        self.activity_timer.start()
        QTimer.singleShot(0, self.poll)
        if self.folder.text():
            QTimer.singleShot(0, self.scan)

    def build_ui(self):
        self.new_button = button('+ New chat', self.new_chat)
        self.new_button.setToolTip('Start a chat immediately. A default workspace is created automatically.')
        self.load_button = button('Load model', self.load_model)
        self.unload_button = button('Unload', self.unload)
        self.engine_button = button('Engine…', self.choose_engine)
        self.model_button = button('Model…', self.choose_model)
        self.model_text = QLineEdit(Path(self.model_path).name if self.model_path else '')
        self.model_text.setReadOnly(True)
        self.model_text.setPlaceholderText('Select a GGUF model')
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        try:
            self.port.setValue(int(self.config.get('engine', 'port', '8080')))
        except ValueError:
            self.port.setValue(8080)
        self.port.valueChanged.connect(self.port_changed)
        toolbar = row(self.new_button, self.load_button, self.unload_button, self.engine_button,
                      self.model_button, self.model_text, self.port, QLabel('lcb-ai'))
        toolbar.layout().setStretch(5, 1)
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.itemClicked.connect(self.tree_selected)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self.chat_menu)
        self.workspace_new = button('Workspace…', lambda: self.edit_workspace(True))
        self.workspace_edit = button('Edit…', self.edit_workspace)
        self.left = column(heading('Chats and workspaces'), row(self.workspace_new, self.workspace_edit),
                           self.tree, QLabel('Saved locally'))
        self.left.layout().setStretch(2, 1)
        self.tabs = QTabBar()
        self.tabs.setExpanding(False)
        self.tabs.setTabsClosable(True)
        self.tabs.currentChanged.connect(self.tab_changed)
        self.tabs.tabCloseRequested.connect(self.close_tab)
        self.exchange = QComboBox()
        self.exchange.currentIndexChanged.connect(self.scroll_exchange)
        self.retry_button = button('Retry', lambda: self.retry(False))
        self.resend_button = button('Edit and resend…', lambda: self.retry(True))
        exchange_row = row(self.exchange, self.retry_button, self.resend_button)
        exchange_row.layout().setStretch(0, 1)
        self.transcript = QTextBrowser()
        self.transcript.setObjectName('chatTranscript')
        self.transcript.document().setDocumentMargin(14)
        self.transcript.setOpenLinks(False)
        self.transcript.anchorClicked.connect(self.open_link)
        self.prompt = QPlainTextEdit()
        self.prompt.setPlaceholderText('Write a message…    Ctrl+Enter to send')
        self.prompt.setMinimumHeight(76)
        self.prompt.setMaximumHeight(90)
        self.send_button = button('Send', self.send)
        self.stop_button = button('Stop', self.stop)
        self.attach_button = button('Attach…', self.choose_attachments)
        self.remove_attachment_button = button('Remove', self.remove_attachment)
        self.attachment_list = QListWidget()
        self.attachment_list.setMaximumHeight(64)
        self.attachment_list.itemDoubleClicked.connect(self.preview_attachment)
        self.attachment_list.hide()
        self.media_status = QLabel('Text · load a model to check media support')
        self.media_status.setWordWrap(True)
        attachment_row = row(self.attach_button, self.remove_attachment_button, self.media_status)
        attachment_row.layout().setStretch(2, 1)
        attachment_row.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Maximum)
        self.attach_button.setFixedWidth(90)
        self.remove_attachment_button.setFixedWidth(80)
        buttons = column(self.send_button, self.stop_button)
        self.phase_label = QLabel(self.phase_text)
        self.phase_label.setWordWrap(True)
        self.phase_label.setObjectName('chatPhase')
        self.thinking_text = QPlainTextEdit()
        self.thinking_text.setReadOnly(True)
        self.thinking_text.setPlaceholderText('The model’s emitted thinking appears here. No trace has been received.')
        self.center = column(heading('Conversations'), self.tabs, exchange_row,
                             self.phase_label, self.transcript,
                             self.attachment_list, attachment_row,
                             row(self.prompt, buttons))
        self.center.layout().setStretch(4, 1)
        self.right_tabs = QTabWidget()
        self.models = QTableWidget(0, 2)
        self.models.setHorizontalHeaderLabels(['Model and support', 'Size'])
        self.models.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.models.setSelectionMode(QAbstractItemView.SingleSelection)
        self.models.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.models.verticalHeader().hide()
        self.models.verticalHeader().setDefaultSectionSize(42)
        self.models.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.models.setColumnWidth(1, 75)
        self.models.setSortingEnabled(True)
        self.models.itemSelectionChanged.connect(self.inspect_model)
        self.models.itemActivated.connect(self.use_selected_model)
        self.details = QTableWidget(0, 2)
        self.details.setHorizontalHeaderLabels(['Property', 'Value'])
        self.details.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.details.verticalHeader().hide()
        self.details.verticalHeader().setDefaultSectionSize(22)
        self.details.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.details.setWordWrap(False)
        self.projector_text = QLineEdit()
        self.projector_text.setReadOnly(True)
        self.projector_text.setPlaceholderText('No projector · text only')
        self.projector_button = button('Choose…', self.choose_projector)
        self.projector_clear = button('Text only', lambda: self.set_projector(''))
        self.use_model_button = button('Use selected model', self.use_selected_model)
        model_page = column(self.models, self.use_model_button, heading('Model properties'), self.details,
                            QLabel('Selected model’s media projector (reload to apply)'), self.projector_text,
                            row(self.projector_button, self.projector_clear))
        model_page.layout().setStretch(0, 3)
        model_page.layout().setStretch(3, 2)
        self.right_tabs.addTab(model_page, 'Models')
        self.generation_widgets = []
        gen_page = QWidget()
        form = QFormLayout(gen_page)
        ranges = [(1, 16384), (0, 2), (.000001, 1), (512, 1048576), (0, 2),
                  (1, 2), (0, 4), (0, 1000), (0, 1), (-2, 2)]
        for i, (label, limits) in enumerate(zip(LABELS, ranges)):
            if i == 4:
                editor = QComboBox()
                editor.addItems(['Auto', 'Off', 'On'])
                editor.currentIndexChanged.connect(lambda value, field=i: self.edit_generation(field, value))
            else:
                editor = QSpinBox() if i in (0, 3, 7) else QDoubleSpinBox()
                if isinstance(editor, QDoubleSpinBox):
                    editor.setDecimals(6)
                    editor.setSingleStep(.05)
                editor.setRange(*limits)
                editor.setKeyboardTracking(False)
                editor.valueChanged.connect(lambda value, field=i: self.edit_generation(field, value))
            editor.setObjectName(FIELDS[i])
            if i == 0:
                editor.setToolTip('Maximum generated tokens, including both thinking and the final answer.')
            self.generation_widgets.append(editor)
            form.addRow(label, editor)
        helptext = QLabel('Saved per model. Reload after changing context.\n\nThinking: Auto uses the template default. Traces appear only when the model emits them.\nRepeat: 1 = off. DRY, Top K, Min P, Presence: 0 = off.')
        helptext.setWordWrap(True)
        form.addRow(helptext)
        self.right_tabs.addTab(gen_page, 'Generation')
        self.folder = QLineEdit(self.config.get('library', 'folder'))
        self.recursive = QCheckBox('Include subfolders')
        self.recursive.setChecked(self.config.get('library', 'recursive', '1') == '1')
        self.scan_button = button('Scan', self.scan)
        self.folder_button = button('Folder…', self.choose_folder)
        self.master = QPlainTextEdit()
        self.master.setReadOnly(True)
        library_page = column(QLabel('Model folder'), self.folder, row(self.folder_button, self.scan_button),
                              self.recursive, heading('Workspace master prompt'), self.master)
        library_page.layout().setStretch(5, 1)
        self.right_tabs.addTab(library_page, 'Library')
        self.right = column(heading('Properties and Models'), self.right_tabs)
        self.horizontal = QSplitter(Qt.Horizontal)
        for w in (self.left, self.center, self.right):
            self.horizontal.addWidget(w)
        self.horizontal.setStretchFactor(1, 1)
        self.activity = QPlainTextEdit()
        self.activity.setReadOnly(True)
        self.activity.setMaximumBlockCount(500)
        self.activity.setFont(QFont('monospace', 9))
        self.engine_log = QPlainTextEdit()
        self.engine_log.setReadOnly(True)
        self.engine_log.setMaximumBlockCount(2000)
        self.engine_log.setFont(QFont('monospace', 9))
        self.output_tabs = QTabWidget()
        self.output_tabs.addTab(self.activity, 'Session activity')
        self.output_tabs.addTab(self.engine_log, 'Live llama.cpp log')
        self.thinking_tab = self.output_tabs.addTab(self.thinking_text, 'Thinking')
        self.output_tabs.setTabToolTip(self.thinking_tab, 'Thinking emitted by the local model for the selected exchange.')
        self.output = column(heading('Output'), self.output_tabs)
        self.vertical = QSplitter(Qt.Vertical)
        self.vertical.addWidget(self.horizontal)
        self.vertical.addWidget(self.output)
        self.vertical.setStretchFactor(0, 1)
        central = column(toolbar, self.vertical)
        central.layout().setStretch(1, 1)
        self.setCentralWidget(central)
        self.engine_status = QLabel('Engine offline')
        self.context_status = QLabel('Context counted before sending')
        self.progress = QProgressBar()
        self.progress.setMaximumWidth(110)
        self.progress.setRange(0, 0)
        self.progress.hide()
        self.statusBar().addPermanentWidget(self.engine_status)
        self.statusBar().addPermanentWidget(self.progress)
        self.statusBar().addPermanentWidget(self.context_status, 1)

    def action(self, menu, text, fn, shortcut=None):
        a = QAction(text, self)
        a.triggered.connect(fn)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        menu.addAction(a)
        return a

    def build_menus(self):
        menu = self.menuBar()
        file = menu.addMenu('&File')
        self.action(file, 'New conversation', self.new_chat, 'Ctrl+N')
        self.action(file, 'Close conversation tab', lambda: self.close_tab(self.tabs.currentIndex()), 'Ctrl+W')
        self.action(file, 'Delete conversation…', self.delete_chat)
        file.addSeparator()
        self.action(file, 'New workspace…', lambda: self.edit_workspace(True))
        self.action(file, 'Workspace settings…', self.edit_workspace)
        self.action(file, 'Open data folder', lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.store.root))))
        self.action(file, 'Exit', self.close, 'Ctrl+Q')
        edit = menu.addMenu('&Edit')
        self.action(edit, 'Copy', self.copy_selection, 'Ctrl+C')
        self.action(edit, 'Retry selected exchange', lambda: self.retry(False))
        self.action(edit, 'Edit and resend…', lambda: self.retry(True))
        view = menu.addMenu('&View')
        for title, w in (('Workspace Explorer', self.left), ('Properties', self.right), ('Output', self.output)):
            self.action(view, 'Show / hide ' + title, lambda checked=False, pane=w: pane.setVisible(not pane.isVisible()))
        self.action(view, 'Reset layout', self.reset_layout)
        engine = menu.addMenu('&Engine')
        self.action(engine, 'Stop generation', self.stop, 'Esc')
        self.action(engine, 'Select model…', self.choose_model)
        self.action(engine, 'Select engine…', self.choose_engine)
        self.action(engine, 'Load model', self.load_model)
        self.action(engine, 'Unload model', self.unload)
        self.action(engine, 'Engine details / live log', self.engine_details)
        settings = menu.addMenu('&Settings')
        self.action(settings, 'Generation properties', lambda: self.show_properties(1))
        self.action(settings, 'Setup…', self.setup)
        self.action(settings, 'Model library…', lambda: self.show_properties(2))
        self.action(settings, 'Workspace…', self.edit_workspace)
        helpmenu = menu.addMenu('&Help')
        self.action(helpmenu, 'About lcb-ai', lambda: QMessageBox.about(self, 'About lcb-ai',
            'lcb-ai 0.10 — Native Linux\nOriginal C engine with a classic Qt desktop.\nLarkin Computing Bureau'))
        send_action = QAction(self)
        send_action.setShortcut(QKeySequence('Ctrl+Return'))
        send_action.triggered.connect(self.send)
        self.addAction(send_action)

    def show_properties(self, tab):
        self.right.show()
        self.right_tabs.setCurrentIndex(tab)

    def reset_layout(self):
        self.left.show()
        self.right.show()
        self.output.show()
        self.horizontal.setSizes([210, 730, 280])
        self.vertical.setSizes([640, 128])

    def copy_selection(self):
        w = QApplication.focusWidget()
        if hasattr(w, 'copy'):
            w.copy()
        elif isinstance(w, QTableWidget) and w.currentItem():
            QApplication.clipboard().setText(w.currentItem().text())

    def open_link(self, url):
        if url.scheme() in ('https', 'http'):
            QDesktopServices.openUrl(url)
        elif url.scheme() == 'lcb-attachment' and self.chat:
            for message in self.chat['messages']:
                for item in message.get('attachments', []):
                    if item['file'] == url.path():
                        QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.store.root / 'attachments' / item['file'])))
                        return

    def refresh_attachments(self):
        self.attachment_list.clear()
        for item in self.attachments:
            row_item = QListWidgetItem(f'{item["name"]}  ·  {item["kind"]}  ·  {item["size"] / 1024:.0f} KiB')
            row_item.setToolTip('Saved local copy. Double-click to open. Removing from this message leaves the source file untouched.')
            self.attachment_list.addItem(row_item)
        self.attachment_list.setVisible(bool(self.attachments))
        self.remove_attachment_button.setVisible(bool(self.attachments))

    def preview_attachment(self, item):
        attachment = self.attachments[self.attachment_list.row(item)]
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.store.root / 'attachments' / attachment['file'])))

    def choose_attachments(self):
        suffixes = ' '.join('*' + ext for ext, (kind, _) in MEDIA_TYPES.items() if self.modalities.get(kind))
        paths, _ = QFileDialog.getOpenFileNames(self, 'Attach media · up to 16 MiB per file', '', f'Supported media ({suffixes})')
        if paths:
            self.add_attachments(paths)

    def add_attachments(self, paths):
        if self.busy or self.importing_media or not self.chat:
            return
        if len(paths) + len(self.attachments) > 8:
            self.error('Use at most 8 attachments per message.')
            return
        self.importing_media = True
        chat_id = self.chat['id']
        self.set_phase('Copying attachments into this chat · source files are left untouched')
        self.refresh_controls()
        def done(result):
            self.importing_media = False
            if result[0] and self.chat and self.chat['id'] == chat_id:
                self.attachments.extend(result[1])
                self.refresh_attachments()
                self.save_draft()
                self.set_phase('Attachments ready · add a message and Send')
            elif not result[0]:
                self.set_phase('Attachment could not be added · ' + result[1])
                self.error(result[1])
            self.refresh_controls()
        self.job(lambda _job: [self.store.import_attachment(path) for path in paths], done)

    def remove_attachment(self):
        index = self.attachment_list.currentRow()
        if not self.busy and not self.importing_media and self.attachments:
            self.attachments.pop(index if index >= 0 else -1)
            self.refresh_attachments()
            self.save_draft()
            self.refresh_controls()

    def set_projector(self, path):
        if self.engine.process or self.busy or self.starting_engine or self.settings_loading:
            return
        try:
            self.config.save(self.config.section(self.model_path), 'projector', path)
        except OSError as exc:
            self.error('Projector setting could not be saved: ' + str(exc))
            return
        self.projector_path = path
        self.projector_text.setText(Path(path).name if path else '')
        self.projector_text.setToolTip(path)
        self.media_status.setText('Projector selected · load to verify media' if path else 'Text only · no projector selected')
        self.note('Projector: ' + (path or 'Text only'))

    def choose_projector(self):
        path, _ = QFileDialog.getOpenFileName(self, 'Choose the matching media projector', str(Path(self.model_path).parent), 'GGUF projectors (*.gguf)')
        if not path:
            return
        self.settings_loading = True
        self.refresh_controls()
        def done(result):
            self.settings_loading = False
            if result[0] and result[1].projector:
                self.set_projector(path)
            else:
                self.error('Choose a multimodal projector GGUF from the same model release.')
            self.refresh_controls()
        self.job(lambda _job: self.core.model(path), done)

    def note(self, text):
        self.activity.appendPlainText(time.strftime('%H:%M:%S   ') + text)
        self.statusBar().setToolTip(text)

    def error(self, exc):
        self.note(str(exc))
        QMessageBox.warning(self, 'lcb-ai', str(exc))

    def job(self, fn, callback, progress=None, budget=None, telemetry=None, phase=None):
        job = Job(fn, self)
        self.jobs.add(job)
        job.result.connect(callback)
        if progress:
            job.progress.connect(progress)
        if budget:
            job.budget.connect(budget)
        if telemetry:
            job.telemetry.connect(telemetry)
        if phase:
            job.phase.connect(phase)
        job.finished.connect(lambda: self.jobs.discard(job))
        job.finished.connect(job.deleteLater)
        job.start()
        return job

    def refresh_controls(self):
        running = self.engine.process is not None or self.starting_engine
        for w in (self.new_button, self.tree, self.workspace_new, self.workspace_edit, self.tabs,
                  self.retry_button, self.resend_button, self.exchange, self.prompt):
            w.setEnabled(not self.busy and not self.importing_media)
        for w in (self.engine_button, self.model_button, self.port, self.use_model_button):
            w.setEnabled(not self.busy and not running and not self.settings_loading)
        self.load_button.setEnabled(not self.busy and not running and not self.settings_loading)
        self.unload_button.setEnabled(not self.busy and running)
        self.send_button.setEnabled(not self.busy and not self.settings_loading and self.chat is not None and bool(self.model_path) and self.health == 'Ready')
        self.stop_button.setEnabled(self.busy)
        for w in (self.projector_button, self.projector_clear):
            w.setEnabled(not self.busy and not running and not self.settings_loading and bool(self.model_path))
        self.attach_button.setEnabled(not self.busy and not self.importing_media and any(self.modalities.values()))
        self.attach_button.setToolTip('Load a model with its matching projector to enable supported media inputs. Up to 8 files, 16 MiB each.')
        self.remove_attachment_button.setEnabled(not self.busy and not self.importing_media and bool(self.attachments))
        self.send_button.setEnabled(self.send_button.isEnabled() and not self.importing_media)
        self.retry_button.setEnabled(not self.busy and not self.importing_media and self.exchange.count() > 0)
        self.resend_button.setEnabled(not self.busy and not self.importing_media and self.exchange.count() > 0)
        for w in self.generation_widgets:
            w.setEnabled(not self.busy and not self.settings_loading and bool(self.model_path))
        self.scan_button.setEnabled(not self.scanning)
        self.progress.setVisible(self.busy or self.health == 'Loading')
        self.engine_status.setText(self.health)

    def refresh_tree(self):
        self.tree.clear()
        for identifier, workspace in self.store.workspaces.items():
            parent = QTreeWidgetItem([workspace['name']])
            parent.setData(0, Qt.UserRole, ('workspace', identifier))
            self.tree.addTopLevelItem(parent)
            for chat in self.store.chats.values():
                if chat['workspace'] == identifier:
                    child = QTreeWidgetItem([chat['title']])
                    child.setData(0, Qt.UserRole, ('chat', chat['id']))
                    parent.addChild(child)
                    if self.chat and self.chat['id'] == chat['id']:
                        self.tree.setCurrentItem(child)
            parent.setExpanded(True)
        self.master.setPlainText(self.store.workspaces[self.workspace_id]['master_prompt'])

    def tree_selected(self, item, _column):
        if self.busy:
            return
        kind, identifier = item.data(0, Qt.UserRole)
        if kind == 'chat':
            self.open_chat(identifier)
        else:
            self.workspace_id = identifier
            self.master.setPlainText(self.store.workspaces[identifier]['master_prompt'])

    def chat_menu(self, point):
        item = self.tree.itemAt(point)
        if not item or self.busy:
            return
        kind, identifier = item.data(0, Qt.UserRole)
        if kind != 'chat':
            return
        menu = QMenu(self)
        menu.addAction('Open', lambda: self.open_chat(identifier))
        menu.addAction('Close tab', lambda: self.close_tab(self.tab_index(identifier)))
        menu.addAction('Delete…', lambda: self.delete_chat(identifier))
        menu.exec(self.tree.viewport().mapToGlobal(point))

    def tab_index(self, identifier):
        for i in range(self.tabs.count()):
            if self.tabs.tabData(i) == identifier:
                return i
        return -1

    def save_draft(self):
        if self.restoring or not self.chat or self.busy:
            return True
        try:
            candidate = copy.deepcopy(self.chat)
            candidate['draft'] = self.prompt.toPlainText()
            candidate['draft_attachments'] = copy.deepcopy(self.attachments)
            self.store.save('chats', candidate)
            self.chat = candidate
            return True
        except (OSError, ValueError) as exc:
            self.note('Draft could not be saved: ' + str(exc))
            return False

    def draft_changed(self):
        if not self.restoring:
            self.save_timer.start()

    def open_chat(self, identifier):
        if self.busy or self.importing_media or not self.save_draft():
            return
        index = self.tab_index(identifier)
        self.tabs.blockSignals(True)
        if index < 0:
            if self.tabs.count() >= 32:
                self.tabs.blockSignals(False)
                self.note('Close a conversation tab first (32 open tabs maximum).')
                return
            index = self.tabs.addTab(self.store.chats[identifier]['title'])
            self.tabs.setTabData(index, identifier)
        self.tabs.setCurrentIndex(index)
        self.tabs.blockSignals(False)
        self.chat = copy.deepcopy(self.store.chats[identifier])
        self.workspace_id = self.chat['workspace']
        self.show_chat()

    def show_chat(self):
        self.restoring = True
        self.prompt.setPlainText(self.chat['draft'] if self.chat else '')
        self.attachments = copy.deepcopy(self.chat.get('draft_attachments', [])) if self.chat else []
        self.refresh_attachments()
        self.exchange.clear()
        if self.chat:
            for i in range(0, len(self.chat['messages']), 2):
                self.exchange.addItem(f'{i // 2 + 1}: {self.chat["messages"][i]["content"][:70]}')
        self.exchange.setCurrentIndex(self.exchange.count() - 1)
        self.render()
        self.restoring = False
        self.refresh_tree()
        self.refresh_controls()

    def chat_heading(self, cursor, title, user=False):
        block = QTextBlockFormat()
        block.setTopMargin(16)
        block.setBottomMargin(8)
        block.setBackground(QColor('#e4edf3' if user else '#f0efe7'))
        cursor.setBlockFormat(block)
        style = QTextCharFormat()
        style.setFontWeight(QFont.Bold)
        style.setFontPointSize(11)
        style.setForeground(QColor('#31546b' if user else '#374c3e'))
        cursor.insertText(title, style)
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat())

    def render(self, partial=None, prompt=None):
        self.transcript.clear()
        cursor = self.transcript.textCursor()
        self.exchange_positions = []
        if not self.chat or (not self.chat['messages'] and prompt is None):
            self.chat_heading(cursor, 'Your chat is ready')
            cursor.insertText('Load a model, type a message, and press Send.\n'
                              'Use + New chat to start another conversation. No workspace setup is needed.')
            self.show_thinking('')
            return
        for i, m in enumerate(self.chat['messages']):
            if i % 2 == 0:
                self.exchange_positions.append(cursor.position())
            title = 'You' if i % 2 == 0 else 'Assistant' + (' · ' + m['model'] if m.get('model') else '')
            self.chat_heading(cursor, title, i % 2 == 0)
            if i % 2:
                cursor.insertMarkdown(m['content'])
                cursor.insertText('\n' + m.get('status', 'unknown').capitalize())
                if m.get('reasoning_content'):
                    cursor.insertText(' · Thinking trace saved · select this exchange and open Output → Thinking')
                if m.get('error'):
                    cursor.insertText('\n' + m['error'])
            else:
                cursor.insertText(m['content'])
                for item in m.get('attachments', []):
                    cursor.insertText('\n')
                    link = QTextCharFormat()
                    link.setAnchor(True)
                    link.setAnchorHref('lcb-attachment:' + item['file'])
                    link.setForeground(QColor('#315f8a'))
                    cursor.insertText('↳ ' + item['name'] + ' · ' + item['kind'], link)
                    cursor.setCharFormat(QTextCharFormat())
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat())
        if prompt is not None:
            self.chat_heading(cursor, 'You', True)
            cursor.insertText(prompt)
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat())
            self.chat_heading(cursor, 'Assistant · ' + self.model_name())
            self.stream_position = cursor.position()
            if partial:
                cursor.insertMarkdown(partial)
        else:
            index = self.exchange.currentIndex()
            message = self.chat['messages'][index * 2 + 1] if index >= 0 else {}
            self.show_thinking(message.get('reasoning_content', ''))
        self.transcript.setTextCursor(cursor)
        self.transcript.ensureCursorVisible()

    def model_name(self):
        return self.model_info.name if self.model_info else Path(self.model_path).name

    def show_thinking(self, text):
        scroll = self.thinking_text.verticalScrollBar()
        follow = scroll.value() >= scroll.maximum() - 4
        self.thinking_text.setPlainText(text)
        if follow:
            scroll.setValue(scroll.maximum())
        self.output_tabs.setTabToolTip(self.thinking_tab,
            'Model thinking for the selected exchange · trace received' if text else
            'No thinking trace received for this exchange.')

    def update_stream(self, text):
        if not self.busy:
            return
        cursor = self.transcript.textCursor()
        scroll = self.transcript.verticalScrollBar()
        follow = scroll.value() >= scroll.maximum() - 4
        cursor.setPosition(self.stream_position)
        cursor.movePosition(QTextCursor.End, QTextCursor.KeepAnchor)
        cursor.removeSelectedText()
        cursor.insertMarkdown(text)
        if follow:
            self.transcript.setTextCursor(cursor)
            self.transcript.ensureCursorVisible()

    def stream_telemetry(self, event):
        if not self.busy:
            return
        self.last_stream = event
        self.show_thinking(event['reasoning_content'])
        if event['content']:
            self.set_phase('Writing answer')
        elif event['reasoning_content']:
            self.set_phase('Thinking · follow the Thinking tab in Output')
        elif event['total']:
            self.set_phase(f'Reading prompt · {event["processed"]:,} / {event["total"]:,} tokens')
        else:
            self.set_phase('Waiting for the model · connection active')

    def set_phase(self, text):
        self.phase_text = text
        self.refresh_activity()

    def refresh_activity(self):
        active = self.busy or self.health == 'Loading'
        elapsed = time.monotonic() - self.phase_started
        suffix = f' · {elapsed:.0f}s elapsed' if active else ''
        self.phase_label.setText(self.phase_text + suffix)
        raw = self.engine.read_log()
        if raw:
            self.engine_log.moveCursor(QTextCursor.End)
            self.engine_log.insertPlainText(raw)
            self.engine_log.ensureCursorVisible()

    def engine_details(self):
        self.output.show()
        self.output_tabs.setCurrentIndex(1)
        if self.engine.command:
            self.note('Engine command: ' + shlex.join(self.engine.command))
        self.note('Official llama.cpp server documentation: https://github.com/ggml-org/llama.cpp/blob/b10566/tools/server/README.md')

    def scroll_exchange(self, index):
        if self.restoring or not self.chat or index < 0:
            return
        self.show_thinking(self.chat['messages'][index * 2 + 1].get('reasoning_content', ''))
        if index < len(self.exchange_positions):
            cursor = self.transcript.textCursor()
            cursor.setPosition(self.exchange_positions[index])
            self.transcript.setTextCursor(cursor)
            self.transcript.ensureCursorVisible()

    def tab_changed(self, index):
        if index >= 0 and not self.restoring:
            previous = self.tab_index(self.chat['id']) if self.chat else -1
            if not self.save_draft():
                self.tabs.blockSignals(True)
                self.tabs.setCurrentIndex(previous)
                self.tabs.blockSignals(False)
                return
            self.open_chat(self.tabs.tabData(index))

    def close_tab(self, index):
        if self.busy or self.importing_media or index < 0 or not self.save_draft():
            return
        self.tabs.blockSignals(True)
        self.tabs.removeTab(index)
        self.tabs.blockSignals(False)
        current = self.tabs.currentIndex()
        self.chat = None
        if current >= 0:
            self.open_chat(self.tabs.tabData(current))
        else:
            self.show_chat()

    def new_chat(self):
        if self.busy or self.importing_media or not self.save_draft():
            return
        if self.chat and self.chat['workspace'] == self.workspace_id and not self.chat['messages'] and not self.chat['draft'] and not self.attachments:
            self.prompt.setFocus()
            return
        try:
            chat = self.store.new(self.workspace_id)
            self.open_chat(chat['id'])
            self.prompt.setFocus()
        except (OSError, ValueError) as exc:
            self.error(exc)

    def delete_chat(self, identifier=None):
        if self.busy or self.importing_media:
            return
        identifier = identifier if isinstance(identifier, str) else self.chat['id'] if self.chat else None
        if not identifier:
            return
        if QMessageBox.question(self, 'Delete conversation', 'Permanently delete this saved conversation?') != QMessageBox.Yes:
            return
        try:
            if not self.save_draft():
                return
            self.store.delete(identifier)
            self.tabs.blockSignals(True)
            index = self.tab_index(identifier)
            if index >= 0:
                self.tabs.removeTab(index)
            self.tabs.blockSignals(False)
            if self.chat and self.chat['id'] == identifier:
                self.chat = None
                if self.tabs.currentIndex() >= 0:
                    self.open_chat(self.tabs.tabData(self.tabs.currentIndex()))
                else:
                    self.show_chat()
            self.refresh_tree()
        except (OSError, ValueError) as exc:
            self.error(exc)

    def edit_workspace(self, new=False):
        if self.busy:
            return
        workspace = self.store.workspaces[self.workspace_id]
        dialog = QDialog(self)
        dialog.setWindowTitle('New workspace' if new else 'Workspace settings')
        dialog.resize(540, 350)
        name = QLineEdit('' if new else workspace['name'])
        prompt = QPlainTextEdit('' if new else workspace['master_prompt'])
        form = QFormLayout(dialog)
        form.addRow('Name', name)
        form.addRow('Master prompt', prompt)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        form.addRow(buttons)
        buttons.rejected.connect(dialog.reject)
        def save():
            try:
                obj = self.store.workspace(name.text(), prompt.toPlainText(), None if new else workspace['id'])
                self.workspace_id = obj['id']
                self.refresh_tree()
                dialog.accept()
            except (OSError, ValueError) as exc:
                self.error(exc)
        buttons.accepted.connect(save)
        dialog.exec()

    def retry(self, edit):
        if self.busy or self.importing_media or not self.chat or self.exchange.currentIndex() < 0 or not self.save_draft():
            return
        turn = self.exchange.currentIndex()
        prompt = self.chat['messages'][turn * 2]['content']
        if edit:
            dialog = QDialog(self)
            dialog.setWindowTitle('Edit and resend in a new conversation')
            dialog.resize(560, 320)
            text = QPlainTextEdit(prompt)
            buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
            layout = QVBoxLayout(dialog)
            layout.addWidget(text)
            layout.addWidget(buttons)
            buttons.accepted.connect(dialog.accept)
            buttons.rejected.connect(dialog.reject)
            if dialog.exec() != QDialog.Accepted:
                return
            prompt = text.toPlainText()
        try:
            chat = self.store.branch(self.chat, turn, prompt, 'Edit' if edit else 'Retry')
            self.open_chat(chat['id'])
            self.send()
        except (OSError, ValueError) as exc:
            self.error(exc)

    def load_settings(self):
        self.g = self.config.generation(self.model_path, self.model_info)
        self.model_text.setText(Path(self.model_path).name if self.model_path else '')
        self.model_text.setToolTip(self.model_path)
        for i, widget in enumerate(self.generation_widgets):
            widget.blockSignals(True)
            value = getattr(self.g, FIELDS[i])
            widget.setCurrentIndex(value) if i == 4 else widget.setValue(value)
            widget.blockSignals(False)

    def read_model(self, path, initial=False, auto_load=False):
        # Header reads can block on a sleeping/disconnected NAS. Never run them
        # in Qt's UI thread, including startup and generation property edits.
        self.metadata_epoch += 1
        epoch = self.metadata_epoch
        self.settings_loading = True
        self.set_phase('Inspecting model header · ' + Path(path).name)
        self.refresh_controls()
        def done(result):
            if epoch != self.metadata_epoch:
                return
            self.settings_loading = False
            if result[0]:
                model, projector = result[1]
                try:
                    if model_role(model) in ('Projector companion', 'Draft companion'):
                        raise ValueError('This is a ' + model_role(model).lower() + '. Choose a main chat model GGUF.')
                    if not initial:
                        self.config.import_windows_model(self.model_path, path)
                        self.config.save('engine', 'model', path)
                    self.model_path, self.model_info = path, model
                    self.projector_path = projector
                    self.projector_text.setText(Path(projector).name if projector else '')
                    self.projector_text.setToolTip(projector)
                    self.modalities = {}
                    self.media_status.setText('Projector selected · load to verify media' if projector else 'Text only · no matching projector selected')
                    self.show_model_details(model)
                    self.load_settings()
                    self.note('Model selected: ' + model.name)
                    if not self.busy and not self.engine.process:
                        self.set_phase('PTQ1_0 model · requires a compatible PrismML engine; stock b10566 cannot load it' if model.filetype == 143 else 'Model selected · press Load model, then send a message')
                except (OSError, ValueError) as exc:
                    self.set_phase('Cannot select model · ' + str(exc))
                    self.error(exc)
            else:
                self.set_phase('Cannot read model · ' + result[1])
                self.note(result[1])
            self.refresh_controls()
            if auto_load and result[0] and self.model_path == path and self.model_info and not self.model_info.projector:
                self.load_model()
        def inspect(_job):
            model = self.core.model(path)
            saved = self.config.get(self.config.section(path), 'projector', '__auto__')
            projector = suggested_projector(self.core, model) if saved == '__auto__' else saved
            model.projector_hint = projector
            return model, projector
        self.job(inspect, done)

    def edit_generation(self, field, value):
        if self.busy or not self.model_path:
            return
        try:
            g = Generation.from_buffer_copy(self.g)
            self.core.set(g, field, value)
            self.config.save(self.config.section(self.model_path), FIELDS[field], value)
            self.load_settings()
            self.note('Context saved. Unload and load to apply it.' if field == 3 else 'Generation setting saved for this model.')
        except (OSError, ValueError) as exc:
            self.load_settings()
            self.error(exc)

    def choose_engine(self):
        if self.busy or self.engine.process:
            return
        path, _ = QFileDialog.getOpenFileName(self, 'Select Linux llama-server', self.engine_path)
        if path:
            try:
                with open(path, 'rb') as f:
                    if f.read(2) == b'MZ':
                        raise ValueError('Choose a Linux llama-server executable, not a Windows .exe.')
                if not os.access(path, os.X_OK):
                    raise ValueError('This file is not executable. Set its executable permission first.')
                self.config.save('engine', 'executable', path)
                self.engine_path = path
                self.refresh_controls()
            except (OSError, ValueError) as exc:
                self.error(exc)

    def choose_model(self, auto_load=False):
        if self.busy or self.engine.process:
            return
        path, _ = QFileDialog.getOpenFileName(self, 'Select GGUF model', self.model_path, 'GGUF models (*.gguf);;All files (*)')
        if path:
            self.read_model(path, auto_load=auto_load)

    def set_model(self, path):
        if self.busy or self.engine.process or self.starting_engine:
            self.note('Unload the current model before selecting another.')
            return
        self.read_model(path)

    def port_changed(self, value):
        if not hasattr(self, 'activity'):
            return
        try:
            self.config.save('engine', 'port', value)
            self.health = 'Engine offline'
            self.refresh_controls()
        except OSError as exc:
            self.error(exc)

    def setup(self):
        if self.busy or self.engine.process:
            self.note('Stop generation and unload the model before changing setup.')
            return
        dialog = QDialog(self)
        dialog.setWindowTitle('Setup — Linux')
        dialog.resize(600, 280)
        layout = QVBoxLayout(dialog)
        label = QLabel('Choose a native Linux llama.cpp server and a GGUF model.\n'
                       'llama.cpp is the integrated runner. Ollama and KoboldCpp are separate runners.\n'
                       'Use the tested b10566 release or a compatible build.')
        label.setWordWrap(True)
        layout.addWidget(label)
        for title, url in [('Download llama.cpp (Linux builds)', 'https://github.com/ggml-org/llama.cpp/releases/tag/b10566'),
                           ('Other llama.cpp builds', 'https://github.com/ggml-org/llama.cpp/releases'),
                           ('Ollama — separate runner', 'https://ollama.com/download/linux'),
                           ('KoboldCpp — separate runner', 'https://github.com/LostRuins/koboldcpp/releases')]:
            layout.addWidget(button(title, lambda checked=False, target=url: QDesktopServices.openUrl(QUrl(target))))
        layout.addWidget(button('Select Linux llama-server…', self.choose_engine))
        layout.addWidget(button('Select GGUF model…', self.choose_model))
        buttons = QDialogButtonBox(QDialogButtonBox.Close)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)
        dialog.exec()

    def load_model(self):
        if self.busy or self.engine.process or self.starting_engine or self.settings_loading:
            return
        if not self.engine_path:
            self.choose_engine()
        if not self.model_path:
            self.choose_model(auto_load=True)
            return
        if not self.engine_path:
            return
        self.starting_engine = True
        self.health = 'Loading'
        self.phase_started = time.monotonic()
        self.set_phase('Loading model · ' + self.model_name())
        self.refresh_controls()
        exe, model, port, context = self.engine_path, self.model_path, self.port.value(), self.g.context_tokens
        projector = self.projector_path
        def start(_job):
            self.engine.start(exe, model, port, context, self.store.root / 'engine.log', projector)
        def done(result):
            self.starting_engine = False
            if result[0]:
                if model.startswith('/mnt/'):
                    self.note('Reading the model directly from ' + model + '. Share speed affects loading and memory-mapped reads. No copy or conversion is performed.')
                self.note('Engine command: ' + shlex.join(self.engine.command))
                self.note('Loading model. See Output > Live llama.cpp log.')
            else:
                self.health = 'Engine offline'
                self.set_phase('Model could not load · ' + result[1])
                self.error(result[1])
            self.refresh_controls()
        self.job(start, done)

    def unload(self):
        if self.busy or not self.engine.process:
            return
        self.engine.stop()
        self.health = 'Engine offline'
        self.modalities = {}
        self.media_status.setText('Text · load a model to check media support')
        self.note('Model unloaded.')
        self.set_phase('Model unloaded · load a model to continue')
        self.refresh_controls()

    def poll(self):
        if self.starting_engine:
            return
        if self.engine.process and self.engine.process.poll() is not None:
            self.refresh_activity()
            code = self.engine.process.returncode
            self.engine.stop()
            detail = self.engine.failure()
            self.health = 'Engine offline'
            self.set_phase(f'Engine exited ({code}) · ' + detail)
            self.note(detail)
            self.output.show()
            self.output_tabs.setCurrentIndex(1)
        if self.probing:
            return
        self.probing = True
        port = self.port.value()
        process = self.engine.process
        def probe(_job):
            try:
                data = json.loads(Transport(port).exchange('/health', timeout=.5, deadline=2))
                if data.get('status') == 'ok':
                    props = json.loads(Transport(port).exchange('/props', timeout=.5, deadline=2))
                    return 'Ready', props.get('modalities', {}), props.get('model_path', '')
                return 'Port is not a ready engine', {}, ''
            except EngineHTTPError as exc:
                return ('Loading' if exc.status == 503 else str(exc)), {}, ''
            except Exception:
                return ('Loading' if process and process.poll() is None else 'Engine offline'), {}, ''
        def done(result):
            self.probing = False
            if port == self.port.value() and process is self.engine.process:
                health, modalities, loaded = result[1] if result[0] else ('Engine offline', {}, '')
                matches = loaded and os.path.realpath(loaded) == os.path.realpath(self.model_path)
                self.modalities = modalities if matches and health == 'Ready' else {}
                missing_video = self.modalities.get('video') and not all(shutil.which(exe) for exe in ('ffmpeg', 'ffprobe'))
                if missing_video:
                    self.modalities = dict(self.modalities, video=False)
                if health == 'Ready' and not matches:
                    health = 'Different model on this port'
                kinds = [{'vision': 'Images', 'audio': 'Audio (experimental)', 'video': 'Video'}[key] for key in ('vision', 'audio', 'video') if self.modalities.get(key) is True]
                self.media_status.setText('Text' + (' + ' + ' + '.join(kinds) if kinds else ' only') + (' · engine capabilities' if health == 'Ready' else ' · load to check media'))
                self.media_status.setToolTip('Input types reported by the engine; this does not verify output accuracy. Audio quality depends on the model and runtime.' + (' Video requires ffmpeg and ffprobe; on Fedora: sudo dnf install ffmpeg-free.' if missing_video else ''))
                if health != self.health:
                    self.health = health
                    self.note(health)
                    if health == 'Ready' and not self.busy:
                        self.set_phase('Ready · ' + self.model_name() + ' · type a message below')
                    elif health == 'Loading' and not self.busy:
                        self.set_phase('Loading model · ' + self.model_name())
                self.refresh_controls()
        self.job(probe, done)

    def choose_folder(self):
        folder = QFileDialog.getExistingDirectory(self, 'Model library', self.folder.text())
        if folder:
            self.folder.setText(folder)
            self.scan()

    def scan(self):
        if self.scanning:
            return
        folder = self.folder.text()
        if not folder:
            self.note('Choose an existing model folder.')
            return
        try:
            self.config.save('library', 'folder', folder)
            self.config.save('library', 'recursive', int(self.recursive.isChecked()))
        except OSError as exc:
            self.error(exc)
            return
        self.scanning = True
        self.scan_cancel.clear()
        recursive = self.recursive.isChecked()
        self.refresh_controls()
        def done(result):
            self.scanning = False
            if result[0]:
                models, skipped = result[1]
                self.models.setSortingEnabled(False)
                self.models.setRowCount(len(models))
                self.library_models = {m.path: m for m in models}
                for i, model in enumerate(models):
                    role = model_role(model)
                    if role == 'Chat model':
                        role = 'Media projector found' if model.projector_hint else 'Text only · no projector found'
                    name = QTableWidgetItem(Path(model.path).stem + '\n' + role)
                    name.setToolTip(model.name + '\n' + model.path + '\n' + role)
                    name.setData(Qt.UserRole, model.path)
                    size = SizeItem(f'{model.bytes / 1024**3:.2f} GiB')
                    size.setData(Qt.UserRole, model.bytes)
                    self.models.setItem(i, 0, name)
                    self.models.setItem(i, 1, size)
                self.models.setSortingEnabled(True)
                self.models.sortItems(0)
                self.note(f'Library: {len(models)} models; {skipped} unreadable files skipped (limit 512 models).')
            else:
                self.note(result[1])
            self.refresh_controls()
        self.job(lambda _job: scan_models(self.core, folder, recursive, self.scan_cancel), done)

    def inspect_model(self):
        index = self.models.currentRow()
        if index < 0 or not self.models.item(index, 0):
            return
        model = self.library_models[self.models.item(index, 0).data(Qt.UserRole)]
        self.show_model_details(model)

    def show_model_details(self, model):
        values = [('Name', model.name), ('Architecture', model.architecture or 'Unspecified'),
                  ('Parameters', model.size or 'Unspecified'), ('File size', f'{model.bytes:,} bytes'),
                  ('Quantization', self.core.lib.model_quant(model.filetype)),
                  ('Type', model_role(model)),
                  ('Media projector', Path(getattr(model, 'projector_hint', '')).name if getattr(model, 'projector_hint', '') else 'None detected; select a matching projector to enable media'),
                  ('File', Path(model.path).name), ('Location', model.path)]
        self.details.setRowCount(len(values))
        for i, (key, value) in enumerate(values):
            self.details.setItem(i, 0, QTableWidgetItem(key))
            self.details.setItem(i, 1, QTableWidgetItem(value))
            self.details.item(i, 1).setToolTip(value)

    def use_selected_model(self, *_args):
        index = self.models.currentRow()
        if index >= 0:
            self.set_model(self.models.item(index, 0).data(Qt.UserRole))

    def send(self):
        if self.busy or self.settings_loading or self.starting_engine or self.importing_media or not self.chat:
            return
        prompt = self.prompt.toPlainText()
        if not prompt.strip() and self.attachments:
            prompt = 'Describe the attached media.'
        try:
            text_ok(prompt, MAX_PROMPT, False)
            if not self.model_path:
                raise ValueError('Select the GGUF model used by the local server first.')
            if not self.save_draft():
                raise ValueError('Save the current draft before sending.')
        except ValueError as exc:
            self.error(exc)
            return
        messages = copy.deepcopy(self.chat['messages'])
        attachments = copy.deepcopy(self.attachments)
        instruction = self.store.workspaces[self.chat['workspace']]['master_prompt']
        g = Generation.from_buffer_copy(self.g)
        model = self.model_path
        self.transport = Transport(self.port.value())
        transport = self.transport
        self.busy = True
        self.started = time.monotonic()
        self.phase_started = self.started
        self.last_stream = {'content': '', 'reasoning_content': ''}
        self.show_thinking('')
        self.set_phase('Preparing message · applying the model template and counting tokens')
        self.refresh_controls()
        self.render('', prompt)
        def work(job):
            wire, budget = transport.prepare(self.core, model, messages, instruction, prompt, g, attachments, self.store)
            job.budget.emit(budget)
            job.phase.emit('Reading prompt · waiting for the model’s first token')
            return transport.generate(self.core, wire, job.progress.emit, job.telemetry.emit)
        def budget_ready(budget):
            text = f'Context: {budget["prompt"]} + {budget["response"]} reply + 32 / {budget["context"]}; {budget["omitted"]} older exchanges excluded'
            if budget['prompt'] is None:
                text = f'Media context: engine checks full history against {budget["context"]} tokens; reply up to {budget["response"]}. No history omitted.'
            self.context_status.setText(text)
            self.note(text + '; all history remains saved.')
        def done(result):
            self.busy = False
            self.transport = None
            elapsed = time.monotonic() - self.started
            if not result[0]:
                self.note(result[1])
                self.set_phase('Stopped' if transport.cancelled.is_set() else 'Cannot send · ' + result[1])
                self.render()
            else:
                reply = result[1]
                if reply['content'] or reply.get('reasoning_content'):
                    candidate = copy.deepcopy(self.chat)
                    candidate['messages'].extend([dict(role='user', content=prompt, attachments=attachments),
                        dict(role='assistant', content=reply['content'] or '[No final answer was emitted.]',
                             reasoning_content=reply.get('reasoning_content', ''), model=self.model_name(),
                             status=reply['status'], error=reply['error'])])
                    candidate['draft'] = ''
                    candidate['draft_attachments'] = []
                    if len(candidate['messages']) == 2:
                        candidate['title'] = truncate(prompt.replace('\n', ' '), 80)
                    # Keep the generated text in memory if the disk save fails. Closing retries it.
                    self.chat = candidate
                    try:
                        self.store.save('chats', candidate)
                        self.prompt.blockSignals(True)
                        self.prompt.clear()
                        self.prompt.blockSignals(False)
                    except (OSError, ValueError) as exc:
                        self.prompt.blockSignals(True)
                        self.prompt.clear()
                        self.prompt.blockSignals(False)
                        self.error('Reply is still in memory; save failed: ' + str(exc))
                    self.tabs.setTabText(self.tabs.currentIndex(), candidate['title'])
                    self.show_chat()
                else:
                    self.render()
                count = (f'{reply["tokens"]} generated tokens' if reply['tokens'] else
                         f'{len(reply["content"])} answer / {len(reply.get("reasoning_content", ""))} thinking characters')
                self.set_phase(f'{reply["status"].capitalize()} · {count} · {elapsed:.1f}s' + (' · ' + reply['error'] if reply['error'] else ''))
                self.note(f'Answer {reply["status"]}; {count} in {elapsed:.2f}s. ' + reply['error'])
            self.refresh_controls()
        self.job(work, done, self.update_stream, budget_ready, self.stream_telemetry, self.set_phase)

    def stop(self):
        if self.transport:
            self.transport.cancel()
            self.note('Stopping generation; keeping any received text.')

    def closeEvent(self, event):
        if self.starting_engine or self.settings_loading or self.scanning or self.importing_media:
            self.scan_cancel.set()
            self.note('Waiting for the current file or engine operation to finish. Close again when it completes.')
            event.ignore()
            return
        if self.busy:
            self.stop()
            self.note('Stopping. Close again after the partial reply is saved.')
            event.ignore()
            return
        if not self.save_draft():
            event.ignore()
            return
        try:
            self.config.save('linux-session', 'tabs', json.dumps([self.tabs.tabData(i) for i in range(self.tabs.count())]))
            self.config.save('linux-session', 'active', self.chat['id'] if self.chat else '')
            self.config.save('linux-layout', 'sizes', json.dumps(self.horizontal.sizes()))
            self.config.save('linux-layout', 'vertical', json.dumps(self.vertical.sizes()))
            for key, widget in (('left', self.left), ('right', self.right), ('output', self.output)):
                self.config.save('linux-layout', key, int(not widget.isHidden()))
        except OSError as exc:
            self.error(exc)
            event.ignore()
            return
        self.poll_timer.stop()
        self.activity_timer.stop()
        self.save_timer.stop()
        self.scan_cancel.set()
        self.engine.stop()
        for job in list(self.jobs):
            job.wait()
        self.store.close()
        event.accept()


def main():
    parser = argparse.ArgumentParser(description='LCB-AI native Linux desktop')
    parser.add_argument('--data-dir', help='Use an explicit local data folder, including copied LCB/LTS/LTI data')
    args = parser.parse_args()
    app = QApplication(sys.argv[:1])
    app.setApplicationName('lcb-ai')
    apply_theme(app)
    try:
        window = Window(args.data_dir)
    except Exception as exc:
        QMessageBox.critical(None, 'Cannot start lcb-ai', str(exc))
        return 1
    window.show()
    return app.exec()


def apply_theme(app):
    app.setStyle('Fusion')
    # Set every palette surface used by native KDE file dialogs too. Otherwise
    # their alternating rows/tooltips retain dark-theme colors under our light CSS.
    palette = QPalette()
    colors = {
        QPalette.Window: '#ece9d8', QPalette.WindowText: '#2f373a',
        QPalette.Base: '#ffffff', QPalette.AlternateBase: '#f3f1e6',
        QPalette.Text: '#2f373a', QPalette.Button: '#ece9d8',
        QPalette.ButtonText: '#2f373a', QPalette.Highlight: '#c0d3e0',
        QPalette.HighlightedText: '#2f373a', QPalette.ToolTipBase: '#fffbe6',
        QPalette.ToolTipText: '#2f373a', QPalette.PlaceholderText: '#677173',
        QPalette.Link: '#315f8a', QPalette.LinkVisited: '#655080',
        QPalette.Light: '#ffffff', QPalette.Midlight: '#faf9f1',
        QPalette.Mid: '#b8b7ab', QPalette.Dark: '#999a91', QPalette.Shadow: '#676962',
    }
    for role, color in colors.items():
        palette.setColor(role, QColor(color))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor('#777b72'))
    app.setPalette(palette)
    app.setStyleSheet('''
        QWidget { font-family: "DejaVu Sans"; font-size: 12px; color: #2f373a; }
        QMainWindow, QDialog, QWidget { background: #ece9d8; }
        QLineEdit, QPlainTextEdit, QTextBrowser, QTreeWidget, QTableWidget, QSpinBox,
        QDoubleSpinBox, QComboBox { background: white; selection-background-color: #c0d3e0; selection-color: #2f373a; }
        QTextBrowser#chatTranscript { font-size: 14px; border: 1px solid #c8c9c1; border-radius: 5px; }
        QLabel#chatPhase { padding: 8px; background: #e4edf3; color: #31546b; border-radius: 4px; }
        QLabel#heading { background: #d9d8c9; border: 1px solid #999a91; padding: 3px; font-weight: bold; }
        QPushButton { padding: 5px 10px; min-height: 20px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: 24px; }
        QTabBar::tab { padding: 6px 10px; }
        QHeaderView::section { padding: 4px; background: #e2e1d6; border: none; }
        QListWidget { background: white; border: 1px solid #c8c9c1; }
        QTabBar::tab:selected { background: #c0d3e0; }
        QSplitter::handle { background: #c9c8b9; }
        QWidget:disabled { color: #85877d; }
    ''')
if __name__ == '__main__':
    sys.exit(main())
