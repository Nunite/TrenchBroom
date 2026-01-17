import tb
import math

def main():
    """
    演示新的 Plane 类和扩展的 Selection API 在 UI 面板中的使用。
    """
    try:
        # 获取当前文档，如果在 TB 外部运行则打印错误
        doc = tb.Document.current()
    except RuntimeError:
        print("请在 TrenchBroom 内部运行此脚本。")
        return

    if doc is None:
        print("未找到活动文档。")
        return

    # 在 Inspector 中创建一个插件面板
    panel = tb.create_plugin_panel("平面与选择演示")
    
    # =========================================================================
    # 1. 平面演示部分
    # =========================================================================
    panel.add_label("<b>平面演示</b>")
    panel.add_label("使用顶点工具选中 3 个顶点，然后点击创建：")
    
    # 用于显示创建的平面信息的标签
    panel.add_label_named("plane_info", "尚未创建平面。")
    
    # 用于保存创建的平面实例的状态容器
    state = {
        "plane": None
    }

    def create_visual_brush_from_plane(plane, size=64.0, thickness=4.0):
        """
        创建一个可视化的 Brush 来代表平面。
        原理：在平面上构建一个大的正方形，然后挤出厚度。
        """
        # 1. 找到平面上的一个基准点 (origin)
        # plane equation: dot(n, p) - d = 0  => dot(n, p) = d
        # p = n * d (这是平面上离原点最近的点)
        origin = plane.normal * plane.dist
        
        # 2. 构建平面坐标系 (u, v)
        # 找一个不与 normal 平行的任意向量 up
        if abs(plane.normal.z) < 0.9:
            up = tb.Vec3(0, 0, 1)
        else:
            up = tb.Vec3(1, 0, 0)
            
        # u_axis = (up * plane.normal).cross(plane.normal) # 这一行不需要，我们下面手动计算了
        # 手动计算叉积 cross(up, normal)
        # up x normal
        u_axis_val = tb.Vec3(
            up.y * plane.normal.z - up.z * plane.normal.y,
            up.z * plane.normal.x - up.x * plane.normal.z,
            up.x * plane.normal.y - up.y * plane.normal.x
        )
        # 归一化
        length = math.sqrt(u_axis_val.x**2 + u_axis_val.y**2 + u_axis_val.z**2)
        if length < 0.0001:
             u_axis_val = tb.Vec3(1, 0, 0) # Fallback
        else:
             u_axis_val = u_axis_val / length
             
        v_axis_val = tb.Vec3(
            plane.normal.y * u_axis_val.z - plane.normal.z * u_axis_val.y,
            plane.normal.z * u_axis_val.x - plane.normal.x * u_axis_val.z,
            plane.normal.x * u_axis_val.y - plane.normal.y * u_axis_val.x
        )
        
        # 3. 计算正方形的 4 个顶点
        half_size = size
        p1 = origin - u_axis_val * half_size - v_axis_val * half_size
        p2 = origin + u_axis_val * half_size - v_axis_val * half_size
        p3 = origin + u_axis_val * half_size + v_axis_val * half_size
        p4 = origin - u_axis_val * half_size + v_axis_val * half_size
        
        # 4. 挤出厚度生成凸包顶点 (8个点)
        # 往平面法线反方向挤出，这样平面本身是顶面
        offset = plane.normal * (-thickness)
        
        points = [
            p1, p2, p3, p4,
            p1 + offset, p2 + offset, p3 + offset, p4 + offset
        ]
        
        # 5. 创建 Brush
        brush = tb.create_brush(points)
        return brush

    def on_create_plane():
        try:
            # 获取顶点工具选中的顶点
            verts = doc.vertex_tool_vertices()
            if len(verts) < 3:
                panel.set_label_text("plane_info", "错误：请至少选择 3 个顶点。")
                print("错误：需要在顶点工具中选择 3 个顶点。")
                return
            
            # 取前 3 个顶点
            p1 = tb.Vec3(*verts[0])
            p2 = tb.Vec3(*verts[1])
            p3 = tb.Vec3(*verts[2])
            
            # 根据点创建平面
            plane = tb.Plane.from_points(p1, p2, p3)
            state["plane"] = plane
            
            # 创建可视化的 Brush
            vis_brush = create_visual_brush_from_plane(plane, size=128.0, thickness=8.0)
            
            # 更新 UI
            info = f"法线: ({plane.normal.x:.2f}, {plane.normal.y:.2f}, {plane.normal.z:.2f})\n距离: {plane.dist:.2f}"
            if vis_brush:
                info += "\n(已生成可视化 Brush)"
            else:
                info += "\n(生成 Brush 失败)"
                
            panel.set_label_text("plane_info", info)
            print(f"已创建平面: {plane}")
            
        except Exception as e:
            panel.set_label_text("plane_info", f"错误: {e}")
            print(f"异常: {e}")
            import traceback
            traceback.print_exc()

    panel.add_button_callback("从选中顶点创建平面并生成 Brush", on_create_plane)
    
    # 测试点输入
    panel.add_label("测试点 (世界坐标):")
    panel.add_float_field("tp_x", "X", 0.0)
    panel.add_float_field("tp_y", "Y", 0.0)
    panel.add_float_field("tp_z", "Z", 0.0)
    
    panel.add_label_named("dist_info", "距离: -")
    
    def on_calc_dist():
        if state["plane"] is None:
            panel.set_label_text("dist_info", "请先创建一个平面。")
            return
        
        try:
            # 读取输入
            x = panel.get_float_field("tp_x")
            y = panel.get_float_field("tp_y")
            z = panel.get_float_field("tp_z")
            pt = tb.Vec3(x, y, z)
            
            # 计算距离和投影
            dist = state["plane"].distance(pt)
            proj = state["plane"].project(pt)
            
            # 更新 UI
            result_text = f"距离: {dist:.2f}\n投影: ({proj.x:.1f}, {proj.y:.1f}, {proj.z:.1f})"
            panel.set_label_text("dist_info", result_text)
            print(f"点 {pt}: 距离={dist}, 投影={proj}")
            
        except Exception as e:
            print(f"异常: {e}")

    panel.add_button_callback("计算距离与投影", on_calc_dist)

    # =========================================================================
    # 2. 选择 API 演示部分
    # =========================================================================
    panel.add_label("<b>选择 API 演示</b>")
    
    def on_select_worldspawn_brushes():
        """选中属于 worldspawn (实体 0) 的所有笔刷。"""
        try:
            if not doc.entities:
                return
            
            # Worldspawn 通常是第一个实体
            worldspawn = doc.entities[0]
            if worldspawn.classname == "worldspawn":
                brushes = worldspawn.brushes
                if brushes:
                    doc.selection.set(brushes)
                    print(f"选中了 {len(brushes)} 个 worldspawn 笔刷。")
                else:
                    print("Worldspawn 没有笔刷。")
            else:
                print("实体 0 不是 worldspawn？")
        except Exception as e:
            print(f"异常: {e}")

    panel.add_button_callback("设置选择：Worldspawn 笔刷", on_select_worldspawn_brushes)
    
    def on_add_first_entity():
        """将第一个非 worldspawn 实体添加到当前选择中。"""
        try:
            target = None
            for ent in doc.entities:
                if ent.classname != "worldspawn":
                    target = ent
                    break
            
            if target:
                doc.selection.add([target])
                print(f"已将实体 '{target.classname}' 添加到选择。")
            else:
                print("未找到非 worldspawn 实体。")
        except Exception as e:
            print(f"异常: {e}")

    panel.add_button_callback("添加到选择：首个实体", on_add_first_entity)

    def on_deselect_all():
        """清空选择。"""
        doc.selection.deselect_all()
        print("选择已清空。")

    panel.add_button_callback("取消全选", on_deselect_all)
    
    print("UI 面板已创建。请查看 Inspector 的 'Plugin' 标签页。")

if __name__ == "__main__":
    main()
