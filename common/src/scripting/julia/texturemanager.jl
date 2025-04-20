type TextureManager
	id::Int
end



addTextureFolder(folder) = ccall( :ffi_add_texture_folder, Bool, (Cstring, ), folder)