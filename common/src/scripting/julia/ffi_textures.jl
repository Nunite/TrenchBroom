type Texture
	id::Int
end

function find_texture(name)
	id = ccall( :ffi_find_texture, Int, (Cstring,), name)
	if id == -1
		# todo: when texture doesnt exist... then generate a random texture or return a default texture
		return Texture(0) # id 0 gotta be default texture for now xD
	end
	return Texture(id)
end

pointer(texture::Texture) = ccall( :ffi_get_texture_pointer_by_id, Ptr{Void}, (Int,), texture.id)


if false
	texture = find_texture("concrete/white")
	pointer( texture )
end