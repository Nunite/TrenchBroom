import tb2 as tb

doc = tb.current_document()
worldspawn = doc.entities[0]
worldspawn.set("_tb2_example", "enabled")
print(f"_tb2_example = {worldspawn.get('_tb2_example')}")
