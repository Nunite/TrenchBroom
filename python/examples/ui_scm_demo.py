"""
Native UI Demo: SCM Tree
========================

Demonstrates the enhanced Tree Widget API for building Git-like SCM views.
"""

import tb

_PANEL: tb.PluginPanel | None = None

def main():
    global _PANEL
    
    if _PANEL is None:
        _PANEL = tb.create_plugin_panel("SCM")
    
    _PANEL.clear()

    # Create the Tree Widget
    # Note: We now use add_tree_node to populate it, so initial items can be empty
    _PANEL.add_tree_widget("scm_tree", ["Files"], [], height=300)

    # Add Staged Changes Section (Folder)
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="staged",
        text="Staged Changes",
        icon="folder",
        expanded=True
    )

    # Add Staged Files
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="file1",
        parent_id="staged",
        text="src/main.cpp",
        icon="file",
        checkable=True,
        checked=True
    )
    
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="file2",
        parent_id="staged",
        text="src/utils.h",
        icon="file",
        checkable=True,
        checked=True
    )

    # Add Changes Section (Folder)
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="changes",
        text="Changes",
        icon="folder",
        expanded=True
    )

    # Add Modified Files
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="file3",
        parent_id="changes",
        text="README.md",
        icon="modified",
        checkable=True,
        checked=False
    )
    
    # Add Untracked File
    _PANEL.add_tree_node(
        "scm_tree",
        node_id="file4",
        parent_id="changes",
        text="new_feature.py",
        icon="add",
        checkable=True,
        checked=False
    )

    # Commit Area
    grp = _PANEL.add_group("commit_area", "Commit")
    grp.add_text_area("commit_msg", "", height=60, placeholder="Message (Ctrl+Enter to commit)")
    grp.add_button("Commit Staged", None)

    print("SCM Tree Demo loaded.")

if __name__ == "__main__":
    main()
