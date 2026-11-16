"""
Distributed Lock Manager (DLM) — Ultimate Module-by-Module Layman's Master Textbook
Explains EVERY module in plain English, with real-world analogies, internal code mechanics,
and exact evaluator defense Q&As for each subsystem.
"""

import os
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, HRFlowable
)
from reportlab.pdfgen import canvas

class NumberedCanvas(canvas.Canvas):
    """Two-pass canvas generating running header and 'Page X of Y' footer."""
    def __init__(self, *args, **kwargs):
        super(NumberedCanvas, self).__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_decorations(num_pages)
            super(NumberedCanvas, self).showPage()
        super(NumberedCanvas, self).save()

    def draw_page_decorations(self, page_count):
        if self._pageNumber == 1:
            return  # Skip cover page

        self.saveState()
        self.setFont("Helvetica-Bold", 8)
        self.setFillColor(colors.HexColor("#475569"))

        # Header
        self.drawString(54, 11 * inch - 36, "DLM Master Guide: Complete Module-by-Module Explanation")
        self.setStrokeColor(colors.HexColor("#CBD5E1"))
        self.setLineWidth(0.75)
        self.line(54, 11 * inch - 42, 8.5 * inch - 54, 11 * inch - 42)

        # Footer
        self.line(54, 46, 8.5 * inch - 54, 46)
        self.setFont("Helvetica", 8)
        self.drawString(54, 32, "Operating Systems & Distributed DBMS — Practical Implementation Textbook")
        page_str = f"Page {self._pageNumber} of {page_count}"
        self.drawRightString(8.5 * inch - 54, 32, page_str)
        self.restoreState()


def build_layman_pdf(filename="Distributed_Lock_Manager_Laymans_Guide.pdf"):
    doc = SimpleDocTemplate(
        filename,
        pagesize=letter,
        leftMargin=54,
        rightMargin=54,
        topMargin=54,
        bottomMargin=54
    )

    styles = getSampleStyleSheet()

    # Typography styles
    styles.add(ParagraphStyle(
        name='CoverBadge',
        fontName='Helvetica-Bold',
        fontSize=11,
        leading=14,
        textColor=colors.HexColor("#4338CA"),
        alignment=1,
        spaceAfter=15
    ))

    styles.add(ParagraphStyle(
        name='CoverTitle',
        fontName='Helvetica-Bold',
        fontSize=24,
        leading=30,
        textColor=colors.HexColor("#0F172A"),
        alignment=1,
        spaceAfter=12
    ))

    styles.add(ParagraphStyle(
        name='CoverSubtitle',
        fontName='Helvetica',
        fontSize=11.5,
        leading=16,
        textColor=colors.HexColor("#475569"),
        alignment=1,
        spaceAfter=25
    ))

    styles.add(ParagraphStyle(
        name='SectionHeader',
        fontName='Helvetica-Bold',
        fontSize=14,
        leading=18,
        textColor=colors.HexColor("#0F172A"),
        spaceBefore=14,
        spaceAfter=6,
        keepWithNext=True
    ))

    styles.add(ParagraphStyle(
        name='SubSectionHeader',
        fontName='Helvetica-Bold',
        fontSize=11,
        leading=14.5,
        textColor=colors.HexColor("#1E3A8A"),
        spaceBefore=10,
        spaceAfter=4,
        keepWithNext=True
    ))

    styles.add(ParagraphStyle(
        name='BodyCustom',
        fontName='Helvetica',
        fontSize=9,
        leading=13.5,
        textColor=colors.HexColor("#1E293B"),
        spaceAfter=5
    ))

    styles.add(ParagraphStyle(
        name='BulletCustom',
        fontName='Helvetica',
        fontSize=9,
        leading=13,
        textColor=colors.HexColor("#1E293B"),
        leftIndent=12,
        firstLineIndent=-8,
        spaceAfter=4
    ))

    styles.add(ParagraphStyle(
        name='AnalogyBox',
        fontName='Helvetica',
        fontSize=8.5,
        leading=12.5,
        textColor=colors.HexColor("#065F46")
    ))

    styles.add(ParagraphStyle(
        name='CodeSnippet',
        fontName='Courier',
        fontSize=7.5,
        leading=10,
        textColor=colors.HexColor("#F8FAFC"),
        spaceAfter=3
    ))

    styles.add(ParagraphStyle(
        name='TableHead',
        fontName='Helvetica-Bold',
        fontSize=8,
        leading=10.5,
        textColor=colors.HexColor("#0F172A")
    ))

    styles.add(ParagraphStyle(
        name='TableCell',
        fontName='Helvetica',
        fontSize=7.8,
        leading=10.5,
        textColor=colors.HexColor("#1E293B")
    ))

    story = []

    # =========================================================================
    # COVER PAGE
    # =========================================================================
    story.append(Spacer(1, 30))
    story.append(Paragraph("★ THE COMPLETE MODULE-BY-MODULE MASTER TEXTBOOK ★", styles['CoverBadge']))
    story.append(Paragraph("Distributed Lock Manager (DLM)<br/>Every Module Explained in Plain English", styles['CoverTitle']))
    story.append(Paragraph(
        "A Comprehensive, Layman-Friendly Guide to All 10 Modules in the System: "
        "LockManager, NetworkManager, HeartbeatManager, DeadlockDetector, TransactionManager, "
        "TransactionLog (WAL), TwoPCCoordinator, Node Orchestrator, Interactive UI, and Test Suite.",
        styles['CoverSubtitle']
    ))
    story.append(Spacer(1, 15))

    meta_grid = [
        [Paragraph("<b>Target Audience:</b> Students, Evaluators, Software Engineers", styles['TableCell']),
         Paragraph("<b>Tech Stack:</b> C++17, TCP Sockets, Winsock2, HTML5 Canvas", styles['TableCell'])],
        [Paragraph("<b>Author & Tech Lead:</b> Krishna (Member 1 — Core Architect)", styles['TableCell']),
         Paragraph("<b>Verification Status:</b> 17/17 Tests Passing (100% Success)", styles['TableCell'])],
        [Paragraph("<b>Concurrency Model:</b> Strict Two-Phase Locking (SS2PL)", styles['TableCell']),
         Paragraph("<b>Consensus Protocol:</b> Two-Phase Commit (2PC)", styles['TableCell'])],
        [Paragraph("<b>Deadlock Algorithm:</b> Tarjan 3-Color DFS Cycle Detection", styles['TableCell']),
         Paragraph("<b>Interactive UI:</b> Single-Page Visual Cluster Dashboard", styles['TableCell'])]
    ]
    t_meta = Table(meta_grid, colWidths=[240, 240])
    t_meta.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F8FAFC")),
        ('BOX', (0,0), (-1,-1), 1.5, colors.HexColor("#3B82F6")),
        ('INNERGRID', (0,0), (-1,-1), 0.5, colors.HexColor("#CBD5E1")),
        ('TOPPADDING', (0,0), (-1,-1), 6),
        ('BOTTOMPADDING', (0,0), (-1,-1), 6),
        ('LEFTPADDING', (0,0), (-1,-1), 8),
        ('RIGHTPADDING', (0,0), (-1,-1), 8),
    ]))
    story.append(t_meta)

    story.append(Spacer(1, 30))
    key_box = [
        [Paragraph("<b>How to Use This Textbook:</b> Each module is explained in 5 distinct sections: "
                   "<b>1) What it does in 1 sentence</b>, "
                   "<b>2) The Everyday Layman Analogy</b>, "
                   "<b>3) Internal Code Mechanics & Key Methods</b>, "
                   "<b>4) Exact Source Files</b>, and "
                   "<b>5) Top 3 Questions an Evaluator Will Ask & Your Winning Answers</b>. "
                   "Read this and you will be able to answer any question raised by anyone!", styles['TableCell'])]
    ]
    t_key = Table(key_box, colWidths=[480])
    t_key.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#EFF6FF")),
        ('LINEBEFORE', (0,0), (0,-1), 4, colors.HexColor("#2563EB")),
        ('TOPPADDING', (0,0), (-1,-1), 8),
        ('BOTTOMPADDING', (0,0), (-1,-1), 8),
        ('LEFTPADDING', (0,0), (-1,-1), 10),
        ('RIGHTPADDING', (0,0), (-1,-1), 10),
    ]))
    story.append(t_key)
    story.append(PageBreak())

    # =========================================================================
    # MODULE 1: COMMON (TYPES & LOGGER)
    # =========================================================================
    story.append(Paragraph("Module 1: Common Subsystem (Types & Logger)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Provides universal data types (Transaction IDs, Lock Modes, Node IDs) and a thread-safe, microsecond-accurate logger used across all subsystems.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Imagine a construction site where workers speak different languages. Before building anything, the project manager gives everyone a shared dictionary of standard terms (e.g. 'Blueprints', 'Danger', 'Supervisor') and a secure walkie-talkie channel where only one person can speak at a time without static.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>Types.h</code>: Defines strong C++ types: <code>TxnId = uint64_t</code>, <code>NodeId = uint32_t</code>, <code>ResourceId = std::string</code>, and enums: <code>LockMode { SHARED, EXCLUSIVE }</code>, <code>TxnState { ACTIVE, PREPARING, COMMITTED, ABORTED }</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <code>Logger.cpp</code>: Uses a <code>std::mutex</code> to prevent garbled console text when 10 threads print simultaneously. Automatically attaches timestamps (<code>[HH:MM:SS.mmm]</code>), thread IDs, and log levels. We renamed <code>ERROR</code> to <code>LogLevel::ERR</code> to avoid colliding with a nasty Windows macro defined in <code>wingdi.h</code>.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/common/Types.h</code>, <code>include/dlm/common/Logger.h</code>, <code>src/common/Logger.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m1_qa = [
        [Paragraph("<b>Q: Why use uint64_t for TxnId?</b><br/><i>A: It provides $2^{64}$ unique IDs, ensuring transaction numbers never wrap around or collide even under millions of transactions per second.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: How is Logger thread-safe?</b><br/><i>A: Every log call acquires an internal std::mutex before writing to std::cout or disk, ensuring logs never interleave or corrupt in memory.</i>", styles['TableCell'])]
    ]
    t_m1 = Table(m1_qa, colWidths=[480])
    t_m1.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m1)
    story.append(Spacer(1, 10))

    # =========================================================================
    # MODULE 2: LOCK MANAGER (THE ENGINE)
    # =========================================================================
    story.append(Paragraph("Module 2: Lock Manager (Core Concurrency Engine)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Controls who gets to read or write database rows, puts conflicting transactions to sleep on condition variables, and wakes them up when locks are released.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("The university library dictionary table: Multiple students can look at the book at the same time (Shared Read Lock). But if one student wants to write notes on the page with a pen (Exclusive Write Lock), everyone else must wait in a polite line outside until he closes the book.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics & Key Methods:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>acquireLock(txnId, resId, mode, timeout)</code>: Checks lock compatibility. If granted, inserts into <code>lockTable_</code>. If contended, adds the transaction to <code>waiters_</code> queue and puts the thread to sleep using <code>cv_.wait_for()</code>. Note: <code>std::unique_lock</code> releases the mutex while sleeping!", styles['BulletCustom']))
    story.append(Paragraph("• <code>releaseLock(txnId, resId)</code>: Erases owner, switches mode to SHARED if other owners remain, and calls <code>cv_.notify_all()</code> to wake waiting threads.", styles['BulletCustom']))
    story.append(Paragraph("• <code>releaseAllLocks(txnId)</code>: Bulk lock eviction when a transaction commits or aborts. Looks up all resources held by that transaction in <code>txnLocks_[txnId]</code> and frees them atomically.", styles['BulletCustom']))
    story.append(Paragraph("• <code>getWaitForEdges()</code>: Iterates <code>waiters_</code> and pairs each waiting transaction with active holders to build directed dependency edges for deadlock detection.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/lock_manager/LockManager.h</code>, <code>src/lock_manager/LockManager.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m2_qa = [
        [Paragraph("<b>Q: How do waiting threads wait without wasting 100% CPU?</b><br/><i>A: We use std::condition_variable. The operating system kernel puts the thread into an unscheduled sleep state until cv_.notify_all() wakes it up.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: What critical bug did you fix in acquireLock?</b><br/><i>A: When a transaction was aborted while sleeping, its waiter entry was deleted, leaving auto& waitQueue referencing freed memory (dangling pointer). We refactored it to look up the queue dynamically under lock, preventing access violations.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: What is Strict 2PL?</b><br/><i>A: Strict Two-Phase Locking means all exclusive locks must be held until the very end of the transaction (commit or abort), completely preventing cascading rollbacks.</i>", styles['TableCell'])]
    ]
    t_m2 = Table(m2_qa, colWidths=[480])
    t_m2.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m2)
    story.append(PageBreak())

    # =========================================================================
    # MODULE 3: NETWORK & IPC LAYER
    # =========================================================================
    story.append(Paragraph("Module 3: Network & Inter-Process Communication (IPC)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Connects physical nodes over standard TCP/IP sockets so they can request locks, vote on commits, and send heartbeats across the network.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("A postal service with standardized envelopes. Every letter has a 4-byte stamp on the outside telling the mailman exactly how long the letter is, followed by a pipe-separated form inside with Sender ID, Receiver ID, and Message Type.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <b>Length-Prefixed Framing:</b> TCP is a stream protocol with no inherent message boundaries. We prepend every message with a 4-byte network-byte-order (Big Endian) integer specifying payload length. The receiver reads 4 bytes, discovers size $N$, then calls <code>recv(sock, buf, N, MSG_WAITALL)</code> to prevent packet fragmentation.", styles['BulletCustom']))
    story.append(Paragraph("• <code>Message.cpp</code>: Serializes 12 message types (<code>LOCK_REQUEST</code>, <code>PREPARE</code>, <code>VOTE_YES</code>, <code>COMMIT</code>, <code>WFG_REQUEST</code>, etc.) into a clean pipe string: <code>TYPE|SENDER|RECEIVER|TXN|RES|MODE|PAYLOAD</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <code>NetworkManager.cpp</code>: Manages the TCP server lifecycle: <code>WSAStartup</code> on Windows, <code>socket()</code>, <code>::bind()</code> (scoped to avoid clash with <code>std::bind</code>), <code>listen()</code>, and an <code>acceptLoop()</code> thread that spawns worker threads per connection.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/network/Message.h</code>, <code>src/network/Message.cpp</code>, <code>NetworkManager.h / .cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m3_qa = [
        [Paragraph("<b>Q: How do you solve TCP stream fragmentation / concatenation?</b><br/><i>A: We use a 4-byte length prefix. The receiving socket reads exactly 4 bytes first, extracts payload length N with ntohl(), and loops until all N bytes arrive before deserializing.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: Why use raw sockets instead of gRPC or Boost?</b><br/><i>A: Raw sockets have zero third-party dependencies, compile cleanly out of the box on both Windows and Linux, and demonstrate direct operating system network socket programming.</i>", styles['TableCell'])]
    ]
    t_m3 = Table(m3_qa, colWidths=[480])
    t_m3.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m3)
    story.append(Spacer(1, 10))

    # =========================================================================
    # MODULE 4: HEARTBEAT & FAILURE DETECTION
    # =========================================================================
    story.append(Paragraph("Module 4: Heartbeat & Node Failure Detection", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Monitors the physical health of all cluster nodes and immediately frees up locks if a machine crashes or suffers a network blackout.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("A safety tether on a jet ski or treadmill. While you are riding, the key is attached to your wrist. If you fall off, the tether pulls out, the engine shuts off immediately, and safety brakes engage to prevent a collision.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <b>Broadcaster Thread:</b> Wakes up every 500ms and sends a lightweight <code>HEARTBEAT</code> packet to all known cluster peers.", styles['BulletCustom']))
    story.append(Paragraph("• <b>Liveness Tracker:</b> Upon receiving a heartbeat from Node $K$, records the timestamp in <code>lastHeartbeat_[nodeId] = now</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <b>Timeout Checker:</b> Runs every 1000ms. If <code>now - lastHeartbeat_[K] > 2000ms</code>, Node $K$ is officially declared DEAD. The system fires <code>onNodeFailure(K)</code> which evicts all locks held by transactions that originated on the dead node.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/network/HeartbeatManager.h</code>, <code>src/network/HeartbeatManager.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m4_qa = [
        [Paragraph("<b>Q: What happens if a node crashes while holding exclusive locks?</b><br/><i>A: Without heartbeats, the cluster would deadlock forever. With HeartbeatManager, after 2 seconds of silence, the node is marked DEAD and its locks are automatically evicted.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: How do you choose the timeout threshold?</b><br/><i>A: The heartbeat interval is 500ms and timeout is 2000ms (4 missed beats). This prevents transient network jitter from causing false-positive node evictions.</i>", styles['TableCell'])]
    ]
    t_m4 = Table(m4_qa, colWidths=[480])
    t_m4.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m4)
    story.append(PageBreak())

    # =========================================================================
    # MODULE 5: DEADLOCK DETECTION (GRAPH THEORY)
    # =========================================================================
    story.append(Paragraph("Module 5: Deadlock Detection & Graph Cycle Resolver", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Builds a directed dependency map of who is waiting for whom, detects circular loops using graph algorithms, and automatically aborts the youngest transaction to break the jam.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Two cars meeting face-to-face on a narrow one-lane bridge over a river. Car A won't reverse until Car B reverses; Car B won't reverse until Car A reverses. The traffic police arrives, inspects the situation, and tells the car that arrived most recently (Car B) to back up off the bridge so traffic can flow.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <b>Local + Global Graph Merge:</b> Every 2 seconds, <code>DeadlockDetector</code> asks local <code>LockManager</code> for wait edges and queries peer nodes over TCP (<code>WFG_REQUEST</code> $\to$ <code>WFG_RESPONSE</code>). Merges all edges into an adjacency list <code>adj[waiter] = [holders]</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <b>Tarjan 3-Color DFS:</b> Explores the graph. Colors nodes: WHITE (0 = unvisited), GRAY (1 = currently on recursion stack), and BLACK (2 = finished). If an arrow points to a GRAY node, a back-edge is found, proving a cycle exists!", styles['BulletCustom']))
    story.append(Paragraph("• <b>Youngest Victim Selection:</b> Given cycle $[T_1, T_4, T_8]$, picks <code>victim = max_element(...)</code> ($T_8$). Sends an abort signal to $T_8$, releasing its locks and allowing $T_1$ and $T_4$ to proceed without starvation.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/deadlock/DeadlockDetector.h</code>, <code>src/deadlock/DeadlockDetector.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m5_qa = [
        [Paragraph("<b>Q: What are the 4 Coffman conditions for a deadlock?</b><br/><i>A: Mutual Exclusion, Hold & Wait, No Preemption, and Circular Wait. In Strict 2PL databases, the first 3 are mandatory, so our detector detects and breaks Circular Wait.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: Why abort the youngest transaction instead of the oldest?</b><br/><i>A: 1) Minimizes wasted work: older transactions have held locks longer and done more work. 2) Prevents starvation: older transactions keep their low IDs, guaranteeing they will eventually become the oldest in any conflict and finish.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: What is the difference between local and global deadlocks?</b><br/><i>A: A local deadlock happens on a single machine. A global deadlock crosses machines: Node 1 has T1 waiting for T2; Node 2 has T2 waiting for T1. Neither node sees a cycle locally until the global graph merger combines their edges!</i>", styles['TableCell'])]
    ]
    t_m5 = Table(m5_qa, colWidths=[480])
    t_m5.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m5)
    story.append(Spacer(1, 10))

    # =========================================================================
    # MODULE 6: TRANSACTION MANAGEMENT & WAL
    # =========================================================================
    story.append(Paragraph("Module 6: Transaction Management & Write-Ahead Logging (WAL)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Manages transaction lifecycles (Begin, Lock, Commit, Abort) and writes an append-only log to disk before changing data so nothing is lost if power fails.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("An accountant's physical ledger. Before moving $10,000 between bank accounts, the accountant writes in his leather-bound diary with pen: 'Entry #402: Transferring $10K from Account A to B'. If a blackout occurs mid-transfer, he opens the diary upon waking up to see exactly where he left off.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>Transaction.cpp</code>: Tracks transaction ID, state machine (<code>ACTIVE</code>, <code>PREPARING</code>, <code>COMMITTED</code>, <code>ABORTED</code>), and a set of <code>lockedResources_</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <code>TransactionLog.cpp</code>: Appends formatted pipe records to <code>logs/node_*_wal.log</code>: <code>TYPE|TXN_ID|RESOURCE_ID|DATA|TIMESTAMP</code>. Calls <code>flush()</code> immediately after every write.", styles['BulletCustom']))
    story.append(Paragraph("• <code>TransactionManager.cpp</code>: The coordinator between transactions and locks. When <code>commitTransaction()</code> is called, marks state COMMITTED, logs to WAL, and calls <code>lockManager_.releaseAllLocks(txnId)</code>.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>Transaction.h / .cpp</code>, <code>TransactionManager.h / .cpp</code>, <code>TransactionLog.h / .cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m6_qa = [
        [Paragraph("<b>Q: What is the Write-Ahead Logging rule?</b><br/><i>A: No data mutation shall be committed to persistent memory until the corresponding log record has been flushed to durable storage on disk.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: How do you roll back an aborted transaction?</b><br/><i>A: TransactionManager sets state to ABORTED, logs ABORT in WAL, and calls LockManager::releaseAllLocks(), immediately unblocking any other transactions waiting on those keys.</i>", styles['TableCell'])]
    ]
    t_m6 = Table(m6_qa, colWidths=[480])
    t_m6.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m6)
    story.append(PageBreak())

    # =========================================================================
    # MODULE 7: TWO-PHASE COMMIT COORDINATOR (2PC)
    # =========================================================================
    story.append(Paragraph("Module 7: Two-Phase Commit Coordinator (2PC Consensus)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Ensures that cross-node distributed transactions either commit everywhere across all machines or abort everywhere, guaranteeing distributed atomicity.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("A wedding ceremony. Phase 1 (The Question): The priest asks the groom 'Do you take...?' (he says I DO), and asks the bride 'Do you take...?' (she says I DO). Phase 2 (The Pronouncement): Because both said I DO, the priest announces 'I now pronounce you married!' If either person says NO, the wedding is called off immediately.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <b>Phase 1 (PREPARE):</b> Coordinator sends <code>PREPARE</code> over TCP to all participant nodes and starts a timer. Participants verify local locks, write PREPARE in WAL, and reply <code>VOTE_YES</code> or <code>VOTE_NO</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <b>Phase 2 (COMMIT / ABORT):</b> If <b>ALL</b> participants vote YES, coordinator broadcasts <code>GLOBAL_COMMIT</code>. If even one votes NO or times out, coordinator broadcasts <code>GLOBAL_ABORT</code>.", styles['BulletCustom']))
    story.append(Paragraph("• <code>TwoPCCoordinator.cpp</code>: Thread-safe state tracking using <code>activeTxns_[txnId]</code>, mutexes, and condition variables waiting for incoming vote counts.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/coordinator/TwoPCCoordinator.h</code>, <code>src/coordinator/TwoPCCoordinator.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m7_qa = [
        [Paragraph("<b>Q: What is the blocking problem of Two-Phase Commit?</b><br/><i>A: If the coordinator crashes after participants vote YES but before sending the decision, participants are left blocked holding locks. We solve this with timeout detection and coordinator log replay.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: Why can't nodes just commit independently without 2PC?</b><br/><i>A: If Node 1 commits but Node 2 runs out of memory and crashes, half the transfer happened and half failed. Atomicity is violated and data is corrupted. 2PC guarantees all-or-nothing.</i>", styles['TableCell'])]
    ]
    t_m7 = Table(m7_qa, colWidths=[480])
    t_m7.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m7)
    story.append(Spacer(1, 10))

    # =========================================================================
    # MODULE 8: MASTER NODE ORCHESTRATOR
    # =========================================================================
    story.append(Paragraph("Module 8: Master Node Orchestrator", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Glues all 6 subsystems together into a single, cohesive, self-contained server node daemon.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("The motherboard of a computer. The CPU, RAM, GPU, and hard drive are all great parts, but they can't do anything until they are plugged into the motherboard which routes electricity and data between them.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>NodeConfig</code>: Holds <code>nodeId</code>, <code>port</code>, list of peer addresses (<code>peers: [{id, host, port}]</code>), and log path.", styles['BulletCustom']))
    story.append(Paragraph("• <code>start()</code>: Sets up message routing callbacks, starts <code>NetworkManager</code> listener, starts <code>HeartbeatManager</code>, and launches periodic <code>DeadlockDetector</code> thread.", styles['BulletCustom']))
    story.append(Paragraph("• <code>stop()</code>: Orderly shutdown of all threads, closing sockets, and flushing logs.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>include/dlm/node/Node.h</code>, <code>src/node/Node.cpp</code>, <code>src/node_main.cpp</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m8_qa = [
        [Paragraph("<b>Q: How does a Node dispatch incoming network messages?</b><br/><i>A: During start(), setupMessageHandlers() registers lambda functions with NetworkManager. Incoming LOCK_REQUEST messages route to LockManager, PREPARE messages route to 2PC Coordinator, etc.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: How does Node collect global deadlock edges?</b><br/><i>A: It broadcasts WFG_REQUEST to all peers, waits up to 1 second for WFG_RESPONSE messages, merges peer edges with local edges, and feeds the merged graph to DeadlockDetector.</i>", styles['TableCell'])]
    ]
    t_m8 = Table(m8_qa, colWidths=[480])
    t_m8.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m8)
    story.append(PageBreak())

    # =========================================================================
    # MODULE 9: INTERACTIVE VISUAL DASHBOARD (UI)
    # =========================================================================
    story.append(Paragraph("Module 9: Interactive Visual Web Dashboard (UI)", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("An interactive, single-page visual dashboard that turns invisible C++ locking and graph algorithms into living animations right inside your web browser.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. The Everyday Analogy:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("An air traffic control radar screen. Instead of reading lines of raw radio text, the air traffic controller sees planes as glowing dots on a map with green and red safety indicators.", styles['BodyCustom']))

    story.append(Paragraph("<b>3. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>index.html</code>: Semantic layout featuring Cluster Topology cards, Canvas WFG, Active Lock Table, 2PC progress cards, and WAL console.", styles['BulletCustom']))
    story.append(Paragraph("• <code>app.js</code>: A client-side state engine mirroring the exact C++ backend rules. Draws the Wait-For Graph on an HTML5 <code>&lt;canvas&gt;</code> in a circular layout. When circular wait is formed, lights up the cycle in <b>pulsing neon red</b>, alerts the user, and auto-aborts the youngest victim after 2.5s.", styles['BulletCustom']))
    story.append(Paragraph("• <code>launch_dashboard.bat</code>: 1-click batch launcher in project root that opens <code>index.html</code> directly in your default browser.", styles['BulletCustom']))

    story.append(Paragraph("<b>4. Key Files:</b> <code>index.html</code>, <code>styles.css</code>, <code>app.js</code>, <code>launch_dashboard.bat</code>.", styles['BodyCustom']))

    story.append(Paragraph("<b>5. Top Evaluator Questions & Answers:</b>", styles['SubSectionHeader']))
    m9_qa = [
        [Paragraph("<b>Q: How does the UI render the Wait-For Graph?</b><br/><i>A: It computes circular trigonometry coordinates (x, y) for active transaction nodes, renders glowing circles, and uses HTML5 Canvas context methods (moveTo, lineTo) to draw directed dependency arrows.</i>", styles['TableCell'])],
        [Paragraph("<b>Q: What happens when you click 'Simulate Crash'?</b><br/><i>A: The node turns red/offline, heartbeats stop, and the cluster immediately aborts its active transactions and evicts its held locks, proving fault tolerance live!</i>", styles['TableCell'])]
    ]
    t_m9 = Table(m9_qa, colWidths=[480])
    t_m9.setStyle(TableStyle([('BACKGROUND', (0,0), (-1,-1), colors.HexColor("#F1F5F9")), ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")), ('LEFTPADDING', (0,0), (-1,-1), 6)]))
    story.append(t_m9)
    story.append(Spacer(1, 10))

    # =========================================================================
    # MODULE 10: TEST SUITE & MULTI-NODE SIMULATION
    # =========================================================================
    story.append(Paragraph("Module 10: Automated Test Suite & Multi-Node Simulation", styles['SectionHeader']))
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#3B82F6"), spaceAfter=8))

    story.append(Paragraph("<b>1. What It Does in 1 Sentence:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("Rigorous verification proving that all C++ code compiles cleanly, passes 17 automated unit tests, and executes 5 real-world distributed cluster scenarios over real TCP sockets.", styles['BodyCustom']))

    story.append(Paragraph("<b>2. Internal Code Mechanics:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• <code>tests/test_all.cpp</code>: 17 test cases verifying Shared/Exclusive locks, WFG edge generation, 2-node and 3-node deadlock cycles, WAL recovery, 2PC prepare, and network message roundtrip. All 17 tests pass with 0 failures.", styles['BulletCustom']))
    story.append(Paragraph("• <code>src/simulation_main.cpp</code>: Instantiates 3 independent <code>dlm::Node</code> objects on TCP ports 9001, 9002, and 9003 in-process, executing 5 end-to-end distributed database scenarios.", styles['BulletCustom']))

    story.append(Paragraph("<b>3. Key Files & Commands:</b>", styles['SubSectionHeader']))
    story.append(Paragraph("• Run Tests: <code>.\\build\\Debug\\dlm_tests.exe</code> (Outputs: 17 passed, 0 failed)", styles['BulletCustom']))
    story.append(Paragraph("• Run Simulation: <code>.\\build\\Debug\\dlm_simulation.exe</code> (Executes all 5 cluster scenarios)", styles['BulletCustom']))

    story.append(Spacer(1, 15))
    story.append(Paragraph("<b>Summary: You Are Ready!</b>", styles['SubSectionHeader']))
    story.append(Paragraph("With this guide, you now understand the exact problem, the real-world analogies, the internal algorithms, the C++ code, and the answers to every technical question an evaluator could possibly ask. You are in complete control of this project!", styles['BodyCustom']))

    # Build PDF
    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"SUCCESS: Generated Complete Module Layman's PDF at {filename}")

if __name__ == "__main__":
    out_file = os.path.join(os.path.dirname(__file__), "Distributed_Lock_Manager_Laymans_Guide.pdf")
    build_layman_pdf(out_file)
