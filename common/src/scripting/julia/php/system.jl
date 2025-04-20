# GPL + MIT license
# kungfooman 2017 - 2018

# tldr: system("echo hi") == "hi"

cmd_cmd(command) = Cmd(["cmd.exe", "/c", command])
cmd_bash(command) = Cmd(["C:/msys64/usr/bin/bash.exe", "-c", command])

function spawncmd(cmd::Cmd)
	in = Pipe()
	out = Pipe()
	err = Pipe()
	r = spawn(cmd, (in, out, err))
	close(in);
	close(out.in);
	close(err.in);
	return trim(readstring(out) * readstring(err));
end

system(cmd::AbstractString)::String = spawncmd( cmd_cmd(cmd) );