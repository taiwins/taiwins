--[[
   how we want to it
]]--

require "os"

compositor = create_test()

function random_function()
   print(8*9, 8/9)
end

compositor:bind_key(random_function, "Ctrl-x,Ctrl-a")
