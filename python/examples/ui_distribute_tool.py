import tb
import math
import random

_ADDON = None

def vec3_sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def vec3_add(a, b):
    return (a[0]+b[0], a[1]+b[1], a[2]+b[2])

def vec3_mul(v, s):
    return (v[0]*s, v[1]*s, v[2]*s)

def vec3_len(v):
    return math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])

def vec3_normalize(v):
    l = vec3_len(v)
    if l == 0: return (0, 0, 1)
    return (v[0]/l, v[1]/l, v[2]/l)

def vec3_cross(a, b):
    return (
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0]
    )

def vec3_dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

def quaternion_from_axis_angle(axis, angle_rad):
    # axis must be normalized
    half_angle = angle_rad * 0.5
    s = math.sin(half_angle)
    return (
        math.cos(half_angle), # w
        axis[0] * s,
        axis[1] * s,
        axis[2] * s
    )

def quaternion_multiply(q1, q2):
    w1, x1, y1, z1 = q1
    w2, x2, y2, z2 = q2
    return (
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2
    )

def quaternion_to_euler(q):
    # Returns (roll, pitch, yaw) in degrees
    # This is a simplified conversion, might need adjustment for TB's coordinate system
    # TB uses Z-up. 
    # Standard conversion to Z-Y-X Euler angles
    w, x, y, z = q
    
    # Pitch (Y-axis rotation)
    t2 = +2.0 * (w * y - z * x)
    t2 = +1.0 if t2 > +1.0 else t2
    t2 = -1.0 if t2 < -1.0 else t2
    pitch = math.asin(t2)
    
    # Roll (X-axis rotation)
    t0 = +2.0 * (w * x + y * z)
    t1 = +1.0 - 2.0 * (x * x + y * y)
    roll = math.atan2(t0, t1)
    
    # Yaw (Z-axis rotation)
    t3 = +2.0 * (w * z + x * y)
    t4 = +1.0 - 2.0 * (y * y + z * z)
    yaw = math.atan2(t3, t4)
    
    return (math.degrees(roll), math.degrees(pitch), math.degrees(yaw))

def catmull_rom_spline(points, steps=10):
    """
    Generate a smooth Catmull-Rom spline from a list of 3D points.
    points: list of (x,y,z) tuples
    steps: number of interpolated points per segment
    """
    if len(points) < 2: return points
    
    # Duplicate endpoints to handle open spline
    # P0, P1, P2, ... Pn
    # Control points: P0, P0, P1, P2, ... Pn, Pn
    ctrl = [points[0]] + list(points) + [points[-1]]
    
    smoothed = []
    
    for i in range(len(points) - 1):
        # Segment P_i to P_{i+1} corresponds to ctrl[i+1] to ctrl[i+2]
        p0 = ctrl[i]
        p1 = ctrl[i+1]
        p2 = ctrl[i+2]
        p3 = ctrl[i+3]
        
        for t_step in range(steps):
            t = t_step / float(steps)
            t2 = t * t
            t3 = t2 * t
            
            # Catmull-Rom formula
            # 0.5 * ( (2*p1) + (-p0 + p2)*t + (2*p0 - 5*p1 + 4*p2 - p3)*t2 + (-p0 + 3*p1 - 3*p2 + p3)*t3 )
            
            x = 0.5 * ((2*p1[0]) + (-p0[0] + p2[0])*t + (2*p0[0] - 5*p1[0] + 4*p2[0] - p3[0])*t2 + (-p0[0] + 3*p1[0] - 3*p2[0] + p3[0])*t3)
            y = 0.5 * ((2*p1[1]) + (-p0[1] + p2[1])*t + (2*p0[1] - 5*p1[1] + 4*p2[1] - p3[1])*t2 + (-p0[1] + 3*p1[1] - 3*p2[1] + p3[1])*t3)
            z = 0.5 * ((2*p1[2]) + (-p0[2] + p2[2])*t + (2*p0[2] - 5*p1[2] + 4*p2[2] - p3[2])*t2 + (-p0[2] + 3*p1[2] - 3*p2[2] + p3[2])*t3)
            
            smoothed.append((x, y, z))
            
    smoothed.append(points[-1])
    return smoothed

def binomial_coeff(n, k):
    if k < 0 or k > n: return 0
    return math.factorial(n) // (math.factorial(k) * math.factorial(n - k))

def bezier_curve(points, steps_per_segment=10):
    """
    Generate a smooth Bezier curve from a list of 3D points.
    Uses all points as control points for a single N-degree Bezier curve.
    """
    n = len(points) - 1
    if n < 1: return points
    
    total_steps = n * steps_per_segment
    result = []
    
    for i in range(total_steps + 1):
        t = i / float(total_steps)
        # Calculate B(t)
        x, y, z = 0.0, 0.0, 0.0
        for k, p in enumerate(points):
            # b_{k,n}(t) = C(n,k) * (1-t)^(n-k) * t^k
            # Use math.pow for safety, though ** is fine
            term = binomial_coeff(n, k) * math.pow(1 - t, n - k) * math.pow(t, k)
            x += p[0] * term
            y += p[1] * term
            z += p[2] * term
        result.append((x, y, z))
        
    return result

class DistributeTool:
    def __init__(self) -> None:
        self._panel = tb.create_plugin_panel("沿路径分布")
        self._raw_points = [] # Original user-selected points
        self._path_points = [] # Points used for distribution (raw or smoothed)
        self._path_length = 0.0
        self._segment_lengths = []
        self._message = "请先选择路径点（顶点或实体）"
        self._selected_chain_ui_index = 0
        self._build_ui()
        self._refresh_status()

    def _build_ui(self) -> None:
        self._panel.clear()
        self._panel.add_label_named("status", self._message)
        
        # Chain detection
        self._detected_chains = self._detect_entity_chains()
        
        # Build combo box items
        # Structure: [Placeholder, Manual, Chain 1, Chain 2, ..., Refresh]
        chain_items = ["--- 选择路径来源 ---", "手动: 从当前选择记录"]
        if self._detected_chains:
            chain_items.extend([f"链 {i+1}: {len(c)} 个节点" for i, c in enumerate(self._detected_chains)])
        else:
            chain_items.append("(未检测到实体链)")
        chain_items.append(">> 刷新列表 <<")

        # Ensure index is valid
        if self._selected_chain_ui_index >= len(chain_items):
             self._selected_chain_ui_index = 0

        try:
            # New API with callback support and optional index
            self._panel.add_combo_box("chain_selector", "路径来源", chain_items, self.on_chain_selected, self._selected_chain_ui_index)
        except TypeError:
            try:
                # API with callback but no index support
                self._panel.add_combo_box("chain_selector", "路径来源", chain_items, self.on_chain_selected)
            except TypeError:
                # Fallback for older API without callback
                self._panel.add_combo_box("chain_selector", "选择实体链", chain_items)
                self._panel.add_button_callback("应用选择", self.op_apply_selection)
        except AttributeError:
             self._panel.add_label("Error: add_combo_box not supported")
        except Exception as e:
            self._panel.add_label(f"Error adding combo: {e}")

        self._panel.add_int_field("count", "生成数量", 5, 2, 100)
        try:
            self._panel.add_checkbox("align_to_path", "沿路径旋转", True)
        except AttributeError:
             # Fallback for older TB versions if any
             self._panel.add_int_field("align_to_path", "沿路径旋转 (0=否, 1=是)", 1, 0, 1)

        try:
            self._panel.add_checkbox("smooth_path", "平滑路径 (样条线)", False)
        except AttributeError:
             self._panel.add_int_field("smooth_path", "平滑路径 (0=否, 1=是)", 0, 0, 1)
             
        try:
            self._panel.add_checkbox("use_bezier", "使用贝塞尔曲线 (不经过点)", False)
        except AttributeError:
             self._panel.add_int_field("use_bezier", "贝塞尔曲线 (0=否, 1=是)", 0, 0, 1)
             
        self._panel.add_float_field("offset_start", "起始偏移 (%)", 0.0, 0.0, 100.0, 1, 5.0)
        self._panel.add_float_field("offset_end", "结束偏移 (%)", 0.0, 0.0, 100.0, 1, 5.0)
        
        self._panel.add_label("基础偏移:")
        self._panel.add_float_field("base_off_x", "X 偏移", 0.0, -4096.0, 4096.0, 1, 16.0)
        self._panel.add_float_field("base_off_y", "Y 偏移", 0.0, -4096.0, 4096.0, 1, 16.0)
        self._panel.add_float_field("base_off_z", "Z 偏移", 0.0, -4096.0, 4096.0, 1, 16.0)
        
        self._panel.add_label("随机化参数:")
        self._panel.add_float_field("rand_pos", "位置抖动", 0.0, 0.0, 128.0, 1, 1.0)
        self._panel.add_float_field("rand_rot_z", "随机旋转 Z (度)", 0.0, 0.0, 360.0, 1, 15.0)
        self._panel.add_float_field("rand_scale", "随机缩放 (%)", 0.0, 0.0, 100.0, 1, 10.0)
        
        self._panel.add_button_callback("2. 分布选中物体", self.op_distribute)
        
        self._panel.add_label("说明: 下拉选择路径源，然后选中要复制的物体点击分布。")

    def _detect_entity_chains(self):
        doc = self._current_document()
        if not doc:
            return []
        
        # Try to use the new entities property if available
        try:
            all_entities = doc.entities
            source = "doc.entities"
        except AttributeError:
            # Fallback for older API versions or if entities property is missing
            sel = doc.selection
            all_entities = getattr(sel, "entities", [])
            if callable(all_entities): all_entities = all_entities()
            source = "sel.entities"
        
        if not all_entities:
            return []

        # Filter path entities
        path_entities = [e for e in all_entities if e.classname.lower().startswith("path_")]
        if not path_entities:
            return []

        # Build lookup maps
        by_targetname = {}
        for e in path_entities:
            tname = e.get("targetname")
            if tname:
                by_targetname[tname] = e
        
        # Identify parents (entities that target another)
        # and children (entities that are targeted)
        next_map = {}
        has_parent = set()
        
        for e in path_entities:
            target = e.get("target")
            if target and target in by_targetname:
                nxt = by_targetname[target]
                next_map[e] = nxt
                has_parent.add(nxt)
        
        # Find start nodes (not targeted by any other path entity)
        # Note: In a cycle, there are no start nodes.
        starts = [e for e in path_entities if e not in has_parent]
        
        # If no starts found but we have entities, try to pick one from a cycle
        if not starts and path_entities:
             # Just pick the first one as a potential start for a cycle
             starts = [path_entities[0]]

        chains = []
        visited_global = set()

        for start_node in starts:
            if start_node in visited_global: continue
            
            chain = []
            curr = start_node
            visited_local = set()
            
            while curr:
                chain.append(curr)
                visited_local.add(curr)
                visited_global.add(curr)
                
                if curr in next_map:
                    curr = next_map[curr]
                    if curr in visited_local: # Cycle detected within this chain
                        break 
                else:
                    curr = None
            
            if len(chain) >= 2:
                chains.append(chain)

        # Handle cycles that were not reached (isolated loops)
        # This is a bit more complex, skip for now unless requested.

        return chains

    def on_chain_selected(self, index):
        # Index mapping:
        # 0: Placeholder
        # 1: Manual
        # 2..N+1: Chains (if any)
        # N+2 (or 2 if no chains): (No chains msg) - skip
        # Last: Refresh
        
        self._selected_chain_ui_index = index

        if index == 0: return

        chain_start_idx = 2
        has_chains = len(self._detected_chains) > 0
        
        # Calculate max index
        # If has chains: [0, 1, 2..N+1, N+2(Refresh)] -> len = N+3
        # If no chains: [0, 1, 2(Msg), 3(Refresh)] -> len = 4
        
        total_items = 2 + (len(self._detected_chains) if has_chains else 1) + 1
        is_refresh = (index == total_items - 1)
        
        if is_refresh:
            self._selected_chain_ui_index = 0
            self.op_refresh()
            return
            
        if index == 1:
            self.op_record_path()
            self._build_ui() # Reset UI to reset combo box (with persisted index)
            return
            
        if not has_chains:
            # Selected the "No chains" placeholder
            return
            
        # It's a chain
        chain_idx = index - chain_start_idx
        if 0 <= chain_idx < len(self._detected_chains):
            self.op_use_chain(chain_idx)
            self._build_ui() # Reset UI

    def op_apply_selection(self):
        # Fallback for old API
        try:
            idx = self._panel.get_combo_box_index("chain_selector")
            self.on_chain_selected(idx)
        except:
            pass

    def op_refresh(self):
        self._message = "已刷新实体列表"
        self._build_ui()

    def op_use_chain(self, idx):
        # Modified to take index directly
        if idx < 0 or idx >= len(self._detected_chains):
            return

        chain = self._detected_chains[idx]
        points = []
        for e in chain:
            origin_str = e.get("origin")
            if origin_str:
                try:
                    pts = [float(x) for x in origin_str.split()]
                    if len(pts) >= 3: points.append(tuple(pts[:3]))
                except: pass
        
        if len(points) >= 2:
            self._raw_points = points
            self._path_points = self._raw_points
            self._calculate_path_metrics()
            self._refresh_status(f"已加载链 {idx+1}，共 {len(points)} 个点")
        else:
            self._refresh_status("所选链有效点数不足")


    def _refresh_status(self, msg: str | None = None) -> None:
        if msg:
            self._message = msg
        
        points_info = f"记录点数: {len(self._raw_points)}"
        if self._path_length > 0:
            points_info += f", 当前长度: {self._path_length:.1f}"
            
        full_msg = f"{self._message}\n{points_info}"
        self._panel.set_label_text("status", full_msg)

    def _current_document(self):
        return tb.Document.current()

    def _calculate_path_metrics(self):
        self._path_length = 0.0
        self._segment_lengths = []
        if len(self._path_points) < 2:
            return
            
        for i in range(len(self._path_points) - 1):
            p0 = self._path_points[i]
            p1 = self._path_points[i+1]
            dist = vec3_len(vec3_sub(p1, p0))
            self._segment_lengths.append(dist)
            self._path_length += dist

    def _sort_path_entities(self, entities):
        by_targetname = {}
        for e in entities:
            tname = e.get("targetname")
            if tname:
                by_targetname[tname] = e
        
        next_map = {}
        has_parent = set()
        for e in entities:
            target = e.get("target")
            if target and target in by_targetname:
                nxt = by_targetname[target]
                next_map[e] = nxt
                has_parent.add(nxt)
        
        starts = [e for e in entities if e not in has_parent]
        if not starts and entities: starts = [entities[0]]
        if not starts: return []
        
        result = []
        curr = starts[0]
        visited = set()
        while curr:
            result.append(curr)
            visited.add(curr)
            if curr in next_map:
                curr = next_map[curr]
                if curr in visited: break
            else:
                curr = None
        return result

    def op_record_path(self) -> None:
        doc = self._current_document()
        if not doc: return
        
        # 1. 尝试从顶点工具获取
        verts = doc.vertex_tool_vertices()
        if len(verts) >= 2:
            self._raw_points = verts
            self._path_points = self._raw_points
            self._calculate_path_metrics()
            self._refresh_status(f"已从顶点记录路径，共 {len(verts)} 个点")
            return

        # 2. 尝试从实体选择获取 (path_*)
        sel = doc.selection
        entities = getattr(sel, "entities", [])
        if callable(entities): entities = entities()
        
        path_entities = [e for e in entities if e.classname.startswith("path_")]
        
        if len(path_entities) >= 2:
            sorted_ents = self._sort_path_entities(path_entities)
            points = []
            for e in sorted_ents:
                origin_str = e.get("origin")
                if origin_str:
                    try:
                        pts = [float(x) for x in origin_str.split()]
                        if len(pts) >= 3: points.append(tuple(pts[:3]))
                    except: pass
            
            if len(points) >= 2:
                self._raw_points = points
                self._path_points = self._raw_points
                self._calculate_path_metrics()
                self._refresh_status(f"已从实体记录路径，共 {len(points)} 个点")
                return

        self._refresh_status("错误: 请先选择至少 2 个顶点或路径实体")

    def _get_path_point_at_distance(self, dist: float):
        if not self._path_points or len(self._path_points) < 2:
            return None, None
            
        # Clamp distance
        if dist <= 0:
            p0 = self._path_points[0]
            p1 = self._path_points[1]
            return p0, vec3_normalize(vec3_sub(p1, p0))
        if dist >= self._path_length:
            p0 = self._path_points[-2]
            p1 = self._path_points[-1]
            return p1, vec3_normalize(vec3_sub(p1, p0))
            
        # Find segment
        current_dist = dist
        for i, seg_len in enumerate(self._segment_lengths):
            if current_dist <= seg_len:
                # Found segment i
                p0 = self._path_points[i]
                p1 = self._path_points[i+1]
                
                if seg_len <= 0.0001:
                    local_t = 0
                else:
                    local_t = current_dist / seg_len
                    
                pos = vec3_add(p0, vec3_mul(vec3_sub(p1, p0), local_t))
                tangent = vec3_normalize(vec3_sub(p1, p0))
                return pos, tangent
            else:
                current_dist -= seg_len
                
        # Should not reach here if clamped correctly, but just in case return last point
        p0 = self._path_points[-2]
        p1 = self._path_points[-1]
        return p1, vec3_normalize(vec3_sub(p1, p0))

    def op_distribute(self) -> None:
        if len(self._raw_points) < 2:
            self._refresh_status("请先记录路径！")
            return

        # Update path points based on smoothing option
        try:
            smooth_path = self._panel.get_checkbox("smooth_path")
        except AttributeError:
             smooth_path = int(self._panel.get_int_field("smooth_path")) == 1
             
        if smooth_path:
            try:
                use_bezier = self._panel.get_checkbox("use_bezier")
            except AttributeError:
                 use_bezier = int(self._panel.get_int_field("use_bezier")) == 1
                 
            if use_bezier:
                self._path_points = bezier_curve(self._raw_points, steps_per_segment=10)
            else:
                self._path_points = catmull_rom_spline(self._raw_points, steps=10)
        else:
            self._path_points = list(self._raw_points)
            
        self._calculate_path_metrics()

        doc = self._current_document()
        if not doc: return
        
        sel = doc.selection
        # 使用 all_entities 以涵盖更广泛的选择情况（如子笔刷选中导致的实体选中）
        if not sel.all_entities:
            self._refresh_status("请先选择要分布的物体 (原型)")
            return

        # Parameters
        count = int(self._panel.get_int_field("count"))
        
        try:
            align_to_path = self._panel.get_checkbox("align_to_path")
        except AttributeError:
             align_to_path = int(self._panel.get_int_field("align_to_path")) == 1

        off_start_pct = self._panel.get_float_field("offset_start") / 100.0
        off_end_pct = self._panel.get_float_field("offset_end") / 100.0
        
        base_off_x = self._panel.get_float_field("base_off_x")
        base_off_y = self._panel.get_float_field("base_off_y")
        base_off_z = self._panel.get_float_field("base_off_z")

        rand_pos_range = self._panel.get_float_field("rand_pos")
        rand_rot_z_range = self._panel.get_float_field("rand_rot_z")
        rand_scale_pct = self._panel.get_float_field("rand_scale") / 100.0

        # Calculate effective range based on distance
        start_dist = self._path_length * off_start_pct
        end_dist = self._path_length * (1.0 - off_end_pct)
        
        # Ensure start <= end
        if start_dist > end_dist:
            start_dist = end_dist = (start_dist + end_dist) / 2
            
        effective_len = end_dist - start_dist
        
        step = 0.0
        if count > 1:
            step = effective_len / (count - 1)
        
        # Prototype bounds center (approximation)
        brush_verts_lists = sel.brush_vertices()
        all_verts = [v for sublist in brush_verts_lists for v in sublist]
        
        if not all_verts:
             self._refresh_status("选中的物体没有顶点？")
             return

        center_sum = [0,0,0]
        for v in all_verts:
            center_sum[0] += v[0]
            center_sum[1] += v[1]
            center_sum[2] += v[2]
        num_verts = len(all_verts)
        proto_center = (center_sum[0]/num_verts, center_sum[1]/num_verts, center_sum[2]/num_verts)

        # Track the state of the "current source" for duplication
        # Initially it is the prototype
        current_center = proto_center
        current_yaw = 0.0 # Assuming prototype is at 0 rotation or we treat it as base 0

        with doc.transaction("Python: Distribute Along Path"):
            for i in range(count):
                if count == 1:
                    dist = start_dist + effective_len * 0.5
                else:
                    dist = start_dist + step * i
                
                target_pos, tangent = self._get_path_point_at_distance(dist)
                
                if target_pos is None: continue # Should not happen
                
                # Create instance
                sel.duplicate()
                # Now the new copy is selected, and it is at 'current_center' with 'current_yaw'
                
                # 1. Randomization
                r_pos_x = random.uniform(-rand_pos_range, rand_pos_range)
                r_pos_y = random.uniform(-rand_pos_range, rand_pos_range)
                r_pos_z = random.uniform(-rand_pos_range, rand_pos_range)
                
                r_rot_z = random.uniform(-rand_rot_z_range, rand_rot_z_range)
                r_scale = 1.0 + random.uniform(-rand_scale_pct, rand_scale_pct)
                
                # 2. Target Transform
                if align_to_path:
                    yaw_rad = math.atan2(tangent[1], tangent[0])
                    yaw_deg = math.degrees(yaw_rad)
                else:
                    yaw_deg = 0.0

                final_yaw = yaw_deg + r_rot_z
                
                final_center = vec3_add(target_pos, (r_pos_x, r_pos_y, r_pos_z))
                final_center = vec3_add(final_center, (base_off_x, base_off_y, base_off_z))
                
                # 3. Apply Relative Transforms
                # Move
                move_vec = vec3_sub(final_center, current_center)
                sel.translate(move_vec[0], move_vec[1], move_vec[2])
                
                # Rotate
                # We rotate around the NEW center (final_center)
                delta_yaw = final_yaw - current_yaw
                sel.rotate(0, 0, 1, delta_yaw, final_center[0], final_center[1], final_center[2])
                
                # Update state for next iteration (since next duplicate will start from here)
                current_center = final_center
                current_yaw = final_yaw
                
                # 4. Scale
                if abs(r_scale - 1.0) > 0.001:
                    try:
                        sel.scale(r_scale, r_scale, r_scale, final_center[0], final_center[1], final_center[2])
                    except AttributeError:
                        print("Warning: sel.scale not available (update TB to latest)")
                
        self._refresh_status(f"成功生成 {count} 个实例")

def register():
    global _ADDON
    _ADDON = DistributeTool()

if __name__ == "__main__":
    register()
