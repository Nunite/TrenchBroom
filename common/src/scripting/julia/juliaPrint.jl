juliaPrint(text) = ccall( :ffi_julia_print, Void, (Cstring,), text)
imgui_log = juliaPrint # just compatibility to my console.jl