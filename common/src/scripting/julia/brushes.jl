type Brush
	id::Int
end

type BrushFace
	brush::Brush
	face_id::Int
	function BrushFace(brush_id, face_id)
		brush = Brush(brush_id)
		if faceCount(brush) < face_id
			throw( ArgumentError("brush doesnt have so many faces") ) 
		end
		return new(brush, face_id)
	end
end

selectedBrushesCount() = ccall( :ffi_selected_brushes_count, Int, ())

ffi_free(ptr) = ccall(:ffi_free, Void, (Ptr{Void},), ptr)

# rotate( selectedBrushes()[1], 0.3, 0, 0)
rotate(brush::Brush, roll, pitch, yaw) = ccall( :ffi_brush_rotate, Bool, (Int32, Float32, Float32, Float32), brush.id, roll, pitch, yaw)

function selectedBrushes()::Array{Brush}
	ptrlist = ccall( :ffi_selected_brushes, Ptr{Int}, ())
	if ptrlist == C_NULL
		throw( ArgumentError("ptrlist == C_NULL") )
	end
	n = selectedBrushesCount()
	brushes = Array{Brush}(n)
	for i in 1:n
		brushes[i] = Brush( unsafe_load(ptrlist, i) )
	end
	ffi_free(ptrlist)
	return brushes
end

function faceCount(brush::Brush)
	ccall( :ffi_Brush_faceCount, Int, (Int,), brush.id)
end

face_get_texture_name(brush_id, face_id) = ccall( :ffi_face_get_texture_name, Ptr{Cchar}, (Int, Int), brush_id, face_id)

function textureName(brushFace::BrushFace)::String
	cstr = face_get_texture_name(brushFace.brush.id, brushFace.face_id)
	if cstr == C_NULL
		return "NULL"
	end
	return unsafe_string( cstr )
end

if false
	brushFace = BrushFace(0,0)
	textureName( brushFace )
	
	faceCount(Brush(0))
	BrushFace(0,20)
end

function setTexture(brushFace::BrushFace, texture::Texture)
	brush_id = brushFace.brush.id
	face_id = brushFace.face_id
	texture_id = texture.id
	ccall( :ffi_brushface_set_texture, Void, (Int, Int, Int), brush_id, face_id, texture_id)
end

if false
	setTexture( BrushFace(0,0), Texture(4) )
	texname = textureName(BrushFace(0,0))
	find_texture( texname )
	
	for i in 0:5
		setTexture( BrushFace(2,i), Texture(4) )
	end
end