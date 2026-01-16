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

class DistributeTool:
    def __init__(self) -> None:
        self._panel = tb.create_plugin_panel("沿路径分布")
        self._path_points = []
        self._message = "请先选择路径点（顶点工具）"
        self._build_ui()
        self._refresh_status()

    def _build_ui(self) -> None:
        self._panel.clear()
        self._panel.add_label_named("status", self._message)
        
        self._panel.add_button_callback("1. 从选定顶点记录路径", self.op_record_path)
        
        self._panel.add_int_field("count", "生成数量", 5, 2, 100)
        self._panel.add_float_field("offset_start", "起始偏移 (%)", 0.0, 0.0, 100.0, 1, 5.0)
        self._panel.add_float_field("offset_end", "结束偏移 (%)", 0.0, 0.0, 100.0, 1, 5.0)
        
        self._panel.add_label("随机化参数:")
        self._panel.add_float_field("rand_pos", "位置抖动", 0.0, 0.0, 128.0, 1, 1.0)
        self._panel.add_float_field("rand_rot_z", "随机旋转 Z (度)", 0.0, 0.0, 360.0, 1, 15.0)
        self._panel.add_float_field("rand_scale", "随机缩放 (%)", 0.0, 0.0, 100.0, 1, 10.0)
        
        self._panel.add_button_callback("2. 分布选中物体", self.op_distribute)
        
        self._panel.add_label("说明: 先用顶点工具选点记录路径，再切回普通工具选中要复制的物体。")

    def _refresh_status(self, msg: str | None = None) -> None:
        if msg:
            self._message = msg
        
        points_info = f"当前路径点数: {len(self._path_points)}"
        full_msg = f"{self._message}\n{points_info}"
        self._panel.set_label_text("status", full_msg)

    def _current_document(self):
        return tb.Document.current()

    def op_record_path(self) -> None:
        doc = self._current_document()
        if not doc: return
        
        verts = doc.vertex_tool_vertices()
        if len(verts) < 2:
            self._refresh_status("错误: 至少需要选择 2 个顶点来构成路径")
            return
            
        # TB 返回的顶点顺序通常是选择顺序，或者按内存顺序
        # 假设用户按顺序选择，或者是按空间排序？
        # 这里暂时直接使用返回的顺序。如果用户框选，顺序可能不确定。
        # 一个改进是按距离排序链起来，但这里先简单处理。
        self._path_points = verts
        self._refresh_status(f"已记录路径，共 {len(verts)} 个点")

    def _get_path_point_at(self, t: float):
        # t is 0.0 to 1.0
        # Simple linear interpolation between points
        if not self._path_points: return None, None
        
        total_segments = len(self._path_points) - 1
        if total_segments < 1: return self._path_points[0], (1,0,0) # Should not happen
        
        segment_t = t * total_segments
        idx = int(segment_t)
        if idx >= total_segments:
            idx = total_segments - 1
            local_t = 1.0
        else:
            local_t = segment_t - idx
            
        p0 = self._path_points[idx]
        p1 = self._path_points[idx+1]
        
        # Position
        pos = vec3_add(p0, vec3_mul(vec3_sub(p1, p0), local_t))
        
        # Tangent (Direction)
        tangent = vec3_normalize(vec3_sub(p1, p0))
        
        return pos, tangent

    def op_distribute(self) -> None:
        if len(self._path_points) < 2:
            self._refresh_status("请先记录路径！")
            return

        doc = self._current_document()
        if not doc: return
        
        sel = doc.selection
        # 使用 all_entities 以涵盖更广泛的选择情况（如子笔刷选中导致的实体选中）
        if not sel.all_entities:
            self._refresh_status("请先选择要分布的物体 (原型)")
            return

        # Parameters
        count = int(self._panel.get_int_field("count"))
        off_start = self._panel.get_float_field("offset_start") / 100.0
        off_end = self._panel.get_float_field("offset_end") / 100.0
        rand_pos_range = self._panel.get_float_field("rand_pos")
        rand_rot_z_range = self._panel.get_float_field("rand_rot_z")
        rand_scale_pct = self._panel.get_float_field("rand_scale") / 100.0

        # Calculate effective range
        effective_len = 1.0 - off_start - off_end
        if effective_len <= 0: effective_len = 0
        
        step = 0.0
        if count > 1:
            step = effective_len / (count - 1)
        
        # Prototype bounds center (approximation)
        # We need to move the prototype from its current position to the target position
        # So we need its current center.
        # But tb API doesn't give bounds easily yet for complex selection.
        # We assume the user wants the pivot to be the center of selection or 0,0,0?
        # Better: calculate centroid of selected brushes vertices
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

        with doc.transaction("Python: Distribute Along Path"):
            for i in range(count):
                t = off_start + step * i
                if count == 1: t = 0.5 # if only 1, put in middle? or start? let's stick to logic
                
                target_pos, tangent = self._get_path_point_at(t)
                
                # Create instance
                sel.duplicate()
                # Now the new copy is selected
                
                # 1. Randomization
                r_pos_x = random.uniform(-rand_pos_range, rand_pos_range)
                r_pos_y = random.uniform(-rand_pos_range, rand_pos_range)
                r_pos_z = random.uniform(-rand_pos_range, rand_pos_range)
                
                r_rot_z = random.uniform(-rand_rot_z_range, rand_rot_z_range)
                r_scale = 1.0 + random.uniform(-rand_scale_pct, rand_scale_pct)
                
                # 2. Rotation to align with path
                # Default forward is usually X or Y? Let's assume user wants object's X axis to align with path
                # Or we simply rotate Z based on tangent's yaw
                # Calculate Yaw from tangent
                # tangent is (x, y, z)
                # yaw = atan2(y, x)
                yaw_rad = math.atan2(tangent[1], tangent[0])
                yaw_deg = math.degrees(yaw_rad)
                
                # Apply Rotation
                # Rotate around prototype center first?
                # Actually duplicate() keeps properties.
                # We want to rotate IT to match tangent.
                # Assuming original object is facing East (0 degrees).
                # If original is not facing East, user might need an offset.
                # For now, we apply yaw_deg.
                
                # Apply Random Z Rotation
                final_yaw = yaw_deg + r_rot_z
                
                # Perform Transforms
                # 1. Move from proto_center to target_pos
                move_vec = vec3_sub(target_pos, proto_center)
                move_vec = vec3_add(move_vec, (r_pos_x, r_pos_y, r_pos_z))
                
                sel.translate(move_vec[0], move_vec[1], move_vec[2])
                
                # 2. Rotate at target position
                # rotate(axis_x, axis_y, axis_z, angle, center_x, center_y, center_z)
                # We rotate around the NEW center (target_pos)
                new_center = vec3_add(target_pos, (r_pos_x, r_pos_y, r_pos_z))
                sel.rotate(0, 0, 1, final_yaw, new_center[0], new_center[1], new_center[2])
                
                # 3. Scale? TB API currently doesn't have scale() for selection :(
                # Map2Curvex had it, but current Python API selection protocol doesn't list scale.
                # So we skip scale for now.
                
        self._refresh_status(f"成功生成 {count} 个实例")

def register():
    global _ADDON
    _ADDON = DistributeTool()

if __name__ == "__main__":
    register()
