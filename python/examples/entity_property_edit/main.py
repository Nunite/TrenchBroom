import trenchbroom as tb

doc = tb.current_document()
worldspawn = doc.entities[0]
worldspawn.set("_trenchbroom_example", "enabled")
print(f"_trenchbroom_example = {worldspawn.get('_trenchbroom_example')}")
