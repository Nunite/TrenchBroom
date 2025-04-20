
if isdefined(:libwebgame) == false
	libwebgame = TextureCollection("libwebgame")
	path = "C:/xampp/htdocs/WebFiles/libwebgame/textures"
	filenames = allTextures(path)
	for lastPart in filenames # lastPart is e.g. "concrete/asphalt.jpg"
		addTexture(libwebgame, path * "/" * lastPart, removeExtension(lastPart))
	end
end


