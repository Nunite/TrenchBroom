import tb
import subprocess
import os
import html
import re
import base64

class GitManager:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Git Manager")
        self.repo_dir = self.get_repo_dir()
        self.commit_message = ""
        self.new_branch_name = ""
        self.dubious_ownership_path = None
        self.refresh()

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
        return f'<img src="data:image/svg+xml;base64,{b64}" width="{width}" height="{height}" style="vertical-align: middle;" />'

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
            
            content_html = f'''
            <a href="checkout:{item["id"]}" class="row-link">
                <img src="data:image/svg+xml;base64,{svg_b64}" width="{svg_w / SCALE}" height="{svg_h / SCALE}" class="graph-img" />
                <span class="text-content">
                    <span class="hash">{item["id"]}</span>{refs_span}<span class="msg">{html.escape(item["subject"])}</span><span class="meta">{html.escape(item["author"])} &bull; {html.escape(item["date"])}</span>
                </span>
            </a>
            '''
            
            table_rows.append(f'<tr><td class="cell">{content_html}</td></tr>')
            
        style = """
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
            
            /* Use a generic font that might have these glyphs or fallback */
            @font-face {
                font-family: 'codicon';
                src: url('codicon.ttf') format('truetype');
            }
            .icon-branch {
                font-family: 'Segoe UI Symbol', 'Arial Unicode MS', sans-serif;
                margin-right: 2px;
                font-size: 10px;
            }
        </style>
        """
        
        return f"""
        <html>
        <head>{style}</head>
        <body>
            <table cellpadding="0" cellspacing="0" border="0">
                {''.join(table_rows)}
            </table>
        </body>
        </html>
        """

    def refresh(self):
        try:
            new_msg = self.panel.get_text_field("commit_msg")
            if new_msg:
                self.commit_message = new_msg
        except:
            pass
        
        # Save new branch name if typed
        try:
            self.new_branch_name = self.panel.get_text_field("new_branch_name")
        except:
            self.new_branch_name = ""

        self.panel.clear()
        
        self.repo_dir = self.get_repo_dir()
        if not self.repo_dir:
            self.panel.add_label("<i>Please save the map to enable Git features.</i>")
            self.panel.add_button_callback("Refresh", self.refresh)
            return

        if not os.path.exists(os.path.join(self.repo_dir, ".git")):
            self.panel.add_label(f"Folder: {self.repo_dir}")
            self.panel.add_label("Not a git repository.")
            self.panel.add_button_callback("Initialize Repo (git init)", self.on_init)
            self.panel.add_button_callback("Refresh", self.refresh)
            return

        # Header / Branch
        branch = self.get_current_branch()
        self.panel.add_label(f"<h2>Branch: {html.escape(branch)}</h2>")
        
        if self.dubious_ownership_path:
            self.panel.add_label("<font color='red'><b>Error: Dubious Ownership</b></font>")
            self.panel.add_button_callback(f"Fix Safe Directory", self.on_fix_safe_directory)
            self.panel.add_button_callback("Refresh", self.refresh)
            return 
        
        # Toolbar
        self.panel.add_button_callback("Refresh Status", self.refresh)
        self.panel.add_button_callback("Pull (Reload Map)", self.on_pull)
        self.panel.add_button_callback("Push", self.on_push)
        
        # Branch Management
        self.panel.add_label("<b>Branch Management</b>")
        self.panel.add_text_field("new_branch_name", "New Branch Name", self.new_branch_name)
        self.panel.add_button_callback("Create Branch", self.on_create_branch)
        
        branches = self.get_branches()
        branch_items = []
        current_branch_index = 0
        for idx, b in enumerate(branches):
            display = b
            if b.startswith("* "):
                display = b[2:] + " (Current)"
                current_branch_index = idx
            else:
                display = b.strip()
            branch_items.append(display)
            
        self.panel.add_list_widget("branch_list", branch_items, None)
        # Select current branch in list
        # Note: add_list_widget doesn't support setting selection index in current API, 
        # but we can provide context menu for operations.
        
        self.panel.set_list_widget_context_menu("branch_list", [
            ("Checkout", self.on_checkout_branch_from_list),
            ("Delete", self.on_delete_branch_from_list),
            ("Push (Publish)", self.on_push_branch_from_list)
        ])

        # Commit Area
        self.panel.add_label("<b>Commit</b>")
        self.panel.add_text_field("commit_msg", "Message", self.commit_message)
        self.panel.add_button_callback("Commit (Save, Add & Commit)", self.on_commit)
        
        # Changes List
        staged, changes, untracked = self.get_changes_categorized()
        self.current_changes = [] 
        
        if staged:
            self.panel.add_label(f"<b>Staged Changes ({len(staged)})</b>")
            for status, path in staged:
                self.panel.add_label(f"<font color='#8de28d'>[Staged] {html.escape(path)}</font>")
        
        if changes or untracked:
            count = len(changes) + len(untracked)
            self.panel.add_label(f"<b>Pending Changes ({count})</b>")
            
            def add_chk(status_code, path, color):
                key = f"chk_{path}"
                label = f"[{status_code}] {path}"
                self.panel.add_checkbox(key, label, 0)
                self.current_changes.append(path)
                
            for status, path in changes:
                add_chk(status, path, "#e2c08d")
            for status, path in untracked:
                add_chk("?", path, "#8ddbe2")
                
            self.panel.add_button_callback("Discard Selected Changes", self.on_discard_changes)
        
        if not (staged or changes or untracked):
            self.panel.add_label("<br><i>No pending changes</i><br>")

        # History List
        self.panel.add_label("<b>History (Graph)</b>")
        
        try:
            items = self.get_history_items()
            if items:
                view_models = self.to_view_models(items)
                html_content = self.generate_history_html(view_models)
                self.panel.add_html_view("history_view", html_content, 300, self.on_history_link_clicked)
            else:
                self.panel.add_label("<i>No history found.</i>")
        except Exception as e:
             print(f"Error generating history graph: {e}")
             import traceback
             traceback.print_exc()
             self.panel.add_label(f"<i>Error loading history: {e}</i>")

    def on_history_link_clicked(self, link):
        if link.startswith("checkout:"):
            hash = link.split(":")[1]
            print(f"Checkout {hash} requested via link")
            self.run_git(["checkout", hash])
            self.reload_map()
            self.refresh()

    def get_current_branch(self):
        res = self.run_git(["rev-parse", "--abbrev-ref", "HEAD"])
        if res and res.returncode == 0:
            return res.stdout.strip()
        return "HEAD (Detached?)"

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

    def on_checkout_branch_from_list(self, index):
        branches = self.get_branches()
        if 0 <= index < len(branches):
            raw_name = branches[index]
            branch_name = raw_name.replace("*", "").strip()
            print(f"Checkout branch: {branch_name}")
            self.run_git(["checkout", branch_name])
            self.reload_map()
            self.refresh()

    def on_delete_branch_from_list(self, index):
        branches = self.get_branches()
        if 0 <= index < len(branches):
            raw_name = branches[index]
            if "*" in raw_name:
                print("Cannot delete current branch")
                return
            branch_name = raw_name.strip()
            print(f"Deleting branch: {branch_name}")
            self.run_git(["branch", "-D", branch_name])
            self.refresh()

    def on_push_branch_from_list(self, index):
        branches = self.get_branches()
        if 0 <= index < len(branches):
            raw_name = branches[index]
            branch_name = raw_name.replace("*", "").strip()
            print(f"Pushing branch: {branch_name}")
            # Push with set-upstream
            self.run_git(["push", "--set-upstream", "origin", branch_name])
            self.refresh()

    def on_init(self):
        self.run_git(["init"])
        self.refresh()

    def on_fix_safe_directory(self):
        if not self.repo_dir: return
        safe_path = self.repo_dir.replace('\\', '/')
        self.run_git(["config", "--global", "--add", "safe.directory", safe_path])
        self.dubious_ownership_path = None
        self.refresh()

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

        path = doc.path if doc else None
        if path:
            self.run_git(["add", path])
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

    def on_discard_changes(self):
        to_checkout = []
        to_clean = []
        staged, changes, untracked = self.get_changes_categorized()
        
        for path in self.current_changes:
            try:
                checked = self.panel.get_checkbox(f"chk_{path}")
                if checked:
                    is_modified = any(p == path for _, p in changes)
                    is_untracked = any(p == path for _, p in untracked)
                    
                    if is_modified:
                        to_checkout.append(path)
                    elif is_untracked:
                        to_clean.append(path)
            except:
                pass
                
        if to_checkout:
            self.run_git(["checkout", "HEAD", "--"] + to_checkout)
        if to_clean:
            self.run_git(["clean", "-fd", "--"] + to_clean)
        if to_checkout or to_clean:
            self.reload_map()
            self.refresh()

    def reload_map(self):
        doc = tb.Document.current()
        if doc:
            try:
                doc.reload()
            except:
                pass

# Start
GitManager()
