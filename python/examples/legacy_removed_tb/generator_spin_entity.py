import tb

def main():
    doc = tb.Document.current()
    selection = doc.selection
    
    # 1. 验证选择
    # 使用 all_entities 属性而不是方法，因为 C++ 接口已修复
    entities = selection.all_entities
    
    if len(entities) == 0:
        print("错误: 未选中任何实体。")
        return

    # 过滤掉 worldspawn (如果用户不小心选中的话)
    entities = [e for e in entities if e.classname != "worldspawn"]

    if len(entities) != 1:
        print(f"错误: 请选中且仅选中一个 func_detail 实体。当前选中了 {len(entities)} 个实体。")
        return

    entity = entities[0]
    # 如果不仅限于 func_detail，可以去掉这个检查
    if entity.classname != "func_detail":
        print(f"警告: 选中的实体是 {entity.classname}，建议使用 func_detail。")

    # 2. 读取属性
    try:
        # 读取总角度 (默认 90 度)
        total_angle = float(entity.get("_angle", "90.0"))
        
        # 读取段数 (默认 5 段)
        count = int(entity.get("_count", "5"))
        if count < 1:
            print("错误: _count 必须大于 0")
            return

        # 读取旋转中心 (必须提供)
        # 格式: "x y z"
        pivot_str = entity.get("_pivot", "")
        if not pivot_str:
            print("错误: 必须设置 '_pivot' 属性 (格式: 'x y z') 来指定旋转中心。")
            return
        
        pivot = [float(x) for x in pivot_str.split()]
        if len(pivot) != 3:
            print("错误: '_pivot' 属性格式错误，需要 3 个数值。")
            return
            
        # 读取旋转轴 (默认 Z 轴)
        axis_str = entity.get("_axis", "0 0 1")
        axis = [float(x) for x in axis_str.split()]
        if len(axis) != 3:
            axis = [0.0, 0.0, 1.0]
            
    except ValueError:
        print("错误: 属性数值格式不正确。")
        return

    # 计算每一步的角度
    step_angle = total_angle / count

    print(f"开始生成弯管: 中心={pivot}, 总角度={total_angle}, 段数={count}, 单步角度={step_angle}")

    # 3. 执行生成循环
    # 使用事务，方便撤销
    with doc.transaction("Spin Func_Detail"):
        for i in range(count):
            # 复制当前选中的实体
            # duplicate() 会自动选中新生成的副本，并取消选中原来的
            selection.duplicate()
            
            # 旋转当前选中的副本
            # 参数: x轴, y轴, z轴, 角度(度), 中心x, 中心y, 中心z
            selection.rotate(axis[0], axis[1], axis[2], step_angle, pivot[0], pivot[1], pivot[2])

    print("生成完成。")

if __name__ == "__main__":
    main()