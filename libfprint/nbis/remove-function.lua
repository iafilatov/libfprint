#!/bin/lua

function read_all(file)
	local f = io.open(file, "r")
	if not f then return nil end
	local t = f:read("*all")
	f:close()
	return t
end

function write_all(file, content)
	local f = io.open(file, "w")
	f:write(content)
	f:close()
end

-- From http://lua-users.org/wiki/SplitJoin
function split_lines(str)
   local t = {}
   local function helper(line)
      table.insert(t, line)
      return ""
   end
   helper((str:gsub("(.-)\r?\n", helper)))
   return t
end

function remove_func_content(func, content)
	local lines = split_lines(content)
	local res = {}
	local in_func = false
	local num_braces = 0
	local found_func = false
	for k, v in ipairs(lines) do
		if in_func == true then
			local orig_braces = num_braces
			local _, count = string.gsub(v, "{", "")
			num_braces = num_braces + count
			_, count = string.gsub(v, "}", "")
			num_braces = num_braces - count
			if orig_braces ~= 0 and num_braces == 0 then
				print (func .. ' finished line '.. k)
				in_func = false
			end
		elseif (v:match(' '..func..'%(%a+') or
		        v:match(' '..func..' %(%a+') or
		        v:match(' '..func..'%( %a+') or
		        v:match(' '..func..'%($')) and
		        not v:match('= '..func..'%(%a+') then
			print (func .. ' started line ' .. k)
			found_func = true
			in_func = true
		else
			table.insert(res, v)
		end
	end

	if not found_func then
		return nil
	end
	return table.concat(res, '\n')
end

function remove_func_file(func, file)
	content = read_all(file)
	content = remove_func_content(func, content)
	if not content then
		error('Could not find function '..func..'() in '..file)
	else
		write_all(file, content)
	end
end

local func
for k, v in ipairs(arg) do
	if k == 1 then
		func = v
	else
		remove_func_file(func, v)
	end
end

