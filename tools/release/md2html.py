"""Markdown -> styled HTML (Word then converts to .docx). Usage: md2html.py in.md out.html "Title"
Requires: pip install markdown
"""
import sys, markdown

src, out, title = sys.argv[1], sys.argv[2], sys.argv[3]
md = open(src, encoding="utf-8").read()
body = markdown.markdown(md, extensions=["tables", "fenced_code", "sane_lists"])
css = """
body { font-family: Calibri, sans-serif; font-size: 11pt; color: #1a1a1a; margin: 2cm; }
h1 { font-size: 20pt; color: #B03A2E; border-bottom: 2px solid #B03A2E; padding-bottom: 4pt; }
h2 { font-size: 14pt; color: #B03A2E; margin-top: 18pt; }
h3 { font-size: 12pt; color: #7B241C; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0; }
th { background: #B03A2E; color: white; text-align: left; padding: 4pt 6pt; font-size: 10pt; }
td { border: 1px solid #bbb; padding: 4pt 6pt; font-size: 10pt; vertical-align: top; }
tr:nth-child(even) td { background: #f6eae8; }
code { font-family: Consolas, monospace; font-size: 10pt; background: #f2f2f2; padding: 0 2pt; }
pre { font-family: Consolas, monospace; font-size: 9pt; background: #f6f6f6; border: 1px solid #ddd; padding: 8pt; }
li { margin: 3pt 0; }  em { color: #555; }
hr { border: none; border-top: 1px solid #ccc; margin: 14pt 0; }  a { color: #B03A2E; }
"""
html = f'<!DOCTYPE html><html><head><meta charset="utf-8"><title>{title}</title><style>{css}</style></head><body>\n{body}\n<hr><p><em>Z-Class Control System - {title}</em></p></body></html>'
open(out, "w", encoding="utf-8").write(html)
print(f"html: {out}")
