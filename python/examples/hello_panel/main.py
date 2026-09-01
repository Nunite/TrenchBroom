import trenchbroom as tb

panel = tb.create_plugin_panel("Hello")
panel.add_label("Hello from a manifest plugin.")
panel.add_button("Print", lambda: print("Hello panel button clicked"))
