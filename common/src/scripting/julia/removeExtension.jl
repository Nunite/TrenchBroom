function removeExtension(path)
	pos = findlast(path, '.')
	if pos == 0
		return path # when no dot, just return full path
	end
	pos -= 1 # we dont wanna include the dot
	return path[1 : pos]
end