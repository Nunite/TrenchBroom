

function assignMapTextures()
	n = faceCount(Brush(0))
	brushFaces = Vector{BrushFace}(n)
	for i in 1:n
		brushFaces[i] = BrushFace(0, i - 1) # -1, because C indexing :S
	end

	for brushFace in brushFaces
		texname = textureName( brushFaces[1] )
		tex = find_texture( texname )
		setTexture( brushFace, tex )
	end
end