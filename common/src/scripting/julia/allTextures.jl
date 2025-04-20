function allTextures(path)
	filenames = Glob.glob("*/*.jpg", path)
	from = length(path)
	if path[from] == '/'
		from += 1 # skip first "/" in "/wood/c.jpg if it exists
	else
		from += 2 # advance over "/"
	end

	shortFilenames = String[]
	for tmp in filenames
		to = length(tmp)
		shortFilename = tmp[from:to] # should be something like wood/c.jpg
		push!(shortFilenames, shortFilename)
	end
	return shortFilenames
end

if false
	path = "C:/xampp/htdocs/WebFiles/libwebgame/textures"
	filenames = allTextures(path)
	for tmp in filenames
		log(console, removeExtension(tmp))
	end
end