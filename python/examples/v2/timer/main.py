import tb2 as tb

ticks = 0
token = None


def on_tick():
    global ticks
    ticks += 1
    print(f"timer tick {ticks}")
    if ticks >= 5:
        tb.clear_interval(token)
        print("timer stopped")


token = tb.set_interval(on_tick, 1000)
print("V2 timer started")
