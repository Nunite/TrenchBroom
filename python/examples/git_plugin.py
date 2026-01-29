import tb
import subprocess
import os
import html
import base64

class GitManager:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Git Manager")
        self.repo_dir = self.get_repo_dir()
        self.commit_message = ""
        self.new_branch_name = ""
        self.dubious_ownership_path = None
        self._svg_cache = {}
        self._badge_cache = {}
        self._header_html = """
        <div style="display: flex; justify-content: space-between; align-items: center; padding: 4px 4px;">
            <span style="font-size: 13px; font-weight: bold; color: #cccccc; text-transform: uppercase;">Source Control</span>
            <span style="display: flex; align-items: center;">
                <a href="refresh:" title="Refresh" style="text-decoration: none; color: #cccccc; font-size: 16px; margin-left: 10px;">&#8635;</a>
                <a href="pull:" title="Pull" style="text-decoration: none; color: #cccccc; font-size: 16px; margin-left: 10px;">&#8659;</a>
                <a href="push:" title="Push" style="text-decoration: none; color: #cccccc; font-size: 16px; margin-left: 10px;">&#8657;</a>
            </span>
        </div>
        """
        self._ui_built = False
        self._build_ui()
        self.refresh()

    def _encode_link_payload(self, text: str) -> str:
        if not text:
            return ""
        return base64.urlsafe_b64encode(text.encode("utf-8")).decode("ascii").rstrip("=")

    def _decode_link_payload(self, text: str) -> str:
        if not text:
            return ""
        padding = (-len(text)) % 4
        if padding:
            text = text + ("=" * padding)
        return base64.urlsafe_b64decode(text.encode("ascii")).decode("utf-8", errors="replace")

    def _make_link(self, action: str, payload: str) -> str:
        return f"{action}:{self._encode_link_payload(payload)}"

    def get_repo_dir(self):
        doc = tb.Document.current()
        if not doc:
            return None
        try:
            path = doc.path
            if not path:
                return None
            return os.path.dirname(path)
        except AttributeError:
            return None

    def run_git(self, args):
        if not self.repo_dir:
            self.repo_dir = self.get_repo_dir()
            
        if not self.repo_dir:
            return None
            
        try:
            startupinfo = None
            if os.name == 'nt':
                startupinfo = subprocess.STARTUPINFO()
                startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                
            result = subprocess.run(
                ["git"] + args, 
                cwd=self.repo_dir, 
                capture_output=True, 
                text=True,
                startupinfo=startupinfo,
                encoding='utf-8',
                errors='replace'
            )
            
            if result.stderr and "dubious ownership" in result.stderr:
                self.dubious_ownership_path = self.repo_dir
            else:
                self.dubious_ownership_path = None
                
            return result
        except Exception as e:
            print(f"Git execution error: {str(e)}")
            return None

    # --- VS Code Port Logic ---

    def get_history_items(self):
        # Format: Hash|Parents|Subject|Author|Date|Refs
        res = self.run_git(["log", "-n", "50", "--all", "--reflog", "--date-order", "--pretty=format:%h|%p|%s|%an|%cr|%d"])
        
        if not res or res.returncode != 0:
            return []
            
        lines = res.stdout.splitlines()
        items = []
        for line in lines:
            parts = line.split("|")
            if len(parts) < 6: continue
            
            h_hash = parts[0]
            h_parents = parts[1].split() if parts[1] else []
            h_subj = parts[2]
            h_auth = parts[3]
            h_date = parts[4]
            h_refs = parts[5].strip(" ()").split(", ") if parts[5] else []
            if len(h_refs) == 1 and not h_refs[0]: h_refs = []

            # Post-process refs to handle "HEAD -> master"
            processed_refs = []
            for ref in h_refs:
                if " -> " in ref:
                    # e.g., "HEAD -> master" splits to ["HEAD", "master"]
                    parts_ref = ref.split(" -> ")
                    processed_refs.extend(parts_ref)
                else:
                    processed_refs.append(ref)
            h_refs = processed_refs

            items.append({
                "id": h_hash,
                "parentIds": h_parents,
                "author": h_auth,
                "subject": h_subj,
                "date": h_date,
                "refs": h_refs
            })
        return items

    def to_view_models(self, history_items):
        # VS Code colors
        colors = [
            '#FFB000', '#DC267F', '#994F00', '#40B0A6', '#B66DFF'
        ]
        historyItemRefColor = '#0098FF' # chartsBlue equivalent

        color_index = -1
        view_models = []

        for index, item in enumerate(history_items):
            # Previous output becomes current input
            output_swimlanes_prev = view_models[-1]['outputSwimlanes'] if view_models else []
            input_swimlanes = [n.copy() for n in output_swimlanes_prev]
            output_swimlanes = []

            first_parent_added = False

            # Add first parent to output
            if len(item['parentIds']) > 0:
                for node in input_swimlanes:
                    if node['id'] == item['id']:
                        if not first_parent_added:
                            output_swimlanes.append({
                                'id': item['parentIds'][0],
                                'color': node['color']
                            })
                            first_parent_added = True
                        continue
                    
                    output_swimlanes.append(node.copy())
            else:
                # No parents (initial commit), pass through others
                for node in input_swimlanes:
                    if node['id'] == item['id']:
                        continue
                    output_swimlanes.append(node.copy())

            # Add unprocessed parents to output (forks)
            start_idx = 1 if first_parent_added else 0
            for i in range(start_idx, len(item['parentIds'])):
                # Assign new color
                color_index = (color_index + 1) % len(colors)
                color_identifier = colors[color_index]
                
                output_swimlanes.append({
                    'id': item['parentIds'][i],
                    'color': color_identifier
                })
            
            # Determine kind (HEAD logic omitted for simplicity, treating all as nodes)
            kind = 'node'
            if any("HEAD" in r for r in item['refs']):
                kind = 'HEAD'

            view_models.append({
                'historyItem': item,
                'kind': kind,
                'inputSwimlanes': input_swimlanes,
                'outputSwimlanes': output_swimlanes
            })

        return view_models

    def create_badge_svg(self, text, bg_color, text_color, icon_char=None):
        cache_key = (text, bg_color, text_color, icon_char)
        cached = self._badge_cache.get(cache_key)
        if cached is not None:
            return cached

        # Heuristic width calculation
        # Base padding: 12px (6px each side)
        # Char width: ~7px avg (Arial 11px)
        # Icon width: 14px if present
        
        text_width = 0
        for char in text:
            if char.isupper(): text_width += 8
            else: text_width += 6.5
            
        width = int(text_width + 16)
        start_x = 8
        
        if icon_char:
            width += 14
            start_x += 14
            
        height = 18
        radius = 9 # Full pill
        
        # Scale for high DPI
        s = 2.0
        w_s = width * s
        h_s = height * s
        r_s = radius * s
        font_size_s = 11 * s
        
        icon_svg = ""
        if icon_char:
            # Simple text-based icon for now, positioned left
            icon_x = 8 * s
            icon_y = (height/2 + 4) * s
            icon_svg = f'<text x="{icon_x}" y="{icon_y}" font-family="Segoe UI Symbol, Arial Unicode MS, sans-serif" font-size="{font_size_s}" fill="{text_color}" text-anchor="middle" font-weight="normal">{icon_char}</text>'
            
        text_x = (start_x + text_width/2) * s
        text_y = (height/2 + 3.5) * s # Vertical center adjustment
        
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w_s}" height="{h_s}" viewBox="0 0 {w_s} {h_s}">
            <rect x="0" y="0" width="{w_s}" height="{h_s}" rx="{r_s}" ry="{r_s}" fill="{bg_color}" />
            {icon_svg}
            <text x="{text_x}" y="{text_y}" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="{font_size_s}" fill="{text_color}" text-anchor="middle" font-weight="600">{text}</text>
        </svg>'''
        
        b64 = base64.b64encode(svg.encode('utf-8')).decode('utf-8')
        result = f'<img src="data:image/svg+xml;base64,{b64}" width="{width}" height="{height}" style="vertical-align: middle;" />'
        self._badge_cache[cache_key] = result
        return result

    def create_button_svg(self, text, bg_color):
        cache_key = (text, bg_color)
        cached = self._svg_cache.get(cache_key)
        if cached is not None:
            return cached

        # Specific SVG generator for action buttons (Add, Discard, etc)
        # Needs to be small and capsule shaped
        
        text_width = 0
        for char in text:
            text_width += 9 # Increased char width estimation for larger font
            
        width = int(text_width + 16) # Increased padding
        height = 24 # Increased height (was 16)
        radius = 6 # Increased radius
        
        s = 2.0
        w_s = width * s
        h_s = height * s
        r_s = radius * s
        font_size_s = 14 * s # Increased font size (was 10)
        
        text_x = (width/2) * s
        text_y = (height/2 + 4.5) * s # Adjusted vertical center
        
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w_s}" height="{h_s}" viewBox="0 0 {w_s} {h_s}">
            <rect x="0" y="0" width="{w_s}" height="{h_s}" rx="{r_s}" ry="{r_s}" fill="{bg_color}" />
            <text x="{text_x}" y="{text_y}" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="{font_size_s}" fill="#ffffff" text-anchor="middle" font-weight="600">{text}</text>
        </svg>'''
        
        b64 = base64.b64encode(svg.encode('utf-8')).decode('utf-8')
        # Wrap in anchor tag logic is handled by caller, this just returns the IMG tag
        result = f'<img src="data:image/svg+xml;base64,{b64}" width="{width}" height="{height}" style="vertical-align: middle;" />'
        self._svg_cache[cache_key] = result
        return result

    def render_graph_svg(self, view_model):
        SWIMLANE_WIDTH = 11
        SWIMLANE_HEIGHT = 22
        # Use thicker strokes and larger radius to compensate for visual scaling if needed,
        # but since we are scaling everything up by 2.0 and then down by 2.0 via CSS,
        # the logical pixel size should remain 1:1.
        # However, if lines look too thin, we can slightly increase base stroke width.
        
        # VS Code uses 1px stroke logically.
        CIRCLE_RADIUS = 4
        CIRCLE_STROKE_WIDTH = 2
        LINE_STROKE_WIDTH = 1.5 # Slightly thicker than 1px for better visibility

        # Scale 2.0 for HiDPI, then scale down with CSS
        SCALE = 2.0
        
        def s(val): return val * SCALE

        history_item = view_model['historyItem']
        input_swimlanes = view_model['inputSwimlanes']
        output_swimlanes = view_model['outputSwimlanes']

        # Find input index
        input_index = -1
        for i, node in enumerate(input_swimlanes):
            if node['id'] == history_item['id']:
                input_index = i
                break
        
        # Circle index
        circle_index = input_index if input_index != -1 else len(input_swimlanes)

        # Circle color
        circle_color = '#0098FF' # Default
        if circle_index < len(output_swimlanes):
            circle_color = output_swimlanes[circle_index]['color']
        elif circle_index < len(input_swimlanes):
            circle_color = input_swimlanes[circle_index]['color']

        # SVG Construction
        # Calculate width based on max lanes
        width = (max(len(input_swimlanes), len(output_swimlanes), 1) + 1) * SWIMLANE_WIDTH
        
        svg_parts = []
        svg_parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{s(width)}" height="{s(SWIMLANE_HEIGHT)}" viewBox="0 0 {s(width)} {s(SWIMLANE_HEIGHT)}">')

        # Helper for path
        def create_path(d, color, stroke_width=1):
            return f'<path d="{d}" stroke="{color}" stroke-width="{s(stroke_width)}" fill="none" stroke-linecap="round" />'

        output_swimlane_index = 0
        for index in range(len(input_swimlanes)):
            color = input_swimlanes[index]['color']
            
            if input_swimlanes[index]['id'] == history_item['id']:
                # Current commit
                if index != circle_index:
                    # Merge curve (Top -> Mid)
                    x_start = s(SWIMLANE_WIDTH * (index + 1))
                    y_start = 0
                    x_end = s(SWIMLANE_WIDTH * (circle_index + 1))
                    y_end = s(SWIMLANE_HEIGHT / 2) # 11
                    
                    d = f"M {x_start} {y_start} C {x_start} {y_end}, {x_end} {y_start}, {x_end} {y_end}"
                    svg_parts.append(create_path(d, color))
                else:
                    output_swimlane_index += 1
            else:
                # Pass through
                if output_swimlane_index < len(output_swimlanes) and \
                   input_swimlanes[index]['id'] == output_swimlanes[output_swimlane_index]['id']:
                    
                    x_in = s(SWIMLANE_WIDTH * (index + 1))
                    x_out = s(SWIMLANE_WIDTH * (output_swimlane_index + 1))
                    
                    if index == output_swimlane_index:
                        # Straight line
                        d = f"M {x_in} 0 V {s(SWIMLANE_HEIGHT)}"
                        svg_parts.append(create_path(d, color))
                    else:
                        # Shift
                        y_bot = s(SWIMLANE_HEIGHT)
                        d = f"M {x_in} 0 C {x_in} {s(SWIMLANE_HEIGHT/2)}, {x_out} {s(SWIMLANE_HEIGHT/2)}, {x_out} {y_bot}"
                        svg_parts.append(create_path(d, color))
                    
                    output_swimlane_index += 1

        # Add remaining parents (Forking)
        # From Mid to Bottom
        for i in range(1, len(history_item['parentIds'])):
            p_id = history_item['parentIds'][i]
            # Find in output
            parent_out_idx = -1
            for idx, node in enumerate(output_swimlanes):
                if node['id'] == p_id:
                    parent_out_idx = idx
            
            if parent_out_idx != -1:
                # Draw connection from Circle(Mid) to Parent(Bottom)
                x_start = s(SWIMLANE_WIDTH * (circle_index + 1))
                y_start = s(SWIMLANE_HEIGHT / 2)
                x_end = s(SWIMLANE_WIDTH * (parent_out_idx + 1))
                y_end = s(SWIMLANE_HEIGHT)
                
                color = output_swimlanes[parent_out_idx]['color']
                d = f"M {x_start} {y_start} C {x_start} {y_end}, {x_end} {y_start}, {x_end} {y_end}"
                svg_parts.append(create_path(d, color))

        # Vertical Stub: Top to Mid (if input exists)
        if input_index != -1:
            x = s(SWIMLANE_WIDTH * (circle_index + 1))
            d = f"M {x} 0 V {s(SWIMLANE_HEIGHT / 2)}"
            svg_parts.append(create_path(d, input_swimlanes[input_index]['color']))

        # Vertical Stub: Mid to Bottom (if parents exist)
        if len(history_item['parentIds']) > 0:
            x = s(SWIMLANE_WIDTH * (circle_index + 1))
            d = f"M {x} {s(SWIMLANE_HEIGHT / 2)} V {s(SWIMLANE_HEIGHT)}"
            svg_parts.append(create_path(d, circle_color))

        # Draw Circle
        cx = s(SWIMLANE_WIDTH * (circle_index + 1))
        cy = s(SWIMLANE_HEIGHT / 2)
        
        if view_model['kind'] == 'HEAD':
             svg_parts.append(f'<circle cx="{cx}" cy="{cy}" r="{s(CIRCLE_RADIUS + 2)}" fill="{circle_color}" />')
             svg_parts.append(f'<circle cx="{cx}" cy="{cy}" r="{s(CIRCLE_RADIUS)} " fill="#fff" />')
        else:
             svg_parts.append(f'<circle cx="{cx}" cy="{cy}" r="{s(CIRCLE_RADIUS)}" fill="{circle_color}" stroke="#1e1e1e" stroke-width="{s(1)}" />')

        svg_parts.append('</svg>')
        return "".join(svg_parts), width * SCALE, SWIMLANE_HEIGHT * SCALE

    def get_css(self):
        return """
        <style>
            body { 
                background-color: transparent;
                color: #cccccc; 
                font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif;
                font-size: 13px; 
                margin: 0; 
                padding: 0; 
                user-select: none;
                -webkit-user-select: none;
                cursor: default;
                height: 100%;
                overflow: hidden;
            }
            img {
                -webkit-user-drag: none;
            }
            table { 
                border-collapse: collapse; 
                border-spacing: 0;
                width: 100%;
                table-layout: fixed;
            }
            tr { 
                height: 22px;
                background-color: transparent;
            }
            
            td.cell { 
                padding: 0; 
                vertical-align: top;
                white-space: nowrap;
                height: 22px;
                overflow: hidden;
            }
            
            a.row-link { 
                text-decoration: none; 
                color: inherit; 
                display: block; 
                height: 100%; 
                width: 100%;
                line-height: 22px;
            }
            
            img.graph-img {
                vertical-align: middle;
                margin-right: 0px;
                display: inline-block;
            }
            
            span.text-content {
                display: inline-flex;
                align-items: center;
                vertical-align: middle;
                height: 100%;
            }
            
            .hash { color: #569cd6; font-family: Consolas, monospace; margin-right: 8px; flex-shrink: 0; }
            .msg { color: #cccccc; font-weight: 600; flex-grow: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; margin-right: 8px; }
            .meta { color: #858585; font-size: 11px; flex-shrink: 0; white-space: nowrap; }
            
            .refs-container {
                display: inline-flex;
                align-items: center;
                margin-right: 8px;
                flex-shrink: 0;
            }

            .ref-badge { 
                display: inline-block;
                background-color: #252526; 
                color: #cccccc; 
                border-radius: 10px; 
                padding: 0px 8px; 
                margin-right: 4px; 
                font-size: 11px;
                border: 1px solid #3e3e42;
                line-height: 16px;
                height: 16px;
                font-weight: normal;
                white-space: nowrap;
            }
            .ref-head { background-color: #007acc; color: #ffffff; border-color: #007acc; font-weight: 600; }
            .ref-remote { background-color: #652d90; color: #ffffff; border-color: #652d90; }
            .ref-tag { background-color: #ab5a00; color: #ffffff; border-color: #ab5a00; }
            .ref-branch { background-color: transparent; color: #cccccc; border-color: #424242; }
            
            /* Changes List Styles */
            .view-container {
                display: flex;
                flex-direction: column;
                height: 100%;
                overflow: hidden;
            }
            .section-header {
                font-weight: bold;
                font-size: 11px;
                text-transform: uppercase;
                padding: 6px 8px;
                background-color: #252526;
                color: #cccccc;
                flex-shrink: 0;
                display: flex;
                justify-content: space-between;
                align-items: center;
                border-bottom: 1px solid #303031;
            }
            .scroll-list {
                flex-grow: 1;
                overflow-y: auto;
                padding: 4px 0;
            }
            .change-row {
                display: flex;
                justify-content: space-between;
                align-items: center;
                padding: 4px 8px;
                font-family: Consolas, monospace;
                font-size: 13px;
                color: #cccccc;
                cursor: default;
            }
            .change-row:hover {
                background-color: #2a2d2e;
            }
            .change-info {
                white-space: nowrap;
                overflow: hidden;
                text-overflow: ellipsis;
                margin-right: 8px;
                display: flex;
                align-items: center;
            }
            .change-actions a {
                text-decoration: none;
                margin-left: 6px;
                font-size: 11px;
                color: #ffffff;
                border: 1px solid #454545;
                padding: 2px 6px;
                border-radius: 3px;
                background-color: #3a3d41;
                font-weight: 600;
                display: inline-block;
                vertical-align: middle;
            }
            .change-actions a:hover {
                background-color: #0090f1;
                border-color: #0090f1;
            }
            
            /* Scrollbar styling */
            ::-webkit-scrollbar {
                width: 10px;
                height: 10px;
            }
            ::-webkit-scrollbar-track {
                background: transparent;
            }
            ::-webkit-scrollbar-thumb {
                background: #424242;
            }
            ::-webkit-scrollbar-thumb:hover {
                background: #4f4f4f;
            }
            ::-webkit-scrollbar-corner {
                background: transparent;
            }
        </style>
        """

    def wrap_html(self, content):
        return f"""
        <html>
        <head>{self.get_css()}</head>
        <body>
            {content}
        </body>
        </html>
        """

    def generate_history_html(self, view_models):
        if not view_models: return ""
        
        SCALE = 2.0
        
        table_rows = []
        for vm in view_models:
            item = vm['historyItem']
            svg_xml, svg_w, svg_h = self.render_graph_svg(vm)
            svg_b64 = base64.b64encode(svg_xml.encode('utf-8')).decode('utf-8')
            
            # Refs
            refs_html = ""
            for ref in item['refs']:
                bg_color = "#429542" # 默认绿色（分支）
                text_color = "#cccccc"
                icon_char = None
                
                if ref == "HEAD":
                    bg_color = "#007acc" # Blue
                    text_color = "#ffffff"
                    # No icon for HEAD usually
                elif "origin/" in ref:
                    bg_color = "#652d90" # Purple
                    text_color = "#ffffff"
                    icon_char = "&#9729;" # Cloud
                elif "tag: " in ref:
                    bg_color = "#ab5a00" # Orange
                    text_color = "#ffffff"
                    ref = ref.replace("tag: ", "")
                    icon_char = "&#127991;" # Tag
                else:
                    # Local branch
                    bg_color = "#429542" # Dark Grey
                    text_color = "#ffffff"
                    # Use a simple dot or nothing if font support is issue
                    # The user liked the "target" icon. Let's try a bullet or circle char
                    icon_char = "&#9673;" # Fisheye / Bullseye-like circle

                # Generate SVG badge
                badge_img = self.create_badge_svg(ref, bg_color, text_color, icon_char)
                refs_html += f'{badge_img}&nbsp;'
            
            refs_span = ""
            if refs_html:
                # Add extra spacing after the group of badges
                refs_span = f'<span class="refs-container">{refs_html}</span>&nbsp;&nbsp;'

            # Content (Image + Text in one block)
            # Use vertical-align middle to align text with graph
            # Add padding-right to image to separate from text
            
            # Note: Qt's HTML engine is limited. Flexbox is not supported.
            # We use &nbsp; to enforce spacing.
            content_html = f'''
            <a href="checkout:{item["id"]}" class="row-link">
                <img src="data:image/svg+xml;base64,{svg_b64}" width="{svg_w / SCALE}" height="{svg_h / SCALE}" class="graph-img" />
                <span class="text-content">
                    <span class="hash">{item["id"]}</span>&nbsp;{refs_span}<span class="msg">{html.escape(item["subject"])}</span>&nbsp;&nbsp;&nbsp;<span class="meta">{html.escape(item["author"])} &bull; {html.escape(item["date"])}</span>
                </span>
            </a>
            '''
            
            table_rows.append(f'<tr><td class="cell">{content_html}</td></tr>')
            
        return self.wrap_html(f"""
            <div class="view-container">
                <div class="scroll-list">
                    <table cellpadding="0" cellspacing="0" border="0">
                        {''.join(table_rows)}
                    </table>
                </div>
            </div>
        """)

    def _build_ui(self):
        if self._ui_built:
            return

        self.panel.clear()

        state_no_doc = self.panel.add_column("state_no_doc")
        state_no_doc.add_label_named("no_doc_msg", "<i>Please save the map to enable Git features.</i>")
        state_no_doc.add_button_callback("Refresh", self.refresh)

        state_not_repo = self.panel.add_column("state_not_repo")
        state_not_repo.add_label_named("not_repo_folder", "")
        state_not_repo.add_label_named("not_repo_msg", "Not a git repository.")
        state_not_repo.add_button_callback("Initialize Repo (git init)", self.on_init)
        state_not_repo.add_button_callback("Refresh", self.refresh)

        state_repo = self.panel.add_column("state_repo")
        state_repo.add_html_view("header_view", self.wrap_html(self._header_html), 34, self.on_header_link_clicked)

        repo_dubious = state_repo.add_column("repo_dubious")
        repo_dubious.add_label("<font color='red'><b>Error: Dubious Ownership</b></font>")
        repo_dubious.add_button_callback("Fix Safe Directory", self.on_fix_safe_directory)

        repo_main = state_repo.add_column("repo_main")
        repo_main.add_text_field("commit_msg", "", self.commit_message, "Message (Ctrl+Enter to commit)")
        repo_main.add_button_callback("Commit", self.on_commit)

        repo_main.add_label_named("changes_header", "<b>Changes (0)</b>")
        repo_main.add_html_view("changes_view", self.wrap_html(""), 130, self.on_changes_link_clicked)

        repo_main.add_label_named("branches_header", "<b>Branches</b>")
        repo_main.add_text_field("new_branch_name", "Create new branch...", self.new_branch_name)
        repo_main.add_button_callback("Create", self.on_create_branch)
        repo_main.add_html_view("branches_view", self.wrap_html(""), 110, self.on_branch_link_clicked)

        repo_main.add_label_named("history_header", "<b>History</b>")
        repo_main.add_html_view("history_view", self.wrap_html(""), 300, self.on_history_link_clicked)

        self.panel.set_widget_visible("state_no_doc", True)
        self.panel.set_widget_visible("state_not_repo", False)
        self.panel.set_widget_visible("state_repo", False)
        self.panel.set_widget_visible("repo_dubious", False)
        self.panel.set_widget_visible("repo_main", True)

        self._ui_built = True

    def refresh(self):
        self._build_ui()

        try:
            self.commit_message = self.panel.get_text_field("commit_msg")
        except:
            pass

        try:
            self.new_branch_name = self.panel.get_text_field("new_branch_name")
        except:
            self.new_branch_name = ""

        self.repo_dir = self.get_repo_dir()
        
        is_git_repo = False
        branches = []
        staged = []
        changes = []
        untracked = []
        history_items = []
        view_models = []
        
        if self.repo_dir and os.path.exists(os.path.join(self.repo_dir, ".git")):
            is_git_repo = True
            branches = self.get_branches()
            staged, changes, untracked = self.get_changes_categorized()
            
            # Fetch history
            try:
                history_items = self.get_history_items()
                if history_items:
                    view_models = self.to_view_models(history_items)
            except Exception as e:
                print(f"Error fetching history: {e}")
        
        if not self.repo_dir:
            self.panel.set_widget_visible("state_no_doc", True)
            self.panel.set_widget_visible("state_not_repo", False)
            self.panel.set_widget_visible("state_repo", False)
            self.panel.set_label_text("no_doc_msg", "<i>Please save the map to enable Git features.</i>")
            return

        if not is_git_repo:
            self.panel.set_widget_visible("state_no_doc", False)
            self.panel.set_widget_visible("state_not_repo", True)
            self.panel.set_widget_visible("state_repo", False)
            self.panel.set_label_text("not_repo_folder", f"Folder: {self.repo_dir}")
            self.panel.set_label_text("not_repo_msg", "Not a git repository.")
            return

        self.panel.set_widget_visible("state_no_doc", False)
        self.panel.set_widget_visible("state_not_repo", False)
        self.panel.set_widget_visible("state_repo", True)
        self.panel.set_html_view("header_view", self.wrap_html(self._header_html))
        
        if self.dubious_ownership_path:
            self.panel.set_widget_visible("repo_dubious", True)
            self.panel.set_widget_visible("repo_main", False)
            return
        else:
            self.panel.set_widget_visible("repo_dubious", False)
            self.panel.set_widget_visible("repo_main", True)

        self.panel.set_text_field("commit_msg", self.commit_message)
        
        count_staged = len(staged)
        count_pending = len(changes) + len(untracked)
        total_count = count_staged + count_pending
        
        self.panel.set_label_text("changes_header", f"<b>Changes ({total_count})</b>")
        
        changes_html = '<div class="changes-container">'
        has_items = False
        
        # Common base style for the anchor wrapper (transparent, no border)
        # The SVG image itself carries the visual style
        # Added vertical-align: -3px to shift buttons down relative to larger text
        anchor_style = "text-decoration: none; margin-left: 6px; display: inline-block; vertical-align: -3px;"
        
        # Pre-generate button SVGs for common actions
        img_add = self.create_button_svg("Add", "#2da44e")
        img_discard = self.create_button_svg("Discard", "#cf222e")
        img_ignore = self.create_button_svg("Ignore", "#d29922")
        img_unstage = self.create_button_svg("Unstage", "#6e7681")
        
        if staged:
            has_items = True
            for status, path in staged:
                changes_html += f'''
                <div class="change-row">
                    <span class="change-info" style="color: #8de28d">[S] {html.escape(path)}</span>
                    <span class="change-actions">
                        <a href="{self._make_link("unstage", path)}" style="{anchor_style}">{img_unstage}</a>
                    </span>
                </div>
                '''

        if changes:
            has_items = True
            for status, path in changes:
                changes_html += f'''
                <div class="change-row">
                    <span class="change-info" style="color: #e2c08d">[{status}] {html.escape(path)}</span>
                    <span class="change-actions">
                        <a href="{self._make_link("stage", path)}" style="{anchor_style}">{img_add}</a>
                        <a href="{self._make_link("discard", path)}" style="{anchor_style}">{img_discard}</a>
                        <a href="{self._make_link("ignore", path)}" style="{anchor_style}">{img_ignore}</a>
                    </span>
                </div>
                '''
                
        if untracked:
            has_items = True
            for status, path in untracked:
                changes_html += f'''
                <div class="change-row">
                    <span class="change-info" style="color: #8ddbe2">[?] {html.escape(path)}</span>
                    <span class="change-actions">
                        <a href="{self._make_link("stage", path)}" style="{anchor_style}">{img_add}</a>
                        <a href="{self._make_link("discard", path)}" style="{anchor_style}">{img_discard}</a>
                        <a href="{self._make_link("ignore", path)}" style="{anchor_style}">{img_ignore}</a>
                    </span>
                </div>
                '''
        
        if not has_items:
            changes_html += '<div class="change-row" style="color: #858585; font-style: italic;">No pending changes</div>'
            
        changes_html += '</div>'
            
        self.panel.set_html_view("changes_view", self.wrap_html(changes_html))
        
        self.panel.set_text_field("new_branch_name", self.new_branch_name)

        # Use HTML view for branches for better styling
        branches_html = '<div class="changes-container" style="height: 100px;">'
        
        for b in branches:
            is_current = b.startswith("* ")
            branch_name = b[2:].strip() if is_current else b.strip()
            
            # Badge style for branch name
            bg_color = "#007acc" if is_current else "#3a3d41"
            text_color = "#ffffff"
            
            # Generate simple badge using span style instead of SVG for performance in list
            # Adjusted size: ~0.75x of previous huge size (18px * 0.75 ≈ 13.5px)
            # Let's go with 14px font, slightly smaller padding
            badge_style = f"background-color: {bg_color}; color: {text_color}; padding: 4px 10px; border-radius: 12px; font-size: 14px; font-weight: 600; display: inline-block; min-width: 70px; text-align: center;"
            
            actions_html = ""
            if not is_current:
                 actions_html = f'''
                 <a href="{self._make_link("checkout", branch_name)}" title="Checkout" style="font-size: 16px;">&#10145;</a>
                 <a href="{self._make_link("delete", branch_name)}" title="Delete" style="font-size: 16px;">&#128465;</a>
                 '''
            
            actions_html += f'<a href="{self._make_link("push", branch_name)}" title="Push" style="font-size: 16px;">&#8679;</a>'

            branches_html += f'''
            <div class="change-row" style="padding: 4px 4px;">
                <span class="change-info" style="display: flex; align-items: center;">
                    <span style="{badge_style}">{html.escape(branch_name)}</span>
                    {' <span style="color: #858585; font-size: 12px; margin-left: 8px;">(Current)</span>' if is_current else ''}
                </span>
                <span class="change-actions">
                    {actions_html}
                </span>
            </div>
            '''
            
        if not branches:
             branches_html += '<div class="change-row">No branches found</div>'
             
        branches_html += '</div>'
        self.panel.set_html_view("branches_view", self.wrap_html(branches_html))

        try:
            if view_models:
                html_content = self.generate_history_html(view_models)
                self.panel.set_html_view("history_view", html_content)
            else:
                self.panel.set_html_view(
                    "history_view",
                    self.wrap_html('<div class="change-row" style="color: #858585; font-style: italic;">No history found</div>'),
                )
        except Exception as e:
             print(f"Error generating history graph: {e}")
             import traceback
             traceback.print_exc()
             self.panel.set_html_view(
                 "history_view",
                 self.wrap_html(f'<div class="change-row" style="color: #cf222e;">Error loading history: {html.escape(str(e))}</div>'),
             )

    def on_header_link_clicked(self, link):
        print(f"Header link clicked: {link}")
        parts = link.split(":", 1)
        action = parts[0]
        
        if action == "refresh":
            self.refresh()
        elif action == "pull":
            self.on_pull()
        elif action == "push":
            self.on_push()

    def on_history_link_clicked(self, link):
        if link.startswith("checkout:"):
            hash = link.split(":")[1]
            print(f"Checkout {hash} requested via link")
            self.run_git(["checkout", hash])
            self.reload_map()
            self.refresh()

    def get_changes_categorized(self):
        res = self.run_git(["status", "--porcelain"])
        staged = []
        changes = []
        untracked = []
        
        if res and res.returncode == 0:
            for line in res.stdout.splitlines():
                if len(line) < 4: continue
                x = line[0]
                y = line[1]
                path = line[3:]
                
                if x == '?' and y == '?':
                    untracked.append(('?', path))
                else:
                    if x != ' ' and x != '?':
                        staged.append((x, path))
                    if y != ' ':
                        changes.append((y, path))
                        
        return staged, changes, untracked

    def get_branches(self):
        res = self.run_git(["branch", "--list"])
        if res and res.returncode == 0:
            return res.stdout.splitlines()
        return []

    def on_create_branch(self):
        try:
            name = self.panel.get_text_field("new_branch_name")
            if not name:
                print("Branch name empty")
                return
            
            # Create and checkout
            self.run_git(["checkout", "-b", name])
            self.new_branch_name = ""
            self.reload_map()
            self.refresh()
        except Exception as e:
            print(f"Create branch failed: {e}")

    def on_init(self):
        self.run_git(["init"])
        self.refresh()

    def on_fix_safe_directory(self):
        if not self.repo_dir: return
        safe_path = self.repo_dir.replace('\\', '/')
        self.run_git(["config", "--global", "--add", "safe.directory", safe_path])
        self.dubious_ownership_path = None
        self.refresh()

    def on_branch_link_clicked(self, link):
        print(f"Branch link clicked: {link}")
        parts = link.split(":", 1)
        action = parts[0]
        branch = self._decode_link_payload(parts[1]) if len(parts) > 1 else ""
        
        if action == "checkout":
            self.run_git(["checkout", branch])
            self.reload_map()
            self.refresh()
        elif action == "delete":
            self.run_git(["branch", "-D", branch])
            self.refresh()
        elif action == "push":
            self.run_git(["push", "--set-upstream", "origin", branch])
            self.refresh()

    def on_changes_link_clicked(self, link):
        print(f"Changes link clicked: {link}")
        parts = link.split(":", 1)
        action = parts[0]
        path = self._decode_link_payload(parts[1]) if len(parts) > 1 else ""
        
        if action == "stage":
            self.run_git(["add", path])
            self.refresh()
        elif action == "unstage":
            self.run_git(["reset", "HEAD", path])
            self.refresh()
        elif action == "discard":
            # Check if untracked to decide between clean and checkout
            staged, changes, untracked = self.get_changes_categorized()
            is_untracked = any(p == path for _, p in untracked)
            
            if is_untracked:
                 self.run_git(["clean", "-fd", "--", path])
            else:
                 self.run_git(["checkout", "HEAD", "--", path])
            
            self.reload_map()
            self.refresh()
        elif action == "ignore":
            gitignore_path = os.path.join(self.repo_dir, ".gitignore")
            try:
                needs_newline = False
                if os.path.exists(gitignore_path):
                    try:
                        with open(gitignore_path, "rb") as f:
                            f.seek(-1, 2)
                            last_char = f.read(1)
                            if last_char != b'\n':
                                needs_newline = True
                    except:
                        pass
                
                with open(gitignore_path, "a", encoding='utf-8') as f:
                    if needs_newline:
                        f.write("\n")
                    f.write(f"{path}\n")
                
                print(f"Added {path} to .gitignore")
                self.refresh()
            except Exception as e:
                print(f"Failed to update .gitignore: {e}")

    def on_commit(self):
        doc = tb.Document.current()
        if doc:
            try:
                doc.save()
            except Exception as e:
                print(f"Save failed: {e}")

        try:
            msg = self.panel.get_text_field("commit_msg")
            self.commit_message = msg
        except:
            msg = "Update map"

        if not msg.strip():
            print("Commit message empty")
            return

        # Check if we have staged changes
        staged, changes, untracked = self.get_changes_categorized()
        
        # If nothing staged, but we have changes, maybe we should auto-stage tracked files?
        # Standard git behavior is to only commit staged.
        # But for convenience, if nothing is staged, we could stage the current map file if it's modified?
        
        if not staged:
            # If the map file itself is modified but not staged, stage it?
            # Or just tell user to stage things.
            # Let's try to stage the current map file if it's in the modified list
            path = doc.path if doc else None
            map_filename = os.path.basename(path) if path else ""
            
            # Check if map file is in changes
            map_modified = False
            if path:
                # changes list contains (status, path)
                # paths from git status are relative to repo root.
                # We need to match correctly.
                # Simplest is just:
                if any(p.endswith(map_filename) for _, p in changes):
                     map_modified = True
            
            if map_modified:
                print(f"Auto-staging map file: {map_filename}")
                self.run_git(["add", path])
                # Re-check staged
                staged, _, _ = self.get_changes_categorized()

        if not staged:
            print("Nothing to commit (stage files first)")
            return

        self.run_git(["commit", "-m", msg])
        self.commit_message = "" 
        self.refresh()

    def on_pull(self):
        self.run_git(["pull"])
        self.reload_map()
        self.refresh()

    def on_push(self):
        self.run_git(["push"])
        self.refresh()

    # Dead code from previous checkbox-based UI
    # def on_discard_changes(self):
    #     ...
    # def on_ignore_changes(self):
    #     ...

    def reload_map(self):
        doc = tb.Document.current()
        if doc:
            try:
                doc.reload()
            except:
                pass

# Start
GitManager()
