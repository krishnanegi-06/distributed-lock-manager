"""
Generates DLM_DB_Instructions_Guide.pdf using ReportLab.
Contains step-by-step instructions for building, testing, benchmarking,
running natively on Windows, deploying with Docker Compose, and using
the Distributed Transactional Key-Value Database dashboard.
"""

import os
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    Preformatted,
    HRFlowable,
    PageBreak,
)
from reportlab.pdfgen import canvas


class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self._draw_chrome(num_pages)
            super().showPage()
        super().save()

    def _draw_chrome(self, page_count):
        if self._pageNumber == 1:
            return
        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#64748B"))
        self.drawString(54, 11 * inch - 34, "DLM-DB — Setup, Execution & Verification Instructions")
        self.setStrokeColor(colors.HexColor("#CBD5E1"))
        self.line(54, 11 * inch - 40, 8.5 * inch - 54, 11 * inch - 40)
        self.line(54, 44, 8.5 * inch - 54, 44)
        self.drawRightString(8.5 * inch - 54, 30, f"Page {self._pageNumber} of {page_count}")
        self.restoreState()


def build_pdf(output_path: str) -> None:
    doc = SimpleDocTemplate(
        output_path,
        pagesize=letter,
        leftMargin=54,
        rightMargin=54,
        topMargin=56,
        bottomMargin=54,
        title="DLM-DB Instructions Guide",
        author="DLM-DB Engineering",
    )

    styles = getSampleStyleSheet()
    title_style = ParagraphStyle(
        "DocTitle",
        parent=styles["Title"],
        fontName="Helvetica-Bold",
        fontSize=24,
        leading=28,
        textColor=colors.HexColor("#0F172A"),
        spaceAfter=10,
    )
    subtitle_style = ParagraphStyle(
        "DocSubtitle",
        parent=styles["Normal"],
        fontName="Helvetica",
        fontSize=12,
        leading=16,
        textColor=colors.HexColor("#475569"),
        spaceAfter=18,
    )
    h1_style = ParagraphStyle(
        "H1",
        parent=styles["Heading1"],
        fontName="Helvetica-Bold",
        fontSize=15,
        leading=20,
        textColor=colors.HexColor("#1E293B"),
        spaceBefore=14,
        spaceAfter=6,
    )
    h2_style = ParagraphStyle(
        "H2",
        parent=styles["Heading2"],
        fontName="Helvetica-Bold",
        fontSize=12,
        leading=16,
        textColor=colors.HexColor("#334155"),
        spaceBefore=10,
        spaceAfter=4,
    )
    body_style = ParagraphStyle(
        "Body",
        parent=styles["Normal"],
        fontName="Helvetica",
        fontSize=10,
        leading=14,
        textColor=colors.HexColor("#1E293B"),
        spaceAfter=6,
    )
    bullet_style = ParagraphStyle(
        "BulletItem",
        parent=body_style,
        leftIndent=16,
        firstLineIndent=-10,
        spaceAfter=4,
    )
    code_style = ParagraphStyle(
        "CodeBlock",
        parent=styles["Code"],
        fontName="Courier",
        fontSize=8.5,
        leading=11.5,
        textColor=colors.HexColor("#0F172A"),
        backColor=colors.HexColor("#F1F5F9"),
        borderColor=colors.HexColor("#CBD5E1"),
        borderWidth=0.6,
        borderPadding=7,
        spaceBefore=4,
        spaceAfter=8,
    )
    note_style = ParagraphStyle(
        "NoteBox",
        parent=body_style,
        fontSize=9.5,
        leading=13.5,
        backColor=colors.HexColor("#EFF6FF"),
        borderColor=colors.HexColor("#93C5FD"),
        borderWidth=0.75,
        borderPadding=8,
        spaceBefore=4,
        spaceAfter=8,
    )

    story = []

    # Cover / Header
    story.append(Spacer(1, 0.35 * inch))
    story.append(Paragraph("DLM-DB: Distributed Transactional Key-Value Database", title_style))
    story.append(
        Paragraph(
            "Step-by-Step Instructions for Building, Testing, Benchmarking, Running Natively on Windows, "
            "and Deploying with Docker Compose",
            subtitle_style,
        )
    )
    story.append(HRFlowable(width="100%", thickness=1.5, color=colors.HexColor("#4F46E5")))
    story.append(Spacer(1, 14))

    story.append(
        Paragraph(
            "<b>System Architecture Summary:</b> DLM-DB consists of three independent C++17 database nodes "
            "(<code>dlm_node</code> on ports 9001, 9002, 9003) with FNV-1a sharding, Strict Two-Phase Locking (SS2PL), "
            "Two-Phase Commit (2PC), CRC32 Write-Ahead Logging (WAL) with crash recovery, a C++ HTTP REST + WebSocket "
            "API server (<code>dlm_api</code> on port 8080), and a React + TypeScript + Vite dashboard (<code>frontend/</code>).",
            note_style,
        )
    )

    # Prerequisites
    story.append(Paragraph("1. Prerequisites", h1_style))
    prereq_data = [
        ["Environment", "Required Software", "Notes"],
        ["Native Windows", "Windows 10/11 (x64), Visual Studio 2022 (MSVC v143), CMake 3.16+, Node.js 20+ & npm", "No Docker, Boost, or vcpkg needed"],
        ["Docker Mode", "Docker Desktop / Docker Engine with Docker Compose v2", "Builds Linux backend & Nginx frontend"],
    ]
    t = Table(prereq_data, colWidths=[1.3 * inch, 3.6 * inch, 2.0 * inch])
    t.setStyle(
        TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1E293B")),
            ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
            ("FONTSIZE", (0, 0), (-1, -1), 8.5),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#CBD5E1")),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("PADDING", (0, 0), (-1, -1), 6),
        ])
    )
    story.append(t)
    story.append(Spacer(1, 8))

    # Step 2: Build
    story.append(Paragraph("2. Build the C++ Backend & React Frontend (Windows)", h1_style))
    story.append(
        Paragraph(
            "Open PowerShell in the repository root and run the automated build script. This configures CMake with "
            "Visual Studio 17 2022 (x64), compiles all C++ targets, typechecks TypeScript, and builds the Vite bundle.",
            body_style,
        )
    )
    story.append(
        Preformatted(
            ".\\scripts\\windows\\build.ps1 -Config Release\n"
            "# Or for Debug builds (recommended when running tests):\n"
            ".\\scripts\\windows\\build.ps1 -Config Debug",
            code_style,
        )
    )
    story.append(
        Paragraph(
            "<b>Manual Build Equivalent:</b>",
            body_style,
        )
    )
    story.append(
        Preformatted(
            "cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64\n"
            "cmake --build build --config Release\n"
            "cd frontend\n"
            "npm install\n"
            "npm run typecheck\n"
            "npm run build",
            code_style,
        )
    )

    # Step 3: Tests & Benchmarks
    story.append(Paragraph("3. Run Automated Tests & Performance Benchmarks", h1_style))
    story.append(
        Paragraph(
            "Run the CTest suite (<code>dlm_tests</code> + <code>dlm_gtest</code>) and frontend checks in a standard "
            "local PowerShell terminal (not inside a restricted network sandbox, because the multi-node integration and "
            "API tests open local loopback TCP sockets):",
            body_style,
        )
    )
    story.append(
        Preformatted(
            "# Run all C++ tests (CTest) + Frontend typecheck & production build\n"
            ".\\scripts\\windows\\run-tests.ps1 -Config Debug\n\n"
            "# Run individual C++ test executables directly\n"
            ".\\build\\Debug\\dlm_tests.exe\n"
            ".\\build\\Debug\\dlm_gtest.exe\n\n"
            "# Run the multi-node benchmark suite (Release recommended for accurate timings)\n"
            ".\\build\\Release\\dlm_benchmark.exe",
            code_style,
        )
    )
    story.append(
        Paragraph(
            "<b>What the tests verify:</b> Lock compatibility & upgrades, cycle detection, FNV-1a sharding, storage CRUD & "
            "persistence, CRC32 WAL partial-tail recovery, PBKDF2/HMAC auth, SQL command parsing, cross-node 2PC atomic "
            "commits, distributed deadlocks, node unavailability (`NODE_UNAVAILABLE`), in-doubt 2PC recovery, concurrent "
            "writers, and end-to-end HTTP + SQL API requests.",
            body_style,
        )
    )

    # Step 4: Native Windows Cluster
    story.append(PageBreak())
    story.append(Paragraph("4. Start & Stop the 3-Node Cluster on Windows", h1_style))
    story.append(
        Paragraph(
            "Use the provided PowerShell cluster scripts to launch <code>dlm_api.exe</code> (in supervised mode, "
            "spawning three independent <code>dlm_node.exe</code> processes on ports 9001, 9002, 9003) and the React "
            "dashboard on port 5173:",
            body_style,
        )
    )
    story.append(
        Preformatted(
            "# Start 3 database nodes + API server + React frontend\n"
            ".\\scripts\\windows\\start-cluster.ps1 -Config Release\n\n"
            "# Stop cluster cleanly (preserves all committed data under data/)\n"
            ".\\scripts\\windows\\stop-cluster.ps1\n\n"
            "# Explicitly wipe all demo data and WAL logs\n"
            ".\\scripts\\windows\\reset-demo.ps1",
            code_style,
        )
    )
    story.append(Paragraph("4.1 Manual 4-Terminal Launch (Optional)", h2_style))
    story.append(
        Paragraph(
            "If you want to watch each process's stdout independently, start 3 nodes and the API server in separate terminals:",
            body_style,
        )
    )
    story.append(
        Preformatted(
            "# Terminal 1 (Node 1)\n"
            ".\\build\\Release\\dlm_node.exe 1 9001 --data-dir data --log-dir logs 2:127.0.0.1:9002 3:127.0.0.1:9003\n\n"
            "# Terminal 2 (Node 2)\n"
            ".\\build\\Release\\dlm_node.exe 2 9002 --data-dir data --log-dir logs 1:127.0.0.1:9001 3:127.0.0.1:9003\n\n"
            "# Terminal 3 (Node 3)\n"
            ".\\build\\Release\\dlm_node.exe 3 9003 --data-dir data --log-dir logs 1:127.0.0.1:9001 2:127.0.0.1:9002\n\n"
            "# Terminal 4 (Application API Gateway)\n"
            ".\\build\\Release\\dlm_api.exe --port 8080 --config config\\nodes.example.json --data-dir data --log-dir logs\n\n"
            "# Terminal 5 (React Frontend)\n"
            "cd frontend && npm run dev -- --port 5173",
            code_style,
        )
    )

    # Step 5: Using the Dashboard
    story.append(Paragraph("5. First-Run Admin Setup & Dashboard Walkthrough", h1_style))
    steps = [
        "1. Open <b>http://localhost:5173</b> (native Windows) or <b>http://localhost:3000</b> (Docker).",
        "2. <b>First-Run Admin Setup:</b> On first launch, create an administrator username (3+ chars) and password (8+ chars). "
        "The C++ backend hashes it with PBKDF2-HMAC-SHA256 (100,000 iterations) and stores it in <code>data/auth.json</code>.",
        "3. <b>Database Explorer:</b> Create/update/delete keys and verify the FNV-1a shard owner (Node 1, 2, or 3).",
        "4. <b>SQL Command Console:</b> Run multi-statement scripts through the C++ transaction engine:",
    ]
    for s in steps:
        story.append(Paragraph(s, bullet_style))

    story.append(
        Preformatted(
            "BEGIN;\n"
            "PUT account_101 {\"balance\":5000};\n"
            "PUT account_102 {\"balance\":2500};\n"
            "GET account_101;\n"
            "COMMIT;\n\n"
            "-- Or test rollback:\n"
            "BEGIN;\n"
            "DELETE account_101;\n"
            "ABORT;",
            code_style,
        )
    )

    more_steps = [
        "5. <b>Transaction & 2PC Monitor:</b> Execute writes across keys owned by different nodes in a single transaction "
        "to observe 2PC <code>PREPARE</code>, <code>VOTE_YES</code>, durable decision logging, and <code>GLOBAL_COMMIT</code>.",
        "6. <b>Node Crash & Persistence Verification:</b> Go to <b>Node Details</b>, click <b>Crash Node</b> on any node, "
        "verify writes to its owned keys fail with <code>NODE_UNAVAILABLE</code>, then click <b>Restart &amp; Recover</b> and "
        "check <b>Recovery Monitor</b> and <b>Database Explorer</b> to verify committed data survived.",
        "7. <b>WAL Viewer & Deadlock Visualizer:</b> Inspect CRC32-logged WAL records per node/transaction and live Wait-For Graph cycles.",
    ]
    for s in more_steps:
        story.append(Paragraph(s, bullet_style))

    # Step 6: Docker
    story.append(PageBreak())
    story.append(Paragraph("6. Docker Compose Deployment", h1_style))
    story.append(
        Paragraph(
            "Docker Compose runs five isolated containers (<code>node1</code>, <code>node2</code>, <code>node3</code>, "
            "<code>api</code>, and <code>frontend</code>) with separate persistent volumes for each node.",
            body_style,
        )
    )
    story.append(
        Preformatted(
            "# 1. Create environment file (customize secrets before deploying)\n"
            "copy .env.example .env   # Windows\n"
            "# cp .env.example .env   # Linux/macOS\n\n"
            "# 2. Build and start all 5 services\n"
            "docker compose up --build -d\n\n"
            "# 3. Inspect health & logs\n"
            "docker compose ps\n"
            "docker compose logs -f api node1 node2 node3\n\n"
            "# 4. Stop containers (preserves persistent data volumes)\n"
            "docker compose down\n\n"
            "# 5. Explicit reset (DESTROYS all persistent node volumes)\n"
            "docker compose down -v",
            code_style,
        )
    )

    # Step 7: Ports & Troubleshooting
    story.append(Paragraph("7. Ports, Config & Troubleshooting", h1_style))
    ports_data = [
        ["Component", "Default Port", "Purpose"],
        ["node1 / node2 / node3", "9001 / 9002 / 9003", "Inter-node TCP protocol & CTRL_RPC_REQ"],
        ["dlm_api", "8080", "HTTP REST API (/api/*) & WebSocket (/ws)"],
        ["Vite Dev Server (Windows)", "5173", "React dashboard dev UI (proxies /api and /ws to :8080)"],
        ["Nginx Frontend (Docker)", "3000 -> 80", "Production UI & reverse proxy to api:8080"],
    ]
    pt = Table(ports_data, colWidths=[1.9 * inch, 1.5 * inch, 3.5 * inch])
    pt.setStyle(
        TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1E293B")),
            ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
            ("FONTSIZE", (0, 0), (-1, -1), 8.5),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#CBD5E1")),
            ("PADDING", (0, 0), (-1, -1), 6),
        ])
    )
    story.append(pt)
    story.append(Spacer(1, 10))

    tips = [
        "<b>Reset Forgotten Admin Password:</b> Stop the cluster and delete <code>data/auth.json</code>, or set "
        "<code>DLM_ADMIN_USER</code> and <code>DLM_ADMIN_PASSWORD</code> in <code>.env</code> before starting.",
        "<b>Port Already in Use:</b> Run <code>.\\scripts\\windows\\stop-cluster.ps1</code> or check lingering "
        "<code>dlm_node.exe</code> / <code>dlm_api.exe</code> processes.",
        "<b>Windows Firewall Prompt:</b> Allow local loopback/private network access for <code>dlm_node.exe</code> and "
        "<code>dlm_api.exe</code> if prompted on first launch.",
        "<b>Documentation Index:</b> Detailed architecture, protocol specs, API reference, and limitations are stored under <code>docs/</code>.",
    ]
    for tip in tips:
        story.append(Paragraph("• " + tip, bullet_style))

    doc.build(story, canvasmaker=NumberedCanvas)


if __name__ == "__main__":
    out_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "DLM_DB_Instructions_Guide.pdf")
    build_pdf(out_file)
    print(f"Generated: {out_file} ({os.path.getsize(out_file)} bytes)")
