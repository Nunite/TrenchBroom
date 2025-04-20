# GPL + MIT license
# kungfooman 2017 - 2018

function trim(str::AbstractString, ignore::Array{UInt8,1} = UInt8[0x20, 0x09, 0x0a, 0x0D, 0x00, 0x0b])::String
	from = 1
	
	# figure out the first char not in ignore
	for c in str
		if ! (UInt8(c) in ignore)
			#println("c is in ignore, from=", from)
			break
		end
		from = from + 1
	end
	
	n = length(str);
	to = n
	
	# and figure out the position of last valid char
	for i in n:-1:from
		c = str[i]
		if ! (UInt8(c) in ignore)
			#println("c is in ignore, from=", from)
			break
		end
		to = to - 1
	end
	
	ret = str[from:to]
	#println("cut from: ", from, " to ", to, " result: \"", ret, "\"")
	ret
end
#trim("\0    \ntest     \n\0\t")
#trim("test   ")
