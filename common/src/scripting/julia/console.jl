abstract type AbstractConsole end
type Console <: AbstractConsole end
console = Console()

function log(this::AbstractConsole, msgs...)::Void
	for msg in msgs
		toprint = string(msg)
		imgui_log(toprint)
		imgui_log(" ")
		print(toprint, " ")
	end
	imgui_log("\n")
	print("\n")
	nothing
end

# like log(), but prints a stack trace aswell
function error(this::AbstractConsole, msgs...)::Void
	for msg in msgs
		toprint = string(msg)
		imgui_log(toprint)
		imgui_log(" ")
		print(toprint, " ")
	end

	imgui_log("\n")
	print("\n")


	imgui_log("Stack Trace:\n")
	print("Stack Trace:\n")

	for func in stacktrace()
		log(console, func)
	end

	imgui_log("\n")
	print("\n")
	nothing
end





if false
	log(console, "Spawned:", player1)
end
