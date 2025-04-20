importall Base
using Glob

include("juliaPrint.jl")
include("console.jl")

log(console, "Loading init.jl")

try
	include("filename.jl")
	include("ffi_textures.jl")
	include("brushes.jl")
	include("ffi_update_views.jl")
	include("texturemanager.jl")
	include("TextureCollection.jl")
	include("TextureBrowserView.jl")
	include("allTextures.jl")
	include("eval.jl")
	include("eval_and_prettyprint.jl")
	include("removeExtension.jl")
	include("libwebgame.jl")
	include("php.jl")
	include("assignMapTextures.jl")
catch ex
	log(console, ex)
end
