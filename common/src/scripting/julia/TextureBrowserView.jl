type TextureBrowserView
	id::Int
end

function usageCountDidChange(this::TextureBrowserView)::Void
	ccall( :ffi_TextureBrowserView_usageCountDidChange, Void, (Int,), this.id)
end