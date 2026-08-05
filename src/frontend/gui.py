import sys
import re
import json
import subprocess
from collections import deque
from dataclasses import dataclass
from typing import List, Optional, Tuple, Deque
from PyQt6.QtCore import QThread, pyqtSignal, QTimer, Qt
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QTextEdit, QLineEdit, QPushButton, QMessageBox,
    QSplitter, QTabWidget, QLabel, QTreeWidget, QTreeWidgetItem,
    QTableWidget, QTableWidgetItem, QGroupBox, QFormLayout, QSpinBox,
    QDialog, QDialogButtonBox
)
# 你的后端 exe 路径（按你给的）
BACKEND_EXE = r"E:\OS-FS-Sim\build\bin\os_main.exe"
# 提示符形如：[/] > 或 [/dir1] >
PROMPT_RE = re.compile(r"\[[^\]]*\]\s*>\s*$")
_BRANCH_RE = re.compile(r"(├──|└──|├─|└─|\+--|\|--|`--)")


@dataclass
class CommandResult:
    cmd: str
    lines: List[str]
    prompt: str


class BackendReader(QThread):
    line = pyqtSignal(str)

    def __init__(self, proc: subprocess.Popen):
        super().__init__()
        self.proc = proc
        self._running = True

    def run(self):
        while self._running and self.proc and self.proc.stdout:
            s = self.proc.stdout.readline()
            if not s:
                break
            self.line.emit(s.rstrip("\n"))

    def stop(self):
        self._running = False

class BackendController(QWidget):
    raw_line = pyqtSignal(str)
    cmd_finished = pyqtSignal(CommandResult)

    def __init__(self, exe_path: str, parent=None):
        super().__init__(parent)
        self.exe_path = exe_path
        self.proc: Optional[subprocess.Popen] = None
        self.reader = None  # 你的 BackendReader

        self._pending: Deque[str] = deque()
        self._active_cmd: Optional[str] = None
        self._active_lines: List[str] = []
        self._last_prompt: str = "[/] >"

        self._seen_output = False

        # 输出静默超时：tree 这种不在末尾再打印提示符的命令靠它结束
        self._idle_timer = QTimer(self)
        self._idle_timer.setSingleShot(True)
        self._idle_timer.setInterval(1500)  # 首包等 1.5s（你也可以 1200~2000）
        self._idle_timer.start()
        self._idle_timer.timeout.connect(self._on_idle_timeout)

        self.start_backend()

    def start_backend(self):
        self.proc = subprocess.Popen(
            [self.exe_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding='utf-8',
            text=True,
            bufsize=1,
        )
        # 你已有 BackendReader：读 stdout 每行 emit
        self.reader = BackendReader(self.proc)
        self.reader.line.connect(self._on_line)
        self.reader.start()

    def send_command(self, cmd: str):
        cmd = cmd.strip()
        if not cmd:
            return
        if not self.proc or not self.proc.stdin:
            raise RuntimeError("后端未启动或 stdin 不可用")

        # 只入队，不要立刻写入（避免输出交错）
        self._pending.append(cmd)

        # 如果当前没有 active 命令，启动队首
        if self._active_cmd is None:
            self._start_next_command()
    
    def _start_next_command(self):
        """启动队首命令：真正写入 stdin，并清空收集器"""
        if not self._pending:
            self._active_cmd = None
            return

        self._active_cmd = self._pending[0]
        self._active_lines.clear()
        self._seen_output = False
        self._idle_timer.stop()

        # 真正写入 stdin
        self.proc.stdin.write(self._active_cmd + "\n")
        self.proc.stdin.flush()

        self._idle_timer.start()
    def _on_idle_timeout(self):
        # 只有“已经见过输出”且“之后一段时间没新输出”才结束
        print("[DEBUG] idle timeout fired")
        if self._active_cmd is None:
            return

        # 关键：还没见过任何输出 => 不能结束，继续等
        if not self._seen_output:
            self._idle_timer.start()   # 再等一个 interval
            return

        # 见过输出了，且静默超时 => 结束该命令
        self._finish_active()

    def _finish_active(self):
        # 弹出刚刚完成的命令
        cmd = self._pending.popleft() if self._pending else (self._active_cmd or "")
        res = CommandResult(cmd=cmd, lines=self._active_lines.copy(), prompt=self._last_prompt)
        self.cmd_finished.emit(res)

        # 清空并启动下一条
        self._active_lines.clear()
        self._seen_output = False
        self._active_cmd = None

        self._start_next_command()

    def _on_line(self, s: str):
        self.raw_line.emit(s)
        st = s.strip()

        # 1) 提示符：只记录，不作为结束条件（你们提示符在前！）
        if PROMPT_RE.search(st):
            self._last_prompt = st
            return

        # 2) 非提示符输出：归入当前 active 命令
        if self._active_cmd is not None:
            self._active_lines.append(s)

            if st:
                # ✅ 第一次看到输出：把“首包等待(长)”切换成“静默等待(短)”
                if not self._seen_output:
                    # 这里的 400 就是你原来的静默阈值（200~500都行）
                    self._idle_timer.setInterval(400)

                self._seen_output = True

                # ✅ 每来一行输出，就延后结束判定
                self._idle_timer.start()

    def close(self):
        try:
            if self.reader:
                self.reader.stop()
                self.reader.wait(500)
            if self.proc:
                self.proc.terminate()
        except Exception:
            pass

# -------------------- 解析器：尽力从文本里提取结构化信息 --------------------

def extract_ls_items(lines: List[str]) -> Tuple[List[str], List[Tuple[str, Optional[int], Optional[int]]], str]:
    dirs: List[str] = []
    files: List[Tuple[str, Optional[int], Optional[int]]] = []  # (name, start_block, size)
    cwd = ""
    mode = None

    for raw in lines:
        s = raw.strip()
        if not s:
            continue
        if PROMPT_RE.search(s):
            continue

        m = re.search(r"目录内容\s*\((.*?)\)", s)
        if m:
            cwd = m.group(1).strip()
            continue

        if s.startswith("目录:"):
            # 这行不要当子目录项
            continue

        if s == "[子目录]":
            mode = "DIR"; continue
        if s == "[文件]":
            mode = "FILE"; continue

        if set(s) <= {"-", "="}:
            continue

        if mode == "DIR":
            name = s.rstrip("/")
            if name and name not in dirs:
                dirs.append(name)
            continue

        if mode == "FILE":
            # 兼容： "test.txt (起始块：64，大小：128字节)" 或只有文件名
            m2 = re.match(r"([^\s]+)\s*(?:\(\s*起始块：(\d+)\s*，\s*大小：(\d+)\s*字节\s*\))?", s)
            if m2:
                fname = m2.group(1)
                sb = int(m2.group(2)) if m2.group(2) else None
                sz = int(m2.group(3)) if m2.group(3) else None
                files.append((fname, sb, sz))
            continue

    return dirs, files, cwd

def _depth_from_prefix(prefix: str) -> int:
    """
    你们的缩进是：
      "│  " （竖线+两个空格）表示一层
    所以 depth 直接数竖线/管道符最稳。
    """
    p = prefix.replace("┃", "│")
    return p.count("│") + p.count("|")

def parse_tree_to_items(lines: List[str]) -> QTreeWidgetItem:
    root = QTreeWidgetItem(["/"])
    root.setData(0, Qt.ItemDataRole.UserRole, 1)  # 根当目录
    stack: List[Tuple[int, QTreeWidgetItem]] = [(0, root)]  # (depth, item)
    any_node = False

    for raw in lines:
        s = raw.rstrip("\n")
        st = s.strip()
        if not st:
            continue
        if PROMPT_RE.search(st):
            continue

        # 跳过标题/说明
        if "完整目录树" in st or "完整目录结构" in st or "目录树" in st:
            continue

        # 根行 "/" 不作为子节点
        if st == "/":
            continue

        m = _BRANCH_RE.search(s)
        if not m:
            continue

        prefix = s[:m.start()]
        raw_name = s[m.end():].strip()
        if not raw_name:
            continue

        # ✅ 目录/文件判断
        is_dir = raw_name.endswith("/")
        name = raw_name.rstrip("/")

        # ✅ 深度判断（按你们的输出：│  表示一层）
        depth = _depth_from_prefix(prefix)

        # ✅ 找父节点：栈顶深度必须 <= 当前 depth
        while stack and stack[-1][0] > depth:
            stack.pop()
        parent_item = stack[-1][1] if stack else root

        item = QTreeWidgetItem([name])
        item.setData(0, Qt.ItemDataRole.UserRole, 1 if is_dir else 0)
        parent_item.addChild(item)

        # ✅ 子节点深度入栈（子层 = depth+1）
        stack.append((depth + 1, item))
        any_node = True

    # 兜底：如果啥也没解析出来，保证树不空
    if not any_node:
        root.addChild(QTreeWidgetItem(["(tree 解析失败：输出格式异常，请看左侧终端)"]))

    return root

def extract_root_children_from_tree(lines: List[str]) -> Tuple[List[str], List[str]]:
        """
        从 tree 输出中提取根目录 '/' 下的一级：dirs, files
        依赖 parse_tree_to_items 的逻辑类似，但这里只取 depth==0 的孩子。
        """
        dirs, files = [], []

        for raw in lines:
            s = raw.rstrip("\n")
            st = s.strip()
            if not st:
                continue
            if PROMPT_RE.search(st):
                continue
            if "完整目录树" in st or "完整目录结构" in st or "目录树" in st:
                continue
            if st == "/":
                continue

            m = _BRANCH_RE.search(s)
            if not m:
                continue

            prefix = s[:m.start()]
            name_raw = s[m.end():].strip()
            if not name_raw:
                continue

            depth = _depth_from_prefix(prefix)
            if depth != 0:
                continue  # 只取根下一层

            is_dir = name_raw.endswith("/")
            name = name_raw.rstrip("/")

            if is_dir:
                if name not in dirs:
                    dirs.append(name)
            else:
                if name not in files:
                    files.append(name)

        return dirs, files

class FileEditorDialog(QDialog):
    # mode: "rw" 读写；也可以做只读
    def __init__(self, path: str, parent=None):
        super().__init__(parent)
        self.path = path
        self.setWindowTitle(f"文件编辑：{path}")
        self.resize(900, 650)  # ✅ 弹框大一点

        self.view = QTextEdit()
        self.view.setReadOnly(True)

        self.edit = QTextEdit()
        self.edit.setPlaceholderText("在这里编辑要写入的内容...")

        self.btn_refresh = QPushButton("刷新读取")
        self.btn_save = QPushButton("保存写入")
        self.btn_close = QPushButton("关闭")

        # 底部按钮
        btn_row = QHBoxLayout()
        btn_row.addWidget(self.btn_refresh)
        btn_row.addWidget(self.btn_save)
        btn_row.addStretch(1)
        btn_row.addWidget(self.btn_close)

        lay = QVBoxLayout(self)
        lay.addWidget(QLabel("当前文件内容（只读）"))
        lay.addWidget(self.view, 2)
        lay.addWidget(QLabel("写入内容（可编辑）"))
        lay.addWidget(self.edit, 3)
        lay.addLayout(btn_row)

        self.btn_close.clicked.connect(self.reject)

    def set_view_text(self, txt: str):
        self.view.setPlainText(txt)

    def get_edit_text(self) -> str:
        return self.edit.toPlainText()
# -------------------- 主窗口 --------------------

class MainWindow(QMainWindow):
    def __init__(self, exe_path: str):
        super().__init__()
        self.setWindowTitle("OS课程设计前端（PyQt + stdin/stdout）")
        self.backend = None

        # 终端区
        self.terminal = QPlainTextEdit()
        self.terminal.setReadOnly(True)

        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("输入命令，例如：ls /  或  create_proc DIR_LS {\"path\":\"/\"}")
        self.send_btn = QPushButton("发送")
        self.send_btn.clicked.connect(self.on_send)
        self.cmd_input.returnPressed.connect(self.on_send)

        term_row = QHBoxLayout()
        term_row.addWidget(self.cmd_input, 1)
        term_row.addWidget(self.send_btn)

        term_box = QVBoxLayout()
        term_box.addWidget(QLabel("终端输出"))
        term_box.addWidget(self.terminal, 1)
        term_box.addLayout(term_row)

        term_widget = QWidget()
        term_widget.setLayout(term_box)

        # 左侧：目录树
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(["目录树"])
        self.tree.itemClicked.connect(self.on_tree_clicked)
        self._last_tree_root_children = {"dirs": [], "files": []}  # 用来缓存根目录的 tree 子项

        # 右侧：目录内容（子目录/文件）
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["类型", "名称", "起始块", "大小(字节)"])
        self.table.horizontalHeader().setStretchLastSection(True)

        # 快捷按钮
        self.btn_pwd = QPushButton("pwd")
        self.btn_ls = QPushButton("ls")
        self.btn_tree = QPushButton("tree")
        self.btn_help = QPushButton("help")
        self.btn_clear = QPushButton("clear输出")

        self.btn_pwd.clicked.connect(lambda: self.safe_send("pwd"))
        self.btn_ls.clicked.connect(lambda: self.safe_send("ls"))
        self.btn_tree.clicked.connect(lambda: self.safe_send("tree"))
        self.btn_help.clicked.connect(lambda: self.safe_send("help"))
        self.btn_clear.clicked.connect(self.terminal.clear)

        quick_row = QHBoxLayout()
        for b in [self.btn_pwd, self.btn_ls, self.btn_tree, self.btn_help, self.btn_clear]:
            quick_row.addWidget(b)
        self.btn_blocks = QPushButton("blocks")
        self.btn_stat = QPushButton("stat")
        self.btn_open_file = QPushButton("打开文件(读/写)")
        self.btn_open_file.clicked.connect(self.on_open_file_dialog)
        quick_row.addWidget(self.btn_open_file)

        self.btn_blocks.clicked.connect(self.on_blocks)
        self.btn_stat.clicked.connect(self.on_stat)

        quick_row.addWidget(self.btn_blocks)
        quick_row.addWidget(self.btn_stat)
        quick_widget = QWidget()
        quick_widget.setLayout(quick_row)

        # 调度面板
        sched_group = QGroupBox("调度器")
        self.btn_start_sched = QPushButton("start_scheduler")
        self.btn_run_sched = QPushButton("run_scheduler")
        self.btn_stop_scheduling = QPushButton("stop_scheduling")
        self.btn_stop_sched = QPushButton("stop_scheduler")
        self.btn_list_ready = QPushButton("list_ready")
        self.btn_list_procs = QPushButton("list_procs")

        self.btn_start_sched.clicked.connect(lambda: self.safe_send("start_scheduler"))
        self.btn_run_sched.clicked.connect(lambda: self.safe_send("run_scheduler"))
        self.btn_stop_scheduling.clicked.connect(lambda: self.safe_send("stop_scheduling"))
        self.btn_stop_sched.clicked.connect(lambda: self.safe_send("stop_scheduler"))
        self.btn_list_ready.clicked.connect(lambda: self.safe_send("list_ready"))
        self.btn_list_procs.clicked.connect(lambda: self.safe_send("list_procs"))

        sched_row1 = QHBoxLayout()
        for b in [self.btn_start_sched, self.btn_run_sched, self.btn_stop_scheduling, self.btn_stop_sched]:
            sched_row1.addWidget(b)
        sched_row2 = QHBoxLayout()
        for b in [self.btn_list_ready, self.btn_list_procs]:
            sched_row2.addWidget(b)

        sched_lay = QVBoxLayout()
        sched_lay.addLayout(sched_row1)
        sched_lay.addLayout(sched_row2)
        sched_group.setLayout(sched_lay)

        # 消息面板
        msg_group = QGroupBox("消息 IPC（recv_msg / send_msg）")
        self.recv_pid = QSpinBox()
        self.recv_pid.setRange(0, 10_000_000)
        self.recv_pid.setValue(0)
        self.btn_recv = QPushButton("recv_msg")
        self.btn_recv.clicked.connect(self.on_recv)

        self.send_pid = QSpinBox()
        self.send_pid.setRange(1, 10_000_000)
        self.send_text = QLineEdit()
        self.send_text.setPlaceholderText("要发给进程的内容（可用于演示IPC/控制）")
        self.btn_send_msg = QPushButton("send_msg")
        self.btn_send_msg.clicked.connect(self.on_send_msg)

        form = QFormLayout()
        recv_row = QHBoxLayout()
        recv_row.addWidget(self.recv_pid)
        recv_row.addWidget(self.btn_recv)
        recv_row_w = QWidget()
        recv_row_w.setLayout(recv_row)

        send_row = QHBoxLayout()
        send_row.addWidget(self.send_pid)
        send_row.addWidget(self.send_text, 1)
        send_row.addWidget(self.btn_send_msg)
        send_row_w = QWidget()
        send_row_w.setLayout(send_row)

        form.addRow("接收 PID（0=所有）", recv_row_w)
        form.addRow("发送 (PID + 内容)", send_row_w)
        msg_group.setLayout(form)

        # 右侧 Tab：文件浏览/调度/消息
        right_tabs = QTabWidget()

        browser = QWidget()
        browser_lay = QVBoxLayout(browser)
        browser_lay.addWidget(quick_widget)
        split_browser = QSplitter(Qt.Orientation.Horizontal)
        split_browser.addWidget(self.tree)
        split_browser.addWidget(self.table)
        split_browser.setStretchFactor(0, 1)
        split_browser.setStretchFactor(1, 2)
        browser_lay.addWidget(split_browser, 1)
        right_tabs.addTab(browser, "文件浏览")

        sched_tab = QWidget()
        sched_tab_lay = QVBoxLayout(sched_tab)
        sched_tab_lay.addWidget(sched_group)
        right_tabs.addTab(sched_tab, "调度")

        msg_tab = QWidget()
        msg_tab_lay = QVBoxLayout(msg_tab)
        msg_tab_lay.addWidget(msg_group)
        right_tabs.addTab(msg_tab, "消息")

        # ===== 批量任务 Tab =====
        self.batch_edit = QTextEdit()
        self.batch_edit.setPlaceholderText(
            "每行一条命令（建议只写 create_proc ...）\n"
            "例如：\n"
            "create_proc DIR_MKDIR {\"path\":\"/demo\"}\n"
            "create_proc FILE_TOUCH {\"path\":\"/demo/a.txt\"}\n"
        )
        self.batch_edit.setPlainText(
            "create_proc DIR_MKDIR {\"path\":\"/demo\"}\n"
            "create_proc FILE_TOUCH {\"path\":\"/demo/test.txt\"}\n"
            "create_proc FILE_WRITE {\"path\":\"/demo/test.txt\",\"content\":\"hello\"}\n"
            "create_proc FILE_READ {\"path\":\"/demo/test.txt\"}\n"
        )

        self.btn_batch_send = QPushButton("批量发送（逐行）")
        self.btn_batch_send_create_only = QPushButton("仅发送 create_proc 行")
        self.btn_batch_clear = QPushButton("清空")

        # 绑定按钮（下面会写 _batch_send 函数）
        self.btn_batch_send.clicked.connect(lambda: self._batch_send(only_create=False))
        self.btn_batch_send_create_only.clicked.connect(lambda: self._batch_send(only_create=True))
        self.btn_batch_clear.clicked.connect(lambda: self.batch_edit.setPlainText(""))

        batch_tab = QWidget()
        batch_lay = QVBoxLayout(batch_tab)

        row = QHBoxLayout()
        row.addWidget(self.btn_batch_send)
        row.addWidget(self.btn_batch_send_create_only)
        row.addWidget(self.btn_batch_clear)

        batch_lay.addLayout(row)
        batch_lay.addWidget(self.batch_edit, 1)

        right_tabs.addTab(batch_tab, "批量 create_proc")

        # 主分割：左（终端）+ 右（结构化面板）
        main_split = QSplitter(Qt.Orientation.Horizontal)
        main_split.addWidget(term_widget)
        main_split.addWidget(right_tabs)
        main_split.setStretchFactor(0, 2)
        main_split.setStretchFactor(1, 3)

        container = QWidget()
        container_lay = QVBoxLayout(container)
        container_lay.addWidget(main_split, 1)
        self.setCentralWidget(container)

        # ========== 进程/消息面板 ==========
        self.ready_table = QTableWidget(0, 2)
        self.ready_table.setHorizontalHeaderLabels(["PID/序号", "描述"])
        self.ready_table.horizontalHeader().setStretchLastSection(True)

        self.msg_table = QTableWidget(0, 2)
        self.msg_table.setHorizontalHeaderLabels(["PID", "消息/结果"])
        self.msg_table.horizontalHeader().setStretchLastSection(True)

        self.btn_ready_refresh2 = QPushButton("刷新就绪队列(list_ready)")
        self.btn_msgs_refresh = QPushButton("接收消息(recv_msg 0)")
        self.btn_ready_refresh2.clicked.connect(lambda: self.safe_send("list_ready"))
        self.btn_msgs_refresh.clicked.connect(lambda: self.safe_send("recv_msg 0"))

        proc_tab = QWidget()
        proc_lay = QVBoxLayout(proc_tab)

        row = QHBoxLayout()
        row.addWidget(self.btn_ready_refresh2)
        row.addWidget(self.btn_msgs_refresh)
        proc_lay.addLayout(row)

        proc_lay.addWidget(QLabel("就绪队列（READY）"))
        proc_lay.addWidget(self.ready_table, 1)

        proc_lay.addWidget(QLabel("执行结果/邮箱消息（FINISHED / IPC）"))
        proc_lay.addWidget(self.msg_table, 1)

        right_tabs.addTab(proc_tab, "进程/消息")

        # 启动后端
        try:
            self.backend = BackendController(exe_path)
        except Exception as e:
            QMessageBox.critical(self, "启动失败", str(e))
            raise

        self.backend.raw_line.connect(self.append_terminal_line)
        self.backend.cmd_finished.connect(self.on_cmd_finished)

        # 提示：启动后先拉一次 tree/ls，让面板有内容
        self.append_terminal_line("[FRONTEND] 已启动后端。建议先点 tree / ls 试试。")
        self._file_dlg: Optional[FileEditorDialog] = None
        self._waiting_read_for_path: Optional[str] = None
        self._suppress_terminal_output = False
    def refresh_ready_table(self, lines):
        # 简单：每行当作一个条目（后端输出格式不确定，先保证可用）
        rows = [ln.strip() for ln in lines if ln.strip() and not PROMPT_RE.search(ln.strip())]

        self.ready_table.setRowCount(0)
        for i, s in enumerate(rows, 1):
            r = self.ready_table.rowCount()
            self.ready_table.insertRow(r)
            self.ready_table.setItem(r, 0, QTableWidgetItem(str(i)))
            self.ready_table.setItem(r, 1, QTableWidgetItem(s))

    def refresh_msg_table(self, lines):
        # recv_msg 的输出通常包含 PID 或类似标识；这里做宽松解析：能提取 PID 就提取，提取不到就放 PID=?
        rows = [ln.rstrip("\n") for ln in lines if ln.strip() and not PROMPT_RE.search(ln.strip())]

        parsed = []
        for s in rows:
            # 常见形式：[PID=3] xxx 或 PID=3 xxx
            m = re.search(r"PID\s*=\s*(\d+)", s)
            if not m:
                m = re.search(r"\[PID\s*=\s*(\d+)\]", s)
            pid = m.group(1) if m else "?"
            parsed.append((pid, s.strip()))

        self.msg_table.setRowCount(0)
        for pid, msg in parsed:
            r = self.msg_table.rowCount()
            self.msg_table.insertRow(r)
            self.msg_table.setItem(r, 0, QTableWidgetItem(pid))
            self.msg_table.setItem(r, 1, QTableWidgetItem(msg))

    def on_open_file_dialog(self):
        path = self._get_selected_file_path()
        if not path:
            self.append_terminal_line("[FRONTEND] 请在文件表中选择一个文件。")
            return

        # 如果已经打开同一个文件弹框，就复用
        if self._file_dlg is None or self._file_dlg.path != path:
            self._file_dlg = FileEditorDialog(path, parent=self)
            self._file_dlg.btn_refresh.clicked.connect(self._dlg_refresh_read)
            self._file_dlg.btn_save.clicked.connect(self._dlg_save_write)
            self._file_dlg.finished.connect(lambda _: self._on_dlg_closed())

        self._file_dlg.show()
        self._file_dlg.raise_()
        self._file_dlg.activateWindow()

        # 打开就先读一次
        self._dlg_refresh_read()

    def _get_selected_file_path(self) -> Optional[str]:
        """
        从文件表格中获取当前选中的文件的完整路径
        """
        row = self.table.currentRow()
        if row < 0:
            return None

        t_item = self.table.item(row, 0)
        n_item = self.table.item(row, 1)
        if not t_item or not n_item:
            return None

        if t_item.text() != "FILE":
            return None

        fname = n_item.text()

        tip = self.table.toolTip()
        if tip.startswith("当前目录："):
            cwd = tip.replace("当前目录：", "").strip()
            return f"{cwd.rstrip('/')}/{fname}"

        return fname

    def on_blocks(self):
        path = self._get_selected_file_path()
        if not path:
            self.append_terminal_line("[FRONTEND] 请在文件表中选择一个文件。")
            return
        self.safe_send(f"blocks {path}")

    def on_stat(self):
        path = self._get_selected_file_path()
        if not path:
            self.append_terminal_line("[FRONTEND] 请在文件表中选择一个文件。")
            return
        self.safe_send(f"stat {path}")

    def on_file_read(self):
        path = self._get_selected_file_path()
        if not path:
            self.append_terminal_line("[FRONTEND] 请选择文件。")
            return
        self.safe_send(f"read {path}")

    def on_file_cat(self):
        path = self._get_selected_file_path()
        if not path:
            self.append_terminal_line("[FRONTEND] 请选择文件。")
            return
        self.safe_send(f"cat {path} 0")

    def on_file_edit(self):
        path = self._get_selected_file_path()
        content = self.file_op_input.text().strip()
        if not path or not content:
            self.append_terminal_line("[FRONTEND] 请选择文件并输入内容。")
            return
        self.safe_send(f"edit {path} 0 {content}")

    def _on_dlg_closed(self):
        self._file_dlg = None
        self._waiting_read_for_path = None
        self._suppress_terminal_output = False

    def _dlg_refresh_read(self):
        if not self._file_dlg:
            return
        path = self._file_dlg.path
        self._waiting_read_for_path = path

        # 抑制终端刷屏
        self._suppress_terminal_output = True

        # ✅ 直接读文件
        self.safe_send(f"read {path}")

    def _dlg_save_write(self):
        if not self._file_dlg:
            return
        path = self._file_dlg.path
        content = self._file_dlg.get_edit_text()

        # ⚠️ 防止换行导致后端 split 崩溃
        safe_content = content.replace("\r\n", "\\n").replace("\n", "\\n")

        self.safe_send(f"write {path} {safe_content}")

        # 写完立刻刷新
        self._dlg_refresh_read()

    def _extract_file_content_from_lines(self, lines: List[str]) -> str:
        kept = []
        for ln in lines:
            st = ln.strip()
            if not st:
                continue
            if PROMPT_RE.search(st):
                continue
            # 你后端可能会有这些行，不想显示在弹框里就过滤
            if st.startswith("[DEBUG]") or st.startswith("[INFO]") or st.startswith("[WARNING]"):
                continue
            kept.append(ln.rstrip("\n"))
        return "\n".join(kept).strip()

    def _batch_send(self, only_create: bool = False):
        """
        从多行文本框里读取命令，逐行发送给后端。
        only_create=True 时，只发送以 create_proc 开头的行。
        """
        text = self.batch_edit.toPlainText()
        lines = text.splitlines()

        cmds = []
        for ln in lines:
            s = ln.strip()
            if not s:
                continue
            if s.startswith("#"):
                continue
            if only_create and not s.startswith("create_proc"):
                continue
            cmds.append(s)

        if not cmds:
            self.append_terminal_line("[FRONTEND] 批量区没有可发送的命令。")
            return

        self.append_terminal_line(f"[FRONTEND] 批量发送 {len(cmds)} 条命令...")
        for c in cmds:
            self.safe_send(c)

    def append_terminal_line(self, s: str):
        # ✅ 读文件时抑制终端刷屏（避免内容出现在终端）
        if getattr(self, "_suppress_terminal_output", False):
            # 仍允许 DEBUG 或 FRONTEND 提示通过（可选）
            if s.startswith("[FRONTEND]") or s.startswith("[DEBUG]"):
                self.terminal.appendPlainText(s)
            return
        self.terminal.appendPlainText(s)

    def safe_send(self, cmd: str):
        try:
            self.append_terminal_line(f">>> {cmd}")
            self.backend.send_command(cmd)
        except Exception as e:
            self.append_terminal_line(f"[FRONTEND] 发送失败：{e}")

    def on_send(self):
        cmd = self.cmd_input.text().strip()
        if not cmd:
            return
        self.cmd_input.clear()
        self.safe_send(cmd)

    def on_recv(self):
        pid = self.recv_pid.value()
        self.safe_send(f"recv_msg {pid}")

    def on_send_msg(self):
        pid = self.send_pid.value()
        text = self.send_text.text().strip()
        if not text:
            self.append_terminal_line("[FRONTEND] send_msg 内容不能为空。")
            return
        # 注意：为了安全，简单加引号包起来（后端若按整行解析更稳）
        self.safe_send(f"send_msg {pid} {text}")

    def on_cmd_finished(self, res: CommandResult):
        self.append_terminal_line(
            f"[DEBUG] cmd_finished: '{res.cmd}', lines={len(res.lines)}"
        )
        if res.lines:
            self.append_terminal_line(f"[DEBUG] first line: {res.lines[0]}")

        cmd = res.cmd.strip()
        cmd0 = cmd.split()[0] if cmd else ""

        # ✅ 弹框触发的 read：把内容放到弹框里，不走下面分发
        if (cmd0 == "read"
            and self._waiting_read_for_path is not None
            and self._file_dlg is not None):
            content = self._extract_file_content_from_lines(res.lines)
            self._file_dlg.set_view_text(content)

            self._waiting_read_for_path = None
            self._suppress_terminal_output = False
            return
        
        # ---------- ① 正常命令分发 ----------
        if cmd0 == "ls":
            self.refresh_ls(res.lines)
        elif cmd0 == "tree":
            self.refresh_tree(res.lines)
        elif cmd0 == "list_ready":
            self.refresh_ready_table(res.lines)
        elif cmd0 == "recv_msg":
            self.refresh_msg_table(res.lines)

    def refresh_tree(self, lines: List[str]):
        self.tree.clear()
        root_item = parse_tree_to_items(lines)
        self.tree.addTopLevelItem(root_item)
        self.tree.expandAll()

    def refresh_ls(self, lines: List[str]):
        dirs, files, cwd = extract_ls_items(lines)

        self.table.setRowCount(0)

        # 目录
        for d in dirs:
            r = self.table.rowCount()
            self.table.insertRow(r)
            self.table.setItem(r, 0, QTableWidgetItem("DIR"))
            self.table.setItem(r, 1, QTableWidgetItem(d))
            self.table.setItem(r, 2, QTableWidgetItem(""))
            self.table.setItem(r, 3, QTableWidgetItem(""))

        # 文件
        for fname, sb, sz in files:
            r = self.table.rowCount()
            self.table.insertRow(r)
            self.table.setItem(r, 0, QTableWidgetItem("FILE"))
            self.table.setItem(r, 1, QTableWidgetItem(fname))
            self.table.setItem(r, 2, QTableWidgetItem("" if sb is None else str(sb)))
            self.table.setItem(r, 3, QTableWidgetItem("" if sz is None else str(sz)))

        if cwd:
            self.table.setToolTip(f"当前目录：{cwd}")

    def on_tree_clicked(self, item: QTreeWidgetItem, col: int):
        # 拼路径
        parts = []
        cur = item
        while cur is not None:
            txt = cur.text(0).strip()
            if txt and txt != "/":
                parts.append(txt.strip("/"))
            cur = cur.parent()
        parts.reverse()

        node_path = "/" + "/".join(parts) if parts else "/"
        is_dir = (item.data(0, Qt.ItemDataRole.UserRole) == 1)

        # ===== 点击目录 =====
        if is_dir:
            if node_path == "/":
                # ❗ 根目录：只 cd，不 ls，避免重复 warning
                self.safe_send("cd /")
                self.append_terminal_line("⚠️  目录为空或路径不存在：/")
                return
            else:
                self.safe_send(f"cd {node_path}")
                self.safe_send("ls")

        # ===== 点击文件 =====
        else:
            parent_path = "/" + "/".join(parts[:-1]) if len(parts) > 1 else "/"

            if parent_path != "/":
                self.safe_send(f"cd {parent_path}")
                self.safe_send("ls")
            else:
                self.safe_send("cd /")
                self.append_terminal_line("⚠️  目录为空或路径不存在：/")

            self.safe_send(f"stat {node_path}")

    def closeEvent(self, event):
        try:
            if self.backend:
                self.backend.close()
        except Exception:
            pass
        event.accept()


def main():
    app = QApplication(sys.argv)
    w = MainWindow(BACKEND_EXE)
    w.resize(1200, 750)
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
