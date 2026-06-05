import tb2 as tb

panel = tb.create_plugin_panel("V2 Hello")
panel.add_label("Hello from a manifest plugin.")
panel.add_button("Print", lambda: print("V2 hello panel button clicked"))
