#!/usr/bin/env python3
import sys
import re
import os
import xml.etree.ElementTree as ET
import pandas as pd

def parse_raw_log(log_text: str):
    """Extract <TestCaseResult> XML blocks from a raw CTS log file."""
    pattern = re.compile(
        r'<TestCaseResult[^>]*>.*?</TestCaseResult>',
        re.DOTALL
    )
    matches = pattern.findall(log_text)
    return matches


def parse_xml_file(path: str):
    """Extract <TestCaseResult> nodes from a pure XML file."""
    tree = ET.parse(path)
    root = tree.getroot()
    return [
        ET.tostring(elem, encoding="unicode")
        for elem in root.findall(".//TestCaseResult")
    ]


def process_testcases(xml_blocks):
    """Convert XML test blocks into structured rows."""
    rows = []

    for block in xml_blocks:
        elem = ET.fromstring(block)

        case = elem.attrib.get("CasePath", "unknown")
        duration = elem.findtext("Number", default="0")
        result = elem.find("Result").attrib.get("StatusCode", "UNKNOWN")
        message = elem.findtext("Text", default="")

        rows.append({
            "Test Case": case,
            "Duration (µs)": int(duration),
            "Status": result,
            "Message": message,
        })

    return rows

def status_to_html(status: str) -> str:
    cls = {
        "Pass": "status-Pass",
        "Fail": "status-Fail",
        "NotSupported": "status-NotSupported",
    }.get(status, "")
    return f'<span class="status-pill {cls}">{status}</span>'

def main():
    if len(sys.argv) != 3:
        print("Usage: cts_report.py <input_log_or_xml> <output_html>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    if not os.path.exists(input_path):
        print(f"Error: input file not found: {input_path}")
        sys.exit(1)

    # Detect input format
    with open(input_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    if "<TestCaseResult" in content and content.strip().startswith("<?xml"):
        print("[INFO] Detected pure XML input")
        xml_blocks = parse_xml_file(input_path)
    else:
        print("[INFO] Detected raw CTS log input")
        xml_blocks = parse_raw_log(content)

    if not xml_blocks:
        print("Error: no TestCaseResult entries found.")
        sys.exit(1)

    rows = process_testcases(xml_blocks)

    df = pd.DataFrame(rows)
    df["Status"] = df["Status"].apply(status_to_html)

    # Create HTML
    table_html = df.to_html(
        index=False,
        escape=False,
        justify="center",
        border=0,
        classes="cts-table"
    )

    # Nicer HTML + CSS
    html = f"""
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>Vulkan CTS Report</title>
<style>
:root {{
    --bg: #0f172a;
    --bg-card: #020617;
    --bg-card-soft: #02081f;
    --accent: #38bdf8;
    --accent-soft: rgba(56, 189, 248, 0.15);
    --accent-strong: #0ea5e9;
    --text-main: #e5e7eb;
    --text-muted: #9ca3af;
    --border-soft: rgba(148, 163, 184, 0.2);
    --danger: #f97373;
    --warning: #eab308;
    --success: #22c55e;
    --radius-lg: 14px;
    --radius-pill: 999px;
    --shadow-soft: 0 18px 45px rgba(15, 23, 42, 0.65);
    --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
}}

* {{
    box-sizing: border-box;
}}

html, body {{
    margin: 0;
    padding: 0;
    background: radial-gradient(circle at top, #1e293b 0, #020617 45%, #000 100%);
    color: var(--text-main);
    font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}}

body {{
    min-height: 100vh;
    display: flex;
    align-items: flex-start;
    justify-content: center;
    padding: 40px 16px;
}}

.report-shell {{
    width: 100%;
    max-width: 1200px;
}}

.report-card {{
    background: linear-gradient(145deg, var(--bg-card) 0, var(--bg-card-soft) 60%, #020617 100%);
    border-radius: 24px;
    box-shadow: var(--shadow-soft);
    padding: 24px 24px 18px;
    border: 1px solid rgba(148, 163, 184, 0.20);
    backdrop-filter: blur(12px);
}}

.report-header {{
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 16px;
    margin-bottom: 18px;
}}

.report-title-block h1 {{
    font-size: 1.5rem;
    margin: 0 0 4px;
    letter-spacing: 0.02em;
}}

.report-title-block .subtitle {{
    margin: 0;
    font-size: 0.9rem;
    color: var(--text-muted);
}}

.report-meta {{
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    justify-content: flex-end;
}}

.badge {{
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 4px 10px;
    border-radius: var(--radius-pill);
    font-size: 0.8rem;
    border: 1px solid rgba(148, 163, 184, 0.35);
    background: rgba(15, 23, 42, 0.85);
    color: var(--text-muted);
}}

.badge-dot {{
    width: 7px;
    height: 7px;
    border-radius: 999px;
    background: var(--accent);
}}

.badge-accent {{
    border-color: rgba(56, 189, 248, 0.45);
    background: var(--accent-soft);
    color: var(--accent-strong);
}}

.table-wrapper {{
    margin-top: 16px;
    border-radius: var(--radius-lg);
    border: 1px solid var(--border-soft);
    overflow: hidden;
    background: rgba(15, 23, 42, 0.85);
}}

.cts-table {{
    width: 100%;
    border-collapse: collapse;
    border-spacing: 0;
    font-size: 0.9rem;
}}

.cts-table thead {{
    background: radial-gradient(circle at top, rgba(56, 189, 248, 0.1), rgba(15, 23, 42, 1));
}}

.cts-table thead th {{
    padding: 10px 12px;
    text-align: left;
    border-bottom: 1px solid var(--border-soft);
    font-weight: 500;
    color: var(--text-muted);
    font-size: 0.8rem;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    white-space: nowrap;
}}

.cts-table tbody tr {{
    transition: background 0.18s ease, transform 0.08s ease;
}}

.cts-table tbody tr:nth-child(even) {{
    background: rgba(15, 23, 42, 0.95);
}}

.cts-table tbody tr:nth-child(odd) {{
    background: rgba(15, 23, 42, 0.85);
}}

.cts-table tbody tr:hover {{
    background: rgba(56, 189, 248, 0.07);
}}

.cts-table td {{
    padding: 8px 12px;
    border-bottom: 1px solid rgba(15, 23, 42, 0.9);
    vertical-align: top;
}}

.cts-table td:first-child {{
    font-family: var(--font-mono);
    font-size: 0.82rem;
    color: #e2e8f0;
}}

.cts-table td:nth-child(2) {{
    white-space: nowrap;
}}

.cts-table td:nth-child(3) {{
    width: 1%;
    white-space: nowrap;
}}

.cts-table td:last-child {{
    color: var(--text-muted);
}}

.status-pill {{
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 2px 10px;
    border-radius: var(--radius-pill);
    font-size: 0.75rem;
    font-weight: 500;
    border: 1px solid transparent;
}}

.status-Pass {{
    background: rgba(34, 197, 94, 0.12);
    border-color: rgba(34, 197, 94, 0.55);
    color: var(--success);
}}

.status-Fail {{
    background: rgba(248, 113, 113, 0.12);
    border-color: rgba(248, 113, 113, 0.55);
    color: var(--danger);
}}

.status-NotSupported {{
    background: rgba(234, 179, 8, 0.12);
    border-color: rgba(234, 179, 8, 0.55);
    color: var(--warning);
}}

.footer-note {{
    margin-top: 12px;
    font-size: 0.78rem;
    color: var(--text-muted);
    text-align: right;
}}

.footer-note code {{
    font-family: var(--font-mono);
    font-size: 0.75rem;
    color: #cbd5f5;
}}
</style>
</head>
<body>
<div class="report-shell">
  <div class="report-card">
    <div class="report-header">
      <div class="report-title-block">
        <h1>Vulkan CTS Report</h1>
        <p class="subtitle">Summary of test cases, status and timings</p>
      </div>
      <div class="report-meta">
        <div class="badge badge-accent">
          <span class="badge-dot"></span>
          <span>Generated from CTS log</span>
        </div>
        <div class="badge">
          <span class="badge-dot" style="background: #4ade80;"></span>
          <span>Total tests: {len(df)}</span>
        </div>
      </div>
    </div>
    <div class="table-wrapper">
      {table_html}
    </div>
    <div class="footer-note">
      Generated by <code>cts_report.py</code>
    </div>
  </div>
</div>
</body>
</html>
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html)

    print(f"[OK] HTML report saved to: {output_path}")


if __name__ == "__main__":
    main()
