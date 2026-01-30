#!/usr/bin/env python3
"""
Markdown to HTML converter for LoliProfiler memory analysis reports.
Converts markdown reports to styled HTML with syntax highlighting.
"""

import re
import sys
import argparse
from typing import Optional


def escape_html(text: str) -> str:
    """Escape HTML special characters."""
    return (text
            .replace('&', '&amp;')
            .replace('<', '&lt;')
            .replace('>', '&gt;')
            .replace('"', '&quot;')
            .replace("'", '&#39;'))


def convert_code_blocks(text: str) -> str:
    """Convert fenced code blocks to HTML."""
    def replace_code_block(match):
        lang = match.group(1) or ''
        code = escape_html(match.group(2))
        lang_class = f' class="language-{lang}"' if lang else ''
        return f'<pre><code{lang_class}>{code}</code></pre>'

    # Match ```lang ... ``` blocks
    pattern = r'```(\w*)\n(.*?)```'
    return re.sub(pattern, replace_code_block, text, flags=re.DOTALL)


def convert_inline_code(text: str) -> str:
    """Convert inline `code` to HTML."""
    return re.sub(r'`([^`]+)`', r'<code>\1</code>', text)


def convert_headers(text: str) -> str:
    """Convert markdown headers to HTML."""
    lines = text.split('\n')
    result = []
    for line in lines:
        # Match headers
        match = re.match(r'^(#{1,6})\s+(.+)$', line)
        if match:
            level = len(match.group(1))
            content = match.group(2)
            result.append(f'<h{level}>{content}</h{level}>')
        else:
            result.append(line)
    return '\n'.join(result)


def convert_bold_italic(text: str) -> str:
    """Convert bold and italic text."""
    # Bold: **text** or __text__
    text = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', text)
    text = re.sub(r'__([^_]+)__', r'<strong>\1</strong>', text)
    # Italic: *text* or _text_
    text = re.sub(r'\*([^*]+)\*', r'<em>\1</em>', text)
    text = re.sub(r'(?<![_\w])_([^_]+)_(?![_\w])', r'<em>\1</em>', text)
    return text


def convert_lists(text: str) -> str:
    """Convert markdown lists to HTML."""
    lines = text.split('\n')
    result = []
    in_ul = False
    in_ol = False

    for line in lines:
        # Unordered list
        ul_match = re.match(r'^(\s*)[-*]\s+(.+)$', line)
        # Ordered list
        ol_match = re.match(r'^(\s*)\d+\.\s+(.+)$', line)

        if ul_match:
            if not in_ul:
                if in_ol:
                    result.append('</ol>')
                    in_ol = False
                result.append('<ul>')
                in_ul = True
            result.append(f'<li>{ul_match.group(2)}</li>')
        elif ol_match:
            if not in_ol:
                if in_ul:
                    result.append('</ul>')
                    in_ul = False
                result.append('<ol>')
                in_ol = True
            result.append(f'<li>{ol_match.group(2)}</li>')
        else:
            if in_ul:
                result.append('</ul>')
                in_ul = False
            if in_ol:
                result.append('</ol>')
                in_ol = False
            result.append(line)

    # Close any open lists
    if in_ul:
        result.append('</ul>')
    if in_ol:
        result.append('</ol>')

    return '\n'.join(result)


def convert_horizontal_rules(text: str) -> str:
    """Convert horizontal rules (---) to HTML."""
    return re.sub(r'^---+\s*$', '<hr>', text, flags=re.MULTILINE)


def convert_paragraphs(text: str) -> str:
    """Wrap text blocks in paragraph tags."""
    # Split by double newlines
    blocks = re.split(r'\n\n+', text)
    result = []

    for block in blocks:
        block = block.strip()
        if not block:
            continue
        # Don't wrap if already wrapped in HTML tags
        if (block.startswith('<h') or
            block.startswith('<ul') or
            block.startswith('<ol') or
            block.startswith('<pre') or
            block.startswith('<hr') or
            block.startswith('<li') or
            block.startswith('<p')):
            result.append(block)
        else:
            # Check if block contains list items (don't wrap those in p tags)
            if '<li>' in block:
                result.append(block)
            else:
                # Convert single newlines to <br> within paragraphs
                block = block.replace('\n', '<br>\n')
                result.append(f'<p>{block}</p>')

    return '\n\n'.join(result)


def get_html_template(title: str, content: str) -> str:
    """Generate full HTML document with styling."""
    return f'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{escape_html(title)}</title>
    <style>
        :root {{
            --bg-color: #ffffff;
            --text-color: #333333;
            --code-bg: #f5f5f5;
            --border-color: #e0e0e0;
            --header-color: #2c3e50;
            --link-color: #3498db;
            --success-color: #27ae60;
            --warning-color: #f39c12;
            --danger-color: #e74c3c;
        }}

        @media (prefers-color-scheme: dark) {{
            :root {{
                --bg-color: #1a1a2e;
                --text-color: #e0e0e0;
                --code-bg: #16213e;
                --border-color: #0f3460;
                --header-color: #e94560;
                --link-color: #00d9ff;
            }}
        }}

        * {{
            box-sizing: border-box;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            line-height: 1.6;
            color: var(--text-color);
            background-color: var(--bg-color);
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }}

        h1 {{
            color: var(--header-color);
            border-bottom: 3px solid var(--header-color);
            padding-bottom: 10px;
            margin-top: 0;
        }}

        h2 {{
            color: var(--header-color);
            border-bottom: 2px solid var(--border-color);
            padding-bottom: 8px;
            margin-top: 30px;
        }}

        h3 {{
            color: var(--header-color);
            margin-top: 25px;
        }}

        pre {{
            background-color: var(--code-bg);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 16px;
            overflow-x: auto;
            font-size: 14px;
        }}

        code {{
            font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
            background-color: var(--code-bg);
            padding: 2px 6px;
            border-radius: 4px;
            font-size: 0.9em;
        }}

        pre code {{
            padding: 0;
            background-color: transparent;
        }}

        hr {{
            border: none;
            border-top: 2px solid var(--border-color);
            margin: 30px 0;
        }}

        ul, ol {{
            padding-left: 30px;
        }}

        li {{
            margin-bottom: 8px;
        }}

        strong {{
            color: var(--header-color);
        }}

        p {{
            margin: 15px 0;
        }}

        /* Memory analysis specific styles */
        .memory-positive {{
            color: var(--danger-color);
            font-weight: bold;
        }}

        .memory-negative {{
            color: var(--success-color);
            font-weight: bold;
        }}

        /* Table styles */
        table {{
            border-collapse: collapse;
            width: 100%;
            margin: 20px 0;
        }}

        th, td {{
            border: 1px solid var(--border-color);
            padding: 12px;
            text-align: left;
        }}

        th {{
            background-color: var(--code-bg);
            font-weight: bold;
        }}

        tr:nth-child(even) {{
            background-color: var(--code-bg);
        }}

        /* Print styles */
        @media print {{
            body {{
                max-width: none;
                padding: 0;
            }}

            pre {{
                white-space: pre-wrap;
                word-wrap: break-word;
            }}
        }}

        /* Responsive design */
        @media (max-width: 768px) {{
            body {{
                padding: 10px;
            }}

            pre {{
                padding: 10px;
                font-size: 12px;
            }}
        }}
    </style>
</head>
<body>
{content}
</body>
</html>'''


def markdown_to_html(markdown_text: str, title: Optional[str] = None) -> str:
    """
    Convert markdown text to styled HTML.

    Args:
        markdown_text: The markdown content to convert
        title: Optional title for the HTML document

    Returns:
        Complete HTML document as string
    """
    # Extract title from first h1 if not provided
    if not title:
        h1_match = re.search(r'^#\s+(.+)$', markdown_text, re.MULTILINE)
        title = h1_match.group(1) if h1_match else 'Memory Analysis Report'

    # Process markdown in order (order matters!)
    content = markdown_text

    # Step 1: Extract and protect code blocks with placeholders
    code_blocks = []
    def save_code_block(match):
        idx = len(code_blocks)
        lang = match.group(1) or ''
        code = escape_html(match.group(2))
        lang_class = f' class="language-{lang}"' if lang else ''
        code_blocks.append(f'<pre><code{lang_class}>{code}</code></pre>')
        return f'\x00CODEBLOCK{idx}\x00'

    content = re.sub(r'```(\w*)\n(.*?)```', save_code_block, content, flags=re.DOTALL)

    # Step 2: Extract and protect inline code with placeholders
    inline_codes = []
    def save_inline_code(match):
        idx = len(inline_codes)
        code = escape_html(match.group(1))
        inline_codes.append(f'<code>{code}</code>')
        return f'\x00INLINECODE{idx}\x00'

    content = re.sub(r'`([^`]+)`', save_inline_code, content)

    # Step 3: Convert headers
    content = convert_headers(content)

    # Step 4: Convert horizontal rules
    content = convert_horizontal_rules(content)

    # Step 5: Convert lists
    content = convert_lists(content)

    # Step 6: Convert bold and italic (now safe - code is protected)
    content = convert_bold_italic(content)

    # Step 7: Restore inline code
    for idx, code in enumerate(inline_codes):
        content = content.replace(f'\x00INLINECODE{idx}\x00', code)

    # Step 8: Wrap remaining text in paragraphs (before restoring code blocks)
    content = convert_paragraphs(content)

    # Step 9: Highlight memory values (positive = red, negative = green)
    # Do this before restoring code blocks so code content is not affected
    content = re.sub(
        r'\+(\d+\.?\d*)\s*(MB|KB|GB|MiB|KiB|GiB)',
        r'<span class="memory-positive">+\1 \2</span>',
        content
    )
    content = re.sub(
        r'-(\d+\.?\d*)\s*(MB|KB|GB|MiB|KiB|GiB)',
        r'<span class="memory-negative">-\1 \2</span>',
        content
    )

    # Step 10: Restore code blocks (after all text processing is done)
    for idx, block in enumerate(code_blocks):
        content = content.replace(f'\x00CODEBLOCK{idx}\x00', block)

    return get_html_template(title, content)


def convert_file(input_file: str, output_file: str, title: Optional[str] = None) -> bool:
    """
    Convert a markdown file to HTML.

    Args:
        input_file: Path to input markdown file
        output_file: Path to output HTML file
        title: Optional title for the HTML document

    Returns:
        True if conversion succeeded, False otherwise
    """
    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            markdown_text = f.read()

        html_content = markdown_to_html(markdown_text, title)

        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html_content)

        return True
    except Exception as e:
        print(f"Error converting file: {e}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Convert markdown memory analysis reports to styled HTML',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert markdown to HTML
  python markdown_to_html.py report.md -o report.html

  # Convert with custom title
  python markdown_to_html.py report.md -o report.html --title "Memory Report v1.0"

  # Read from stdin, write to stdout
  cat report.md | python markdown_to_html.py > report.html
        """
    )

    parser.add_argument('input', nargs='?', default='-',
                        help='Input markdown file (default: stdin)')
    parser.add_argument('--output', '-o',
                        help='Output HTML file (default: stdout)')
    parser.add_argument('--title', '-t',
                        help='HTML document title (default: extracted from h1)')

    args = parser.parse_args()

    # Read input
    if args.input == '-':
        markdown_text = sys.stdin.read()
    else:
        try:
            with open(args.input, 'r', encoding='utf-8') as f:
                markdown_text = f.read()
        except FileNotFoundError:
            print(f"Error: Input file not found: {args.input}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"Error reading input file: {e}", file=sys.stderr)
            return 1

    # Convert
    html_content = markdown_to_html(markdown_text, args.title)

    # Write output
    if args.output:
        try:
            with open(args.output, 'w', encoding='utf-8') as f:
                f.write(html_content)
            print(f"HTML report written to: {args.output}")
        except Exception as e:
            print(f"Error writing output file: {e}", file=sys.stderr)
            return 1
    else:
        print(html_content)

    return 0


if __name__ == '__main__':
    sys.exit(main())
