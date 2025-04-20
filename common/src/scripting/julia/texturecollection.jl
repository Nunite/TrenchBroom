type TextureCollection
	id::Int
	function TextureCollection(name::AbstractString)
		new( ccall( :ffi_new_texture_collection, Int, (Cstring,), name) )
	end
	function TextureCollection(id::Int)
		new( id )
	end
end

function addTexture(texture_collection::TextureCollection, filename, basename)
	ccall( :ffi_add_texture, Bool, (Int, Cstring, Cstring), texture_collection.id, filename, basename)
end

if false
	hello = TextureCollection("hello")
	addTexture(hello, "some/file.jpg", "file")
end